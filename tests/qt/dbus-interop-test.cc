// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <libtransmission-app/dbus-peer-record.h>
#include <libtransmission-app/interop-names.h>
#include <libtransmission-app/interop.h>
#include <libtransmission-app/startup-coordinator.h>

#include "InteropObject.h"
#include "Transports.h"

namespace
{

auto const ServiceName = QStringLiteral(TR_INTEROP_DBUS_SERVICE_NAME);
auto const InterfaceName = QStringLiteral(TR_INTEROP_DBUS_INTERFACE_NAME);
auto const ObjectPath = QStringLiteral(TR_INTEROP_DBUS_OBJECT_PATH);

// A client built from other sources: same methods, its own interface name.
auto const ForeignInterfaceName = QStringLiteral("org.example.OtherClient");
auto const ForeignObjectPath = QStringLiteral("/org/example/OtherClient");

// What every stand-in needs, and no interface name:
// Q_CLASSINFO is inherited, so one declared in the base would be found for every subclass.
class FakeClientBase : public QObject
{
    Q_OBJECT

public:
    [[nodiscard]] QStringList received() const
    {
        auto const lock = QMutexLocker{ &mutex_ };
        return received_;
    }

public slots:
    // NOLINTBEGIN(readability-identifier-naming)
    bool AddMetainfo(QString const& metainfo)
    {
        auto const lock = QMutexLocker{ &mutex_ };
        received_.append(metainfo);
        return true;
    }
    // NOLINTEND(readability-identifier-naming)

private:
    mutable QMutex mutex_;
    QStringList received_;
};

// A client from a release that had no ConfigDir(),
// so it cannot say which config dir it is on.
class UnplaceableFakeClient : public FakeClientBase
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", TR_INTEROP_DBUS_INTERFACE_NAME)
};

// A stand-in that answers both calls a delegating launch makes.
// Torrents it is handed land in received();
// the interface name is left to the subclasses, since one declared here would be inherited by all of them.
class PlaceableFakeClient : public FakeClientBase
{
    Q_OBJECT

public:
    explicit PlaceableFakeClient(QString config_dir)
        : config_dir_{ std::move(config_dir) }
    {
    }

public slots:
    // NOLINTBEGIN(readability-identifier-naming)
    [[nodiscard]] QString ConfigDir() const
    {
        return config_dir_;
    }
    // NOLINTEND(readability-identifier-naming)

private:
    QString const config_dir_;
};

class FakeClient : public PlaceableFakeClient
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", TR_INTEROP_DBUS_INTERFACE_NAME)

public:
    using PlaceableFakeClient::PlaceableFakeClient;
};

// A client built from other sources: the same calls under its own interface name.
// A caller has to be willing to speak it.
class ForeignFakeClient : public PlaceableFakeClient
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.example.OtherClient")

public:
    using PlaceableFakeClient::PlaceableFakeClient;
};

// Puts a fake client on its own bus connection in its own thread.
// The fake needs that thread to answer at all. The transport under test calls over D-Bus
// and blocks for the reply, so a client sharing the calling thread could never run.
class ClientThread
{
public:
    ClientThread(QString connection_name, QObject* const client, bool const own_service_name, QString path = ObjectPath)
        : connection_name_{ std::move(connection_name) }
    {
        client->moveToThread(&thread_);
        thread_.start();

        // Connect from inside the thread that will dispatch it.
        QMetaObject::invokeMethod(
            client,
            [this, client, own_service_name, path = std::move(path)]()
            {
                auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, connection_name_);
                bus.registerObject(path, client, QDBusConnection::ExportAllSlots);

                if (own_service_name)
                {
                    bus.registerService(ServiceName);
                }

                unique_name_ = bus.baseService();
            },
            Qt::BlockingQueuedConnection);
    }

    ClientThread(ClientThread&&) = delete;
    ClientThread(ClientThread const&) = delete;
    ClientThread& operator=(ClientThread&&) = delete;
    ClientThread& operator=(ClientThread const&) = delete;

    ~ClientThread()
    {
        thread_.quit();
        thread_.wait();
        QDBusConnection::disconnectFromBus(connection_name_);
    }

    [[nodiscard]] QString const& uniqueName() const
    {
        return unique_name_;
    }

