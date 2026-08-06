// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

#include <QApplication>
#include <QAxObject>
#include <QString>
#include <QTest>
#include <QUuid>

#include <libtransmission-app/interop-names.h>
#include <libtransmission-app/interop.h>

#include "Transports.h"

namespace
{

// The instance a published test client answers for.
class FakeInstance final : public tr::interop::Instance
{
public:
    explicit FakeInstance(std::string config_dir)
        : config_dir_{ std::move(config_dir) }
    {
    }

    [[nodiscard]] tr::interop::Reply present_window() override
    {
        return tr::interop::Reply::Yes;
    }

    [[nodiscard]] tr::interop::Reply add_metainfo(std::string_view const metainfo) override
    {
        adds_.emplace_back(metainfo);
        return tr::interop::Reply::Yes;
    }

    [[nodiscard]] std::string config_dir() override
    {
        return config_dir_;
    }

    [[nodiscard]] std::string description() const override
    {
        return "com-interop-test instance";
    }

    [[nodiscard]] std::vector<std::string> const& adds() const noexcept
    {
        return adds_;
    }

private:
    std::string const config_dir_;
    std::vector<std::string> adds_;
};

} // namespace

// The COM transport answers one question. Is a Qt client already running, and if so,
// how do I reach it? A transport that starts a client to answer
// leaves a stray process holding a torrent the user cannot see.
//
class ComInteropTest : public QObject
{
    Q_OBJECT

private slots:
    static void does_not_start_a_client_to_answer_the_query()
    {
        // Asking twice catches a lookup that answers itself by starting a client.
        // The second transport would find what the first one launched.
        auto first = tr::interop::make_transport(QStringLiteral("C:/one"));
        QVERIFY(first->find_other_instance() == nullptr);

        auto second = tr::interop::make_transport(QStringLiteral("C:/two"));
        QVERIFY(second->find_other_instance() == nullptr);
    }

    static void routes_each_config_dir_to_its_own_running_client()
    {
        auto first_instance = FakeInstance{ "C:/one" };
        auto second_instance = FakeInstance{ "C:/two" };

        auto first_publisher = tr::interop::make_transport(QStringLiteral("C:/one"));
        first_publisher->publish(first_instance);

        auto second_publisher = tr::interop::make_transport(QStringLiteral("C:/two"));
        second_publisher->publish(second_instance);

        auto first = tr::interop::make_transport(QStringLiteral("C:/one"))->find_other_instance();
        QVERIFY(first != nullptr);
        QCOMPARE(first->config_dir(), std::string{ "C:/one" });
        QVERIFY(first->present_window() == tr::interop::Reply::Yes);
        QVERIFY(first->add_metainfo("alpha") == tr::interop::Reply::Yes);

        auto second = tr::interop::make_transport(QStringLiteral("C:/two"))->find_other_instance();
        QVERIFY(second != nullptr);
        QCOMPARE(second->config_dir(), std::string{ "C:/two" });
        QVERIFY(second->add_metainfo("beta") == tr::interop::Reply::Yes);

        QVERIFY(first_instance.adds() == std::vector<std::string>{ "alpha" });
        QVERIFY(second_instance.adds() == std::vector<std::string>{ "beta" });
    }

    // A registration outliving the client it speaks for is a call into a destroyed Instance.
    static void stops_answering_once_the_client_is_gone()
    {
        auto instance = FakeInstance{ "C:/gone" };

        {
            auto publisher = tr::interop::make_transport(QStringLiteral("C:/gone"));
            publisher->publish(instance);
            QVERIFY(tr::interop::make_transport(QStringLiteral("C:/gone"))->find_other_instance() != nullptr);
        }

        QVERIFY(tr::interop::make_transport(QStringLiteral("C:/gone"))->find_other_instance() == nullptr);
    }

    // A client that predates config-specific monikers registers under the class id alone,
    // so a launch reaches it whatever config dir it asked about. Which dir that client
    // actually serves is tr::interop::StartupCoordinator's question, not the transport's.
    static void falls_back_to_the_class_id_when_nobody_claims_this_config_dir()
    {
        auto instance = FakeInstance{ "C:/legacy" };
        auto publisher = tr::interop::make_transport(QStringLiteral("C:/legacy"));
        publisher->publish(instance);

        auto const found = tr::interop::make_transport(QStringLiteral("C:/unclaimed"))->find_other_instance();
        QVERIFY(found != nullptr);
        QCOMPARE(found->config_dir(), std::string{ "C:/legacy" });
    }

    // If a second object registers under a class id that already has one, COM may return
    // either. The documentation promises nothing.
    // So the second client does not register at all. The fallback then stays with whoever
    // was there first, rather than with whichever one a given Windows hands back.
    static void leaves_the_class_id_with_the_first_client_to_take_it()
    {
        auto first_instance = FakeInstance{ "C:/first" };
        auto second_instance = FakeInstance{ "C:/second" };

        auto first_publisher = tr::interop::make_transport(QStringLiteral("C:/first"));
        first_publisher->publish(first_instance);

        auto second_publisher = tr::interop::make_transport(QStringLiteral("C:/second"));
        second_publisher->publish(second_instance);

        auto const found = tr::interop::make_transport(QStringLiteral("C:/neither"))->find_other_instance();
        QVERIFY(found != nullptr);
        QCOMPARE(found->config_dir(), std::string{ "C:/first" });
    }

    // An old launcher predates config monikers. GetActiveObject() with the shared class
    // id and a call by method name are all it does, so publishing has to keep that entry
    // answering, or an upgrade would strand every older launcher on the machine.
    static void an_old_launcher_reaches_this_client_through_the_class_id()
    {
        auto instance = FakeInstance{ "C:/old" };
        auto publisher = tr::interop::make_transport(QStringLiteral("C:/old"));
        publisher->publish(instance);

        auto* unknown = static_cast<IUnknown*>(nullptr);
        QVERIFY(::GetActiveObject(QUuid{ QStringLiteral(TR_INTEROP_COM_CLASS_ID) }, nullptr, &unknown) == S_OK);
        QVERIFY(unknown != nullptr);

        auto client = QAxObject{ unknown };
        unknown->Release();

        QVERIFY(client.dynamicCall("AddMetainfo(QString)", QStringLiteral("magnet:?xt=old")).toBool());
        QVERIFY(client.dynamicCall("PresentWindow()").toBool());
        QVERIFY(instance.adds() == std::vector<std::string>{ "magnet:?xt=old" });
    }
};

int main(int argc, char** argv)
{
    auto const app = QApplication{ argc, argv };

    auto test = ComInteropTest{};
    return QTest::qExec(&test, argc, argv);
}

#include "com-interop-test.moc"
