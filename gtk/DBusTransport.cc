// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Transports.h"
#include "Utils.h"

#include <libtransmission-app/dbus-peer-record.h>
#include <libtransmission-app/interop-names.h>

#include <libtransmission/log.h>

#include <giomm/dbusconnection.h>
#include <giomm/dbuserror.h>
#include <glibmm/error.h>
#include <glibmm/ustring.h>
#include <glibmm/variant.h>

#include <fmt/format.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

class DBusTransport final : public tr::interop::Transport
{
public:
    explicit DBusTransport(std::string config_dir);
    ~DBusTransport() override;

    DBusTransport(DBusTransport&&) = delete;
    DBusTransport(DBusTransport const&) = delete;
    DBusTransport& operator=(DBusTransport&&) = delete;
    DBusTransport& operator=(DBusTransport const&) = delete;

    void publish(tr::interop::Instance& self) override;

    [[nodiscard]] std::unique_ptr<tr::interop::Instance> find_other_instance() override;

private:
    void on_method_call(
        Glib::RefPtr<Gio::DBus::Connection> const& connection,
        Glib::ustring const& sender,
        Glib::ustring const& object_path,
        Glib::ustring const& interface_name,
        Glib::ustring const& method_name,
        Glib::VariantContainerBase const& parameters,
        Glib::RefPtr<Gio::DBus::MethodInvocation> const& invocation);

    std::string const config_dir_;

    tr::interop::Instance* published_ = nullptr;
    Glib::RefPtr<Gio::DBus::Connection> connection_;
    std::unique_ptr<Gio::DBus::InterfaceVTable> vtable_;
    guint registration_id_ = 0U;
};

// NOLINTBEGIN(cert-err58-cpp)
auto const ServiceName = Glib::ustring{ TR_INTEROP_DBUS_SERVICE_NAME };
auto const InterfaceName = Glib::ustring{ TR_INTEROP_DBUS_INTERFACE_NAME };
auto const ObjectPath = Glib::ustring{ TR_INTEROP_DBUS_OBJECT_PATH };

auto const BusDriverName = Glib::ustring{ "org.freedesktop.DBus" };
auto const BusDriverPath = Glib::ustring{ "/org/freedesktop/DBus" };
// NOLINTEND(cert-err58-cpp)

[[nodiscard]] Glib::RefPtr<Gio::DBus::Connection> session_bus()
{
    try
    {
        return Gio::DBus::Connection::get_sync(static_cast<Gio::DBus::BusType>(G_BUS_TYPE_SESSION));
    }
    catch (Glib::Error const&)
    {
        return {};
    }
}

[[nodiscard]] Glib::ustring to_ustring(std::string_view const sv)
{
    return Glib::ustring{ std::string{ sv } };
}

// The first value of a reply, or nothing when the reply is malformed.
// Throws what the call throws.
template<typename T>
[[nodiscard]] std::optional<T> first_value_or_throw(
    Glib::RefPtr<Gio::DBus::Connection> const& connection,
    Glib::ustring const& path,
    Glib::ustring const& interface,
    Glib::ustring const& method,
    Glib::VariantContainerBase const& params,
    Glib::ustring const& service)
{
    auto const result = connection->call_sync(path, interface, method, params, service);

    // The interface and path come from another client's peer record, so nothing constrains them.
    // GDBus refuses a name it considers malformed by returning no reply and setting no error,
    // so check that a reply is there at all before reading it.
    if (result.gobj() == nullptr || result.get_n_children() == 0U)
    {
        return {};
    }

    // An answer of the wrong type means a client that does not speak this interface.
    // Report it as no answer at all.
    auto child = Glib::VariantBase{};
    result.get_child(child, 0U);
    if (!child.is_of_type(Glib::Variant<T>::variant_type()))
    {
        return {};
    }

    return Glib::VariantBase::cast_dynamic<Glib::Variant<T>>(child).get();
}

// The first value of a reply, or nothing if the call failed.
template<typename T>
[[nodiscard]] std::optional<T> first_value(
    Glib::RefPtr<Gio::DBus::Connection> const& connection,
    Glib::ustring const& path,
    Glib::ustring const& interface,
    Glib::ustring const& method,
    Glib::VariantContainerBase const& params,
    Glib::ustring const& service)
{
    try
    {
        return first_value_or_throw<T>(connection, path, interface, method, params, service);
    }
    catch (Glib::Error const&)
    {
        return {};
    }
}