private:
    QString const connection_name_;
    QString unique_name_;
    QThread thread_;
};

// This process's Instance for the publish cases. It records what a caller asked of it,
// and on which thread the transport delivered the call.
class RecordingInstance final : public tr::interop::Instance
{
public:
    explicit RecordingInstance(std::string config_dir)
        : config_dir_{ std::move(config_dir) }
    {
    }

    [[nodiscard]] tr::interop::Reply present_window() override
    {
        ++presents;
        called_on = QThread::currentThread();
        return tr::interop::Reply::Yes;
    }

    [[nodiscard]] tr::interop::Reply add_metainfo(std::string_view const metainfo) override
    {
        adds.emplace_back(metainfo);
        called_on = QThread::currentThread();
        return tr::interop::Reply::Yes;
    }

    [[nodiscard]] std::string config_dir() override
    {
        called_on = QThread::currentThread();
        return config_dir_;
    }

    [[nodiscard]] std::string description() const override
    {
        return "recording instance";
    }

    int presents = 0;
    std::vector<std::string> adds;
    QThread* called_on = nullptr;

private:
    std::string const config_dir_;
};

void writeRawRecord(QString const& config_dir, QString const& contents)
{
    auto const filename = QString::fromUtf8(
        std::data(tr::interop::PeerRecordFilename),
        static_cast<int>(std::size(tr::interop::PeerRecordFilename)));
    auto file = QFile{ QDir{ config_dir }.filePath(filename) };
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    file.write(contents.toUtf8());
}

// Writes the record a running client leaves in its config dir.
// The interface and path are arguments because a caller has to honour what it finds there,
// not assume its own.
void claimConfigDir(
    QString const& config_dir,
    QString const& bus_name,
    QString const& interface = InterfaceName,
    QString const& path = ObjectPath)
{
    // Through the writer a real client uses, so a change to the format cannot leave these
    // cases passing against a spelling nothing writes.
    // writeRawRecord() is for the records below that are meant to be malformed.
    QVERIFY(
        tr::interop::write_dbus_peer_record(
            config_dir.toStdString(),
            { bus_name.toStdString(), interface.toStdString(), path.toStdString() }));
}

// A launch on `config_dir` with one torrent to hand over.
// True means a running instance took it.
[[nodiscard]] bool delegate(QString const& config_dir, QString const& metainfo)
{
    auto metainfos = std::vector<std::string>{ metainfo.toStdString() };
    auto coordinator = tr::interop::StartupCoordinator{ config_dir.toStdString(), tr::interop::make_transport(config_dir) };

    return coordinator
        .delegate(
            tr::interop::Intent::AddTorrents,
            [metainfos = std::move(metainfos)]() mutable { return std::move(metainfos); })
        .has_value();
}

void expectDelegated(QString const& config_dir, FakeClientBase const& client, QString const& metainfo)
{
    QVERIFY(delegate(config_dir, metainfo));
    QCOMPARE(client.received(), QStringList{ metainfo });
}

} // namespace

// A launch delegates its torrents to a client already running --
// but only to the one running on the config dir it was launched with.
// Clients on other config dirs share none of their state,
// so a torrent handed to the wrong one lands in a session the user was not looking at.
class DBusInteropTest : public QObject
{
    Q_OBJECT

private slots:
    static void initTestCase()
    {
        if (!QDBusConnection::sessionBus().isConnected())
        {
            QTest::qSkip("no session bus", __FILE__, __LINE__);
            return;
        }

        // On a shared bus a real client would receive this test's torrents
        // (and one case would fail for owning the well-known name).
        // Only a private dbus-run-session bus can host what follows.
        if (auto const* const iface = QDBusConnection::sessionBus().interface();
            iface != nullptr && QDBusReply<bool>{ iface->isServiceRegistered(ServiceName) }.value())
        {
            QTest::qSkip("another Transmission owns this bus", __FILE__, __LINE__);
            return;
        }
    }

