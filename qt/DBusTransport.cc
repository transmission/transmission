// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusReply>
#include <QString>
#include <QVariant>
#include <QtDebug>

#include <fmt/format.h>

#include <libtransmission-app/dbus-peer-record.h>
#include <libtransmission-app/interop-names.h>

#include "InteropObject.h"
#include "Transports.h"
#include "Utils.h"

namespace
{

class DBusTransport final : public tr::interop::Transport
{
public:
    explicit DBusTransport(QString config_dir);
    ~DBusTransport() override;

    DBusTransport(DBusTransport&&) = delete;
    DBusTransport(DBusTransport const&) = delete;
    DBusTransport& operator=(DBusTransport&&) = delete;
    DBusTransport& operator=(DBusTransport const&) = delete;

    void publish(tr::interop::Instance& self) override;

    [[nodiscard]] std::unique_ptr<tr::interop::Instance> find_other_instance() override;

private:
    QString const config_dir_;
    std::unique_ptr<InteropObject> object_;
};

// NOLINTBEGIN(cert-err58-cpp)
auto const ServiceName = QStringLiteral(TR_INTEROP_DBUS_SERVICE_NAME);
auto const InterfaceName = QStringLiteral(TR_INTEROP_DBUS_INTERFACE_NAME);
auto const ObjectPath = QStringLiteral(TR_INTEROP_DBUS_OBJECT_PATH);
// NOLINTEND(cert-err58-cpp)

class RemoteInstance final : public tr::interop::Instance
{
public:
    // The interface and path are whatever the peer recorded, and need not be this build's own.
    // A client built from other sources names its own interface, and we call whatever it named.
    RemoteInstance(QString service, QString interface, QString path)
        : service_{ std::move(service) }
        , interface_{ std::move(interface) }
        , path_{ std::move(path) }
    {
    }

    [[nodiscard]] tr::interop::Reply present_window() override
    {
        return call_reply(tr::interop::MethodPresentWindow);
    }

    [[nodiscard]] tr::interop::Reply add_metainfo(std::string_view const metainfo) override
    {
        return call_reply(tr::interop::MethodAddMetainfo, QVariantList{} << Utils::toQString(metainfo));
    }

    [[nodiscard]] std::string config_dir() override
    {
        // No answer covers a release too old to have the method, and a peer that has gone.
        // StartupCoordinator treats the empty answer as a match. A peer that has really
        // gone answers the intent call Gone, which is where we find out.
        auto const dir = call<QString>(tr::interop::MethodConfigDir);
        return dir ? dir->toStdString() : std::string{};
    }

    [[nodiscard]] std::string description() const override
    {
        return fmt::format(
            "D-Bus peer {:s} ({:s} {:s})",
            service_.toStdString(),
            interface_.toStdString(),
            path_.toStdString());
    }

private:
    [[nodiscard]] QDBusMessage raw_call(std::string_view const method, QVariantList const& args) const
    {
        auto request = QDBusMessage::createMethodCall(service_, path_, interface_, Utils::toQString(method));
        request.setArguments(args);
        // Keep the default timeout. A client that is merely busy must not look absent.
        return QDBusConnection::sessionBus().call(request);
    }

    // Only a valid reply proves this endpoint implements the method.
    // Identity and signature errors can come from an unrelated process that inherited
    // a reused unique bus name from a stale record.
    template<typename T>
    [[nodiscard]] std::optional<T> call(std::string_view const method, QVariantList const& args = {}) const
    {
        auto const reply = QDBusReply<T>{ raw_call(method, args) };
        return reply.isValid() ? std::optional<T>{ reply.value() } : std::nullopt;
    }

    // A call whose wire answer is a bool, classified for tr::interop.
    // Two errors prove the peer is gone. ServiceUnknown is the bus reporting that nobody
    // owns the name any more, and Disconnected is this process losing the bus. Every
    // other error can also come from a busy client, or from an unrelated process that
    // inherited a reused unique bus name, so it proves nothing.
    [[nodiscard]] tr::interop::Reply call_reply(std::string_view const method, QVariantList const& args = {}) const
    {
        auto const reply = QDBusReply<bool>{ raw_call(method, args) };
        if (reply.isValid())
        {
            return reply.value() ? tr::interop::Reply::Yes : tr::interop::Reply::No;
        }

        switch (reply.error().type())
        {
        case QDBusError::ServiceUnknown:
        case QDBusError::Disconnected:
            return tr::interop::Reply::Gone;

        default:
            return tr::interop::Reply::Unanswered;
        }
    }

    QString const service_;
    QString const interface_;
    QString const path_;
};

} // namespace

DBusTransport::DBusTransport(QString config_dir)
    : config_dir_{ std::move(config_dir) }
{
}

DBusTransport::~DBusTransport() = default;

void DBusTransport::publish(tr::interop::Instance& self)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
    {
        return;
    }

    tr::interop::publish_dbus_peer(
        config_dir_.toStdString(),
        bus.baseService().toStdString(),
        [this, &self, &bus]
        {
            object_ = std::make_unique<InteropObject>(self);
            if (bus.registerObject(ObjectPath, object_.get(), QDBusConnection::ExportAllSlots))
            {
                return true;
            }

            qWarning() << "couldn't register" << qPrintable(ObjectPath);
            return false;
        },
        [&bus] { return bus.registerService(ServiceName); });
}

std::unique_ptr<tr::interop::Instance> DBusTransport::find_other_instance()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
    {
        return {};
    }

    auto const peer = tr::interop::select_dbus_peer(
        config_dir_.toStdString(),
        bus.baseService().toStdString(),
        [&bus](std::string_view const name)
        {
            auto const* const iface = bus.interface();
            return iface != nullptr ? QDBusReply<QString>{ iface->serviceOwner(Utils::toQString(name)) }.value().toStdString() :
                                      std::string{};
        });

    if (!peer)
    {
        return {};
    }

    return std::make_unique<RemoteInstance>(
        QString::fromStdString(peer->bus_name),
        QString::fromStdString(peer->interface),
        QString::fromStdString(peer->path));
}

namespace tr::interop::detail
{

std::unique_ptr<Transport> make_dbus_transport(QString config_dir)
{
    return std::make_unique<DBusTransport>(std::move(config_dir));
}

} // namespace tr::interop::detail