template<typename T, typename... Args>
[[nodiscard]] std::optional<T> bus_driver_call(
    Glib::RefPtr<Gio::DBus::Connection> const& connection,
    Glib::ustring const& method,
    Args const&... args)
{
    return first_value<T>(connection, BusDriverPath, BusDriverName, method, gtr_variant_tuple(args...), BusDriverName);
}

// The unique name owning `name`, or empty when nobody does.
// We ask the bus itself. A recorded name that comes back unowned belongs to a client that
// has exited, and is not worth a method call's timeout.
[[nodiscard]] Glib::ustring name_owner(Glib::RefPtr<Gio::DBus::Connection> const& connection, Glib::ustring const& name)
{
    return bus_driver_call<Glib::ustring>(connection, "GetNameOwner", name).value_or(Glib::ustring{});
}

// True when this connection became the name's primary owner.
[[nodiscard]] bool request_name(Glib::RefPtr<Gio::DBus::Connection> const& connection, Glib::ustring const& name)
{
    auto constexpr PrimaryOwner = guint32{ 1U }; // DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER

    return bus_driver_call<guint32>(connection, "RequestName", name, guint32{ 0U }) == PrimaryOwner;
}

class RemoteInstance final : public tr::interop::Instance
{
public:
    RemoteInstance(
        Glib::RefPtr<Gio::DBus::Connection> connection,
        Glib::ustring service,
        Glib::ustring interface,
        Glib::ustring path)
        : connection_{ std::move(connection) }
        , service_{ std::move(service) }
        , interface_{ std::move(interface) }
        , path_{ std::move(path) }
    {
    }

    [[nodiscard]] tr::interop::Reply present_window() override
    {
        return call_reply(tr::interop::MethodPresentWindow, {});
    }

    [[nodiscard]] tr::interop::Reply add_metainfo(std::string_view const metainfo) override
    {
        return call_reply(tr::interop::MethodAddMetainfo, gtr_variant_tuple(to_ustring(metainfo)));
    }

    [[nodiscard]] std::string config_dir() override
    {
        // No answer covers a release too old to have the method, and a peer that has gone.
        // StartupCoordinator treats the empty answer as a match. A peer that has really
        // gone answers the intent call Gone, which is where we find out.
        auto const dir = call<Glib::ustring>(tr::interop::MethodConfigDir, {});
        return dir ? std::string{ *dir } : std::string{};
    }

    [[nodiscard]] std::string description() const override
    {
        return fmt::format("D-Bus peer {:s} ({:s} {:s})", service_.raw(), interface_.raw(), path_.raw());
    }

private:
    // Only a valid reply proves this endpoint implements the method.
    // Identity and signature errors can come from an unrelated process that inherited
    // a reused unique bus name from a stale record.
    template<typename T>
    [[nodiscard]] std::optional<T> call(std::string_view const method, Glib::VariantContainerBase const& params)
    {
        return first_value<T>(connection_, path_, interface_, to_ustring(method), params, service_);
    }

    // A call whose wire answer is a bool, classified for tr::interop.
    // Two errors prove the peer is gone. SERVICE_UNKNOWN is the bus reporting that
    // nobody owns the name any more, and DISCONNECTED is this process losing the bus.
    // Every other error can also come from a busy client, or from an unrelated process
    // that inherited a reused unique bus name, so it proves nothing.
    [[nodiscard]] tr::interop::Reply call_reply(std::string_view const method, Glib::VariantContainerBase const& params)
    {
        try
        {
            auto const value = first_value_or_throw<bool>(
                connection_,
                path_,
                interface_,
                to_ustring(method),
                params,
                service_);
            if (!value)
            {
                return tr::interop::Reply::Unanswered;
            }

            return *value ? tr::interop::Reply::Yes : tr::interop::Reply::No;
        }
        catch (Gio::DBus::Error const& e)
        {
            switch (e.code())
            {
            case Gio::DBus::Error::SERVICE_UNKNOWN:
            case Gio::DBus::Error::NAME_HAS_NO_OWNER:
            case Gio::DBus::Error::DISCONNECTED:
                return tr::interop::Reply::Gone;

            default:
                return tr::interop::Reply::Unanswered;
            }
        }
        catch (Glib::Error const&)
        {
            return tr::interop::Reply::Unanswered;
        }
    }