    static void finds_the_client_that_claimed_our_config_dir()
    {
        auto const dir = QTemporaryDir{};
        auto client = FakeClient{ dir.path() };
        auto const thread = ClientThread{ QStringLiteral("claimed"), &client, false };
        claimConfigDir(dir.path(), thread.uniqueName());

        expectDelegated(dir.path(), client, QStringLiteral("magnet:?xt=1"));
    }

    // The bug this all exists to stop. A client on someone else's config dir
    // owns the well-known name and takes delivery of a torrent meant elsewhere.
    static void does_not_deliver_to_a_client_on_another_config_dir()
    {
        auto const theirs = QTemporaryDir{};
        auto const ours = QTemporaryDir{};
        auto client = FakeClient{ theirs.path() };
        auto const thread = ClientThread{ QStringLiteral("theirs"), &client, true };

        QVERIFY(!delegate(ours.path(), QStringLiteral("magnet:?xt=stray")));
        QVERIFY(client.received().isEmpty());
    }

    // ...and the other half. We still reach our own client
    // even though a client on another config dir got to the well-known name first.
    static void reaches_our_client_when_another_owns_the_well_known_name()
    {
        auto const theirs = QTemporaryDir{};
        auto const ours = QTemporaryDir{};

        auto their_client = FakeClient{ theirs.path() };
        auto const their_thread = ClientThread{ QStringLiteral("theirs2"), &their_client, true };

        auto our_client = FakeClient{ ours.path() };
        auto const our_thread = ClientThread{ QStringLiteral("ours2"), &our_client, false };
        claimConfigDir(ours.path(), our_thread.uniqueName());

        expectDelegated(ours.path(), our_client, QStringLiteral("magnet:?xt=2"));
        QVERIFY(their_client.received().isEmpty());
    }

    // Two names for one dir are one config dir, so the client on it is found through either.
    // Starting a second client on a dir the first is already writing
    // is the outcome all of this exists to avoid.
    static void finds_the_client_through_a_symlink_to_its_config_dir()
    {
        auto const dir = QTemporaryDir{};
        auto const link = QTemporaryDir{};
        auto const link_path = QDir{ link.path() }.absoluteFilePath(QStringLiteral("link"));
        QVERIFY(QFile::link(dir.path(), link_path));

        auto client = FakeClient{ dir.path() };
        auto const thread = ClientThread{ QStringLiteral("symlinked"), &client, false };
        claimConfigDir(dir.path(), thread.uniqueName());

        expectDelegated(link_path, client, QStringLiteral("magnet:?xt=4"));
    }

    // ConfigDir() is required to answer canonically, but a client built from other sources may not.
    // Reaching it anyway keeps one end's normalisation from deciding whether the two agree.
    static void finds_a_client_that_answers_with_an_unresolved_path()
    {
        auto const dir = QTemporaryDir{};
        auto const link = QTemporaryDir{};
        auto const link_path = QDir{ link.path() }.absoluteFilePath(QStringLiteral("link"));
        QVERIFY(QFile::link(dir.path(), link_path));

        // the peer answers with the symlink, the caller asks about the real dir
        auto client = ForeignFakeClient{ link_path };
        auto const thread = ClientThread{ QStringLiteral("unresolved"), &client, false, ForeignObjectPath };
        claimConfigDir(dir.path(), thread.uniqueName(), ForeignInterfaceName, ForeignObjectPath);

        expectDelegated(dir.path(), client, QStringLiteral("magnet:?xt=7"));
    }

    // The record says which interface to speak,
    // so a client built from other sources is reachable
    // without either side implementing a name in the other's namespace.
    // That is how separate projects hand torrents to whichever of them is running on the config dir.
    static void delegates_to_a_client_that_names_its_own_interface()
    {
        auto const dir = QTemporaryDir{};
        auto client = ForeignFakeClient{ dir.path() };
        auto const thread = ClientThread{ QStringLiteral("foreign"), &client, false, ForeignObjectPath };
        claimConfigDir(dir.path(), thread.uniqueName(), ForeignInterfaceName, ForeignObjectPath);

        expectDelegated(dir.path(), client, QStringLiteral("magnet:?xt=5"));

        // The peer answers its own interface and no other,
        // so reaching it required honouring the recorded one rather than assuming ours.
        auto const request = QDBusMessage::createMethodCall(
            thread.uniqueName(),
            ForeignObjectPath,
            InterfaceName,
            QStringLiteral(TR_INTEROP_METHOD_CONFIG_DIR));
        QVERIFY(!QDBusReply<QString>{ QDBusConnection::sessionBus().call(request) }.isValid());
    }

    // A record that cannot be understood is not a client,
    // and must not be turned into a call to whatever it half-says.
    static void ignores_a_record_it_cannot_read()
    {
        auto const dir = QTemporaryDir{};
        auto client = FakeClient{ dir.path() };
        auto const thread = ClientThread{ QStringLiteral("unreadable"), &client, false };

        for (auto const& contents :
             { QStringLiteral("bus-name=%1\n").arg(thread.uniqueName()),
               QStringLiteral("interface=%1\npath=%2\n").arg(InterfaceName, ObjectPath),
               QStringLiteral("bus-name=%1\ninterface=nodots\npath=%2\n").arg(thread.uniqueName(), ObjectPath),
               QStringLiteral("bus-name=%1\ninterface=%2\npath=noslash\n").arg(thread.uniqueName(), InterfaceName),
               QStringLiteral("gibberish\n") })
        {
            writeRawRecord(dir.path(), contents);
            QVERIFY2(!delegate(dir.path(), QStringLiteral("magnet:?xt=lost")), qPrintable(contents));
        }

        QVERIFY(client.received().isEmpty());
    }

    // A half-written record, from a client killed mid-write, must not strand the caller.
    // Unreadable means "nothing recorded", so we still have the well-known name to try,
    // and we still check that answer against our config dir.
    static void falls_back_to_the_well_known_name_when_the_record_is_unreadable()
    {
        auto const dir = QTemporaryDir{};
        auto client = FakeClient{ dir.path() };
        auto const thread = ClientThread{ QStringLiteral("halfwritten"), &client, true };
        writeRawRecord(dir.path(), QStringLiteral("bus-name=%1\n").arg(thread.uniqueName()));

        expectDelegated(dir.path(), client, QStringLiteral("magnet:?xt=6"));
    }

    // The record outlives the client that wrote it,
    // so a name that answers nothing has to read as "no client", not as one worth waiting on.
    static void starts_normally_when_the_claiming_client_is_gone()
    {
        auto const dir = QTemporaryDir{};
        claimConfigDir(dir.path(), QStringLiteral(":1.99999"));

        auto transport = tr::interop::make_transport(dir.path());
        QVERIFY(transport->find_other_instance() == nullptr);
    }

    // The peer's death has to be told apart from its silence. A launch mid-handoff
    // starts over a dead peer, but must not start over one that merely stopped
    // answering, whose session still holds the config dir.
    static void reports_a_peer_that_exited_as_gone()
    {
        auto const dir = QTemporaryDir{};
        auto instance = std::unique_ptr<tr::interop::Instance>{};
        auto dead_name = QString{};

        {
            auto client = FakeClient{ dir.path() };
            auto const thread = ClientThread{ QStringLiteral("dying"), &client, false };
            claimConfigDir(dir.path(), thread.uniqueName());
            instance = tr::interop::make_transport(dir.path())->find_other_instance();
            QVERIFY(instance != nullptr);
            dead_name = thread.uniqueName();
        }

        // The daemon processes the client's hangup on its own schedule. Wait until it
        // has, or the call below can be routed to the half-closed connection and
        // answered NoReply rather than ServiceUnknown.
        auto const* const iface = QDBusConnection::sessionBus().interface();
        QVERIFY(iface != nullptr);
        QVERIFY(QTest::qWaitFor(
            [iface, &dead_name]() { return !QDBusReply<bool>{ iface->isServiceRegistered(dead_name) }.value(); },
            5000));

        // The client's connection closed with its thread, so its unique name has no
        // owner. The bus itself reports that, whatever the client was.
        QVERIFY(instance->add_metainfo("magnet:?xt=gone") == tr::interop::Reply::Gone);
    }