    Glib::RefPtr<Gio::DBus::Connection> const connection_;
    Glib::ustring const service_;
    Glib::ustring const interface_;
    Glib::ustring const path_;
};

} // namespace

DBusTransport::DBusTransport(std::string config_dir)
    : config_dir_{ std::move(config_dir) }
{
}

DBusTransport::~DBusTransport()
{
    // Clear this first. A still-registered object would otherwise keep answering calls
    // while we destroy the transport.
    published_ = nullptr;

    if (connection_ && registration_id_ != 0U)
    {
        connection_->unregister_object(registration_id_);
        registration_id_ = 0U;
    }
}

void DBusTransport::on_method_call(
    Glib::RefPtr<Gio::DBus::Connection> const& /*connection*/,
    Glib::ustring const& /*sender*/,
    Glib::ustring const& /*object_path*/,
    Glib::ustring const& /*interface_name*/,
    Glib::ustring const& method_name,
    Glib::VariantContainerBase const& parameters,
    Glib::RefPtr<Gio::DBus::MethodInvocation> const& invocation)
{
    // GDBus dispatches on the main context that registered the object, the GTK mainloop,
    // so the Instance may touch widgets safely.
    auto const reply_bool = [&invocation](bool const value)
    {
        invocation->return_value(Glib::VariantContainerBase::create_tuple(Glib::Variant<bool>::create(value)));
    };

    if (method_name == to_ustring(tr::interop::MethodPresentWindow))
    {
        reply_bool(published_ != nullptr && published_->present_window() == tr::interop::Reply::Yes);
    }
    else if (method_name == to_ustring(tr::interop::MethodAddMetainfo))
    {
        auto metainfo = Glib::Variant<Glib::ustring>{};
        parameters.get_child(metainfo, 0U);
        reply_bool(published_ != nullptr && published_->add_metainfo(metainfo.get().raw()) == tr::interop::Reply::Yes);
    }
    else if (method_name == to_ustring(tr::interop::MethodConfigDir))
    {
        auto const dir = published_ != nullptr ? published_->config_dir() : std::string{};
        invocation->return_value(
            Glib::VariantContainerBase::create_tuple(Glib::Variant<Glib::ustring>::create(to_ustring(dir))));
    }
    else
    {
        invocation->return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", "no such method");
    }
}

void DBusTransport::publish(tr::interop::Instance& self)
{
    auto const connection = session_bus();
    if (!connection)
    {
        return;
    }

    published_ = &self;

    tr::interop::publish_dbus_peer(
        config_dir_,
        connection->get_unique_name().raw(),
        [this, &connection]
        {
            try
            {
                // register_object() takes its own reference on the interface info.
                // Only the vtable has to outlive the registration.
                auto const node_info = Gio::DBus::NodeInfo::create_for_xml(to_ustring(tr::interop::DBusIntrospectionXml));
                vtable_ = std::make_unique<Gio::DBus::InterfaceVTable>(sigc::mem_fun(*this, &DBusTransport::on_method_call));
                registration_id_ = connection->register_object(ObjectPath, node_info->lookup_interface(), *vtable_);
            }
            catch (Glib::Error const& e)
            {
                tr_logAddWarn(fmt::format("couldn't register {:s}: {:s}", ObjectPath.raw(), std::string{ e.what() }));
                return false;
            }

            connection_ = connection;
            return true;
        },
        [&connection] { return request_name(connection, ServiceName); });
}

std::unique_ptr<tr::interop::Instance> DBusTransport::find_other_instance()
{
    auto const connection = session_bus();
    if (!connection)
    {
        return {};
    }

    auto const peer = tr::interop::select_dbus_peer(
        config_dir_,
        connection->get_unique_name().raw(),
        [&connection](std::string_view const name) { return name_owner(connection, to_ustring(name)).raw(); });

    if (!peer)
    {
        return {};
    }

    return std::make_unique<RemoteInstance>(
        connection,
        to_ustring(peer->bus_name),
        to_ustring(peer->interface),
        to_ustring(peer->path));
}

namespace tr::interop
{

std::unique_ptr<Transport> make_transport(std::string_view const config_dir)
{
    return std::make_unique<DBusTransport>(std::string{ config_dir });
}

} // namespace tr::interop