    // Unique names are reused after a session-bus restart.
    // A stale record can therefore name a live process whose object has nothing to do with Transmission;
    // its D-Bus identity errors must not consume this launch.
    static void ignores_a_stale_record_whose_bus_name_was_reused()
    {
        auto const dir = QTemporaryDir{};
        auto unrelated = ForeignFakeClient{ dir.path() };
        auto const thread = ClientThread{ QStringLiteral("reused"), &unrelated, false, ForeignObjectPath };
        claimConfigDir(dir.path(), thread.uniqueName());

        QVERIFY(!delegate(dir.path(), QStringLiteral("magnet:?xt=stale")));
        QVERIFY(unrelated.received().isEmpty());
    }

    // A client that predates ConfigDir() cannot say which dir it serves, and here it is the
    // only one a caller can reach. Delegating to it keeps an upgrade from opening a second
    // window on the config dir that client is most likely already using.
    static void delegates_to_a_client_too_old_to_place_itself()
    {
        auto const dir = QTemporaryDir{};
        auto client = UnplaceableFakeClient{};
        auto const thread = ClientThread{ QStringLiteral("old"), &client, true };

        expectDelegated(dir.path(), client, QStringLiteral("magnet:?xt=3"));
    }

    // find_other_instance() must never return this process, even when this process owns
    // the well-known name and the record. That is the transport contract.
    static void never_finds_this_process()
    {
        auto const dir = QTemporaryDir{};
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(ServiceName));
        claimConfigDir(dir.path(), bus.baseService());

        auto transport = tr::interop::make_transport(dir.path());
        QVERIFY(transport->find_other_instance() == nullptr);

        QVERIFY(bus.unregisterService(ServiceName));
    }

    // These publish on this process's own connection, so they run last.
    // The well-known name they take would otherwise answer for the cases above.

    static void publishing_records_how_to_call_this_client()
    {
        auto const dir = QTemporaryDir{};
        auto instance = RecordingInstance{ dir.path().toStdString() };
        auto transport = tr::interop::make_transport(dir.path());
        transport->publish(instance);

        auto const record = tr::interop::read_dbus_peer_record(dir.path().toStdString());
        QVERIFY(record.has_value());
        QCOMPARE(QString::fromStdString(record->bus_name), QDBusConnection::sessionBus().baseService());
        QCOMPARE(QString::fromStdString(record->interface), InterfaceName);
        QCOMPARE(QString::fromStdString(record->path), ObjectPath);

        QVERIFY(QDBusConnection::sessionBus().unregisterService(ServiceName));
    }

    // The other half of delegates_to_a_client_that_names_its_own_interface():
    // there, a client of this build calls names it did not choose;
    // here, a caller reaches this build knowing nothing but the record.
    // Between them a separately-built client and this one can hand off either way.
    static void a_caller_knowing_only_the_record_reaches_this_client()
    {
        auto const dir = QTemporaryDir{};
        auto instance = RecordingInstance{ dir.path().toStdString() };
        auto transport = tr::interop::make_transport(dir.path());
        transport->publish(instance);

        auto const record = tr::interop::read_dbus_peer_record(dir.path().toStdString());
        QVERIFY(record.has_value());
        auto const service = QString::fromStdString(record->bus_name);
        auto const interface = QString::fromStdString(record->interface);
        auto const path = QString::fromStdString(record->path);

        // A launcher calls from its own process, so call from another connection --
        // and another thread, so this one keeps dispatching.
        auto answered = std::atomic<bool>{ false };
        auto accepted = std::atomic<bool>{ false };
        auto caller = std::thread{ [&service, &interface, &path, &answered, &accepted]()
                                   {
                                       auto const name = QStringLiteral("publish-caller");
                                       auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, name);
                                       auto request = QDBusMessage::createMethodCall(
                                           service,
                                           path,
                                           interface,
                                           QStringLiteral(TR_INTEROP_METHOD_ADD_METAINFO));
                                       request.setArguments(QVariantList{} << QStringLiteral("magnet:?xt=published"));
                                       accepted = QDBusReply<bool>{ bus.call(request) }.value();
                                       answered = true;
                                       QDBusConnection::disconnectFromBus(name);
                                   } };

        // Join before the first verify. A failed verify returns out of the test with `caller`
        // still joinable, and destroying a joinable thread calls std::terminate, so we would
        // get a crash instead of the report.
        // The wait keeps this thread dispatching so the service can answer.
        // A timed-out call ends on its own, so the join is bounded.
        auto const answered_in_time = QTest::qWaitFor([&answered]() { return answered.load(); }, 5000);
        caller.join();
        QVERIFY(answered_in_time);

        QVERIFY(accepted.load());
        QCOMPARE(instance.adds, std::vector<std::string>{ "magnet:?xt=published" });

        // InteropObject.h promises the call arrives on the GUI thread, where the window lives.
        QCOMPARE(instance.called_on, QThread::currentThread());

        QVERIFY(QDBusConnection::sessionBus().unregisterService(ServiceName));
    }

    // An old launcher predates the record and calls the names its release was built with,
    // the well-known name, this interface, this path. Third-party scripts do the same.
    // Publishing has to keep those names answering, or an upgrade would strand every
    // older launcher on the machine.
    static void an_old_launcher_reaches_this_client_through_the_shared_names()
    {
        auto const dir = QTemporaryDir{};
        auto instance = RecordingInstance{ dir.path().toStdString() };
        auto transport = tr::interop::make_transport(dir.path());
        transport->publish(instance);

        auto answered = std::atomic<bool>{ false };
        auto accepted = std::atomic<bool>{ false };
        auto presented = std::atomic<bool>{ false };
        auto caller = std::thread{ [&answered, &accepted, &presented]()
                                   {
                                       auto const name = QStringLiteral("old-launcher");
                                       auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, name);

                                       auto add = QDBusMessage::createMethodCall(
                                           ServiceName,
                                           ObjectPath,
                                           InterfaceName,
                                           QStringLiteral(TR_INTEROP_METHOD_ADD_METAINFO));
                                       add.setArguments(QVariantList{} << QStringLiteral("magnet:?xt=old"));
                                       accepted = QDBusReply<bool>{ bus.call(add) }.value();

                                       auto present = QDBusMessage::createMethodCall(
                                           ServiceName,
                                           ObjectPath,
                                           InterfaceName,
                                           QStringLiteral(TR_INTEROP_METHOD_PRESENT_WINDOW));
                                       presented = QDBusReply<bool>{ bus.call(present) }.value();

                                       answered = true;
                                       QDBusConnection::disconnectFromBus(name);
                                   } };

        // Join before the first verify, as in the record-caller case above.
        auto const answered_in_time = QTest::qWaitFor([&answered]() { return answered.load(); }, 5000);
        caller.join();
        QVERIFY(answered_in_time);

        QVERIFY(accepted.load());
        QVERIFY(presented.load());
        QCOMPARE(instance.adds, std::vector<std::string>{ "magnet:?xt=old" });
        QCOMPARE(instance.presents, 1);

        QVERIFY(QDBusConnection::sessionBus().unregisterService(ServiceName));
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = DBusInteropTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "dbus-interop-test.moc"
