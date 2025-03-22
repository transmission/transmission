// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// _AppIndicatorClass::{fallback,unfallback} use deprecated GtkStatusIcon
#undef GTK_DISABLE_DEPRECATED
// We're using deprecated Gtk::StatusItem ourselves as well
#undef GTKMM_DISABLE_DEPRECATED

#include "SystemTrayIcon.h"

#include "Actions.h"
#include "GtkCompat.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#define TR_SYS_TRAY_IMPL_STATUS_NOTIFIER_ITEM
#elif defined(HAVE_APPINDICATOR)
#define TR_SYS_TRAY_IMPL_APPINDICATOR
#else
#define TR_SYS_TRAY_IMPL_STATUS_ICON
#endif

#include <giomm/menu.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/ustring.h>
#include <gtkmm/icontheme.h>

#if defined(TR_SYS_TRAY_IMPL_STATUS_NOTIFIER_ITEM)
#include <giomm/dbusconnection.h>
#include <giomm/dbusobjectskeleton.h>
#include <giomm/dbusownname.h>
#include <giomm/dbusproxy.h>
#include <giomm/menuattributeiter.h>
#include <gtkmm/popovermenu.h>
#endif

#if defined(TR_SYS_TRAY_IMPL_APPINDICATOR) || defined(TR_SYS_TRAY_IMPL_STATUS_ICON)
#include <gtkmm/menu.h>
#endif

#ifdef TR_SYS_TRAY_IMPL_STATUS_ICON
#include <gtkmm/statusicon.h>
#endif

#include <fmt/ranges.h>

#include <stack>
#include <string>

#ifdef TR_SYS_TRAY_IMPL_APPINDICATOR
#ifdef APPINDICATOR_IS_AYATANA
#include <libayatana-appindicator/app-indicator.h>
#else
#include <libappindicator/app-indicator.h>
#endif
#endif

using namespace std::literals;
using namespace libtransmission::Values;

namespace
{

char const* const TrayIconName = "transmission-tray-icon";
char const* const AppIconName = "transmission";

#if defined(TR_SYS_TRAY_IMPL_APPINDICATOR)
char const* const AppName = "transmission-gtk";
#endif

#if defined(TR_SYS_TRAY_IMPL_STATUS_NOTIFIER_ITEM)

using BoolValueType = Glib::Variant<bool>;
using Int32ValueType = Glib::Variant<gint32>;
using UInt32ValueType = Glib::Variant<guint32>;
using StringValueType = Glib::Variant<Glib::ustring>;
using ObjectPathValueType = Glib::Variant<Glib::DBusObjectPathString>;
using ToolTipValueType = Glib::Variant<
    std::tuple<Glib::ustring, std::vector<std::tuple<gint32, gint32, std::vector<guchar>>>, Glib::ustring, Glib::ustring>>;

char const* const ItemPath = "/StatusNotifierItem";
char const* const MenuPath = "/StatusNotifierItem/menu";

template<typename... Ts>
Glib::VariantContainerBase make_variant_tuple(Ts&&... args)
{
    return Glib::VariantContainerBase::create_tuple(
        { Glib::Variant<std::remove_cv_t<std::remove_reference_t<Ts>>>::create(std::forward<Ts>(args))... });
}

Glib::ustring get_resource_as_string(std::string const& rel_path)
{
    if (auto const bytes = Gio::Resource::lookup_data_global(gtr_get_full_resource_path(rel_path)); bytes != nullptr)
    {
        auto size = gsize{};
        if (auto const* const data = bytes->get_data(size); data != nullptr)
        {
            return { static_cast<char const*>(data), size };
        }
    }

    return {};
}

Glib::ustring get_action_name(Gio::MenuModel const& model, int index)
{
    if (auto const attr = model.get_item_attribute(index, Gio::Menu::Attribute::ACTION, StringValueType::variant_type()); attr)
    {
        auto action_name = attr.get_dynamic<StringValueType::CppType>();
        if (auto const dot_pos = action_name.find('.'); dot_pos != Glib::ustring::npos)
        {
            action_name.erase(0, dot_pos + 1);
        }

        return action_name;
    }

    return {};
}

#endif

} // namespace

class SystemTrayIcon::Impl
{
public:
    Impl(Gtk::Window& main_window, Glib::RefPtr<Session> const& core, Glib::RefPtr<Gio::Menu> const& menu);
    ~Impl();

    void refresh();

private:
#if defined(TR_SYS_TRAY_IMPL_STATUS_NOTIFIER_ITEM)
    void on_bus_acquired(Glib::RefPtr<Gio::DBus::Connection> const& connection, Glib::ustring const& name);
    void on_name_acquired(Glib::RefPtr<Gio::DBus::Connection> const& connection, Glib::ustring const& name);
    void on_name_lost(Glib::RefPtr<Gio::DBus::Connection> const& connection, Glib::ustring const& name);
    void on_watcher_created(Glib::RefPtr<Gio::AsyncResult>& result);
    void on_item_registered(Glib::RefPtr<Gio::AsyncResult>& result);
    void on_item_method_call(
        Glib::RefPtr<Gio::DBus::Connection> const& connection,
        Glib::ustring const& sender,
        Glib::ustring const& object_path,
        Glib::ustring const& interface_name,
        Glib::ustring const& method_name,
        Glib::VariantContainerBase const& parameters,
        Glib::RefPtr<Gio::DBus::MethodInvocation> const& invocation);
    void on_item_get_property(
        Glib::VariantBase& property,
        Glib::RefPtr<Gio::DBus::Connection> const& connection,
        Glib::ustring const& sender,
        Glib::ustring const& object_path,
        Glib::ustring const& interface_name,
        Glib::ustring const& property_name);
    void on_menu_method_call(
        Glib::RefPtr<Gio::DBus::Connection> const& connection,
        Glib::ustring const& sender,
        Glib::ustring const& object_path,
        Glib::ustring const& interface_name,
        Glib::ustring const& method_name,
        Glib::VariantContainerBase const& parameters,
        Glib::RefPtr<Gio::DBus::MethodInvocation> const& invocation);
    template<typename T>
    void for_each_menu_item(T const& callback) const;
    static std::map<Glib::ustring, Glib::VariantBase> get_menu_item_props(Glib::RefPtr<Gio::MenuModel> const& model, int index);
#endif

#if defined(TR_SYS_TRAY_IMPL_STATUS_ICON)
    void activated();
    void popup(guint button, guint when);
#endif

    [[nodiscard]] std::string make_tooltip_text() const;

private:
    Glib::RefPtr<Session> const core_;
    Glib::RefPtr<Gio::Menu> const menu_;

#if defined(TR_SYS_TRAY_IMPL_APPINDICATOR) || defined(TR_SYS_TRAY_IMPL_STATUS_ICON)
    std::unique_ptr<Gtk::Menu> gtk_menu_;
#endif

#if defined(TR_SYS_TRAY_IMPL_STATUS_NOTIFIER_ITEM)
    Glib::ustring name_;
    guint name_id_ = 0;
    guint item_id_ = 0;
    guint menu_id_ = 0;
    guint32 menu_revision_ = 0;
    Glib::RefPtr<Gio::DBus::Connection> connection_;
    Glib::RefPtr<Gio::DBus::Proxy> status_notifier_watcher_;
    std::map<Glib::ustring, Glib::VariantBase> properties_;
    std::unique_ptr<Gtk::PopoverMenu> popup_menu_;

    Glib::RefPtr<Gio::DBus::InterfaceInfo> item_iface_;
    Gio::DBus::InterfaceVTable const item_vtable_{
        sigc::mem_fun(*this, &Impl::on_item_method_call),
        sigc::mem_fun(*this, &Impl::on_item_get_property),
    };
    Glib::RefPtr<Gio::DBus::InterfaceInfo> menu_iface_;
    Gio::DBus::InterfaceVTable const menu_vtable_{
        sigc::mem_fun(*this, &Impl::on_menu_method_call),
    };
#elif defined(TR_SYS_TRAY_IMPL_APPINDICATOR)
    std::unique_ptr<AppIndicator, decltype(&g_object_unref)> indicator_ = { nullptr, &g_object_unref };
#elif defined(TR_SYS_TRAY_IMPL_STATUS_ICON)
    Glib::RefPtr<Gtk::StatusIcon> icon_;
#endif
};

#if defined(TR_SYS_TRAY_IMPL_STATUS_NOTIFIER_ITEM)

SystemTrayIcon::Impl::~Impl()
{
    if (connection_ != nullptr)
    {
        if (menu_id_ != 0)
        {
            connection_->unregister_object(menu_id_);
        }

        if (item_id_ != 0)
        {
            connection_->unregister_object(item_id_);
        }
    }

    if (name_id_ != 0)
    {
        Gio::DBus::unown_name(name_id_);
    }
}

void SystemTrayIcon::Impl::refresh()
{
    // fmt::print("refresh: {}\n", fmt::ptr(connection_.get()));

    if (connection_ != nullptr)
    {
        properties_["ToolTip"] = ToolTipValueType::create({ "", {}, "Transmission", make_tooltip_text() });
        connection_->emit_signal(ItemPath, "org.kde.StatusNotifierItem", "NewToolTip");
    }
}

#elif defined(TR_SYS_TRAY_IMPL_APPINDICATOR)

SystemTrayIcon::Impl::~Impl() = default;

void SystemTrayIcon::Impl::refresh()
{
}

#elif defined(TR_SYS_TRAY_IMPL_STATUS_ICON)

SystemTrayIcon::Impl::~Impl() = default;

void SystemTrayIcon::Impl::activated()
{
    gtr_action_activate("toggle-main-window");
}

void SystemTrayIcon::Impl::popup(guint /*button*/, guint /*when*/)
{
    gtk_menu_->popup_at_pointer(nullptr);
}

void SystemTrayIcon::Impl::refresh()
{
    icon_->set_tooltip_text(make_tooltip_text());
}

#endif

namespace
{

Glib::ustring getIconName()
{
    // if the tray's icon is a 48x48 file, use it.
    // otherwise, use the fallback builtin icon.

    auto const iconTheme = IF_GTKMM4(
        Gtk::IconTheme::get_for_display(Gdk::Display::get_default()),
        Gtk::IconTheme::get_default());
    auto const icon = iconTheme->lookup_icon(TrayIconName, 48);
    return icon ? TrayIconName : AppIconName;
}

} // namespace

SystemTrayIcon::SystemTrayIcon(Gtk::Window& main_window, Glib::RefPtr<Session> const& core)
    : impl_(std::make_unique<Impl>(main_window, core, gtr_action_get_object<Gio::Menu>("icon-popup")))
{
}

SystemTrayIcon::~SystemTrayIcon() = default;

void SystemTrayIcon::refresh()
{
    impl_->refresh();
}

bool SystemTrayIcon::is_available()
{
    return true;
}

std::unique_ptr<SystemTrayIcon> SystemTrayIcon::create(Gtk::Window& main_window, Glib::RefPtr<Session> const& core)
{
    return is_available() ? std::make_unique<SystemTrayIcon>(main_window, core) : nullptr;
}

SystemTrayIcon::Impl::Impl(
    [[maybe_unused]] Gtk::Window& main_window,
    Glib::RefPtr<Session> const& core,
    Glib::RefPtr<Gio::Menu> const& menu)
    : core_(core)
    , menu_(menu)
{
    auto const icon_name = getIconName();

#if defined(TR_SYS_TRAY_IMPL_APPINDICATOR) || defined(TR_SYS_TRAY_IMPL_STATUS_ICON)
    gtk_menu_ = std::make_unique<Gtk::Menu>(menu_);
    gtk_menu_->attach_to_widget(main_window);
#endif

#if defined(TR_SYS_TRAY_IMPL_STATUS_NOTIFIER_ITEM)
    name_ = fmt::format("com.transmissionbt.transmission.icon_{}", getpid());

    properties_ = std::map<Glib::ustring, Glib::VariantBase>({
        { "Category", StringValueType::create("ApplicationStatus") },
        { "Id", StringValueType::create("com.transmissionbt.transmission") },
        { "Title", StringValueType::create("Transmission") },
        { "Status", StringValueType::create("Active") },
        { "WindowId", Int32ValueType::create(0) },
        { "IconName", StringValueType::create(getIconName()) },
        { "ToolTip", ToolTipValueType::create({}) },
        { "ItemIsMenu", BoolValueType::create(false) },
        { "Menu", ObjectPathValueType::create(MenuPath) },
    });

    item_iface_ = Gio::DBus::NodeInfo::create_for_xml(get_resource_as_string("org.kde.StatusNotifierItem.xml"))
                      ->lookup_interface();
    menu_iface_ = Gio::DBus::NodeInfo::create_for_xml(get_resource_as_string("com.canonical.dbusmenu.xml"))->lookup_interface();

    name_id_ = Gio::DBus::own_name(
        TR_GIO_DBUS_BUS_TYPE(SESSION),
        name_,
        sigc::mem_fun(*this, &Impl::on_bus_acquired),
        sigc::mem_fun(*this, &Impl::on_name_acquired),
        sigc::mem_fun(*this, &Impl::on_name_lost));
#elif defined(TR_SYS_TRAY_IMPL_APPINDICATOR)
    indicator_.reset(app_indicator_new(AppName, icon_name.c_str(), APP_INDICATOR_CATEGORY_SYSTEM_SERVICES));
    app_indicator_set_status(indicator_.get(), APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(indicator_.get(), Glib::unwrap(gtk_menu_.get()));
    app_indicator_set_title(indicator_.get(), Glib::get_application_name().c_str());
#elif defined(TR_SYS_TRAY_IMPL_STATUS_ICON)
    icon_ = Gtk::StatusIcon::create(icon_name);
    icon_->signal_activate().connect(sigc::mem_fun(*this, &Impl::activated));
    icon_->signal_popup_menu().connect(sigc::mem_fun(*this, &Impl::popup));
#endif
}

#if defined(TR_SYS_TRAY_IMPL_STATUS_NOTIFIER_ITEM)

void SystemTrayIcon::Impl::on_bus_acquired(Glib::RefPtr<Gio::DBus::Connection> const& connection, Glib::ustring const& name)
{
    fmt::print("on_bus_acquired: '{}'\n", name);

    connection_ = connection;
    item_id_ = connection_->register_object(ItemPath, item_iface_, item_vtable_);
    menu_id_ = connection_->register_object(MenuPath, menu_iface_, menu_vtable_);
}

void SystemTrayIcon::Impl::on_name_acquired(
    Glib::RefPtr<Gio::DBus::Connection> const& /*connection*/,
    Glib::ustring const& name)
{
    fmt::print("on_name_acquired: '{}'\n", name);

    Gio::DBus::Proxy::create(
        connection_,
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        sigc::mem_fun(*this, &Impl::on_watcher_created),
        {},
        TR_GIO_DBUS_PROXY_FLAGS(DO_NOT_LOAD_PROPERTIES));
}

void SystemTrayIcon::Impl::on_name_lost(Glib::RefPtr<Gio::DBus::Connection> const& /*connection*/, Glib::ustring const& name)
{
    fmt::print("on_name_lost: '{}'\n", name);
}

void SystemTrayIcon::Impl::on_watcher_created(Glib::RefPtr<Gio::AsyncResult>& result)
{
    try
    {
        status_notifier_watcher_ = Gio::DBus::Proxy::create_finish(result);
    }
    catch (Glib::Error const& e)
    {
        fmt::print("on_watcher_created: error: {}\n", e.what());
        return;
    }

    fmt::print("on_watcher_created: {}\n", fmt::ptr(status_notifier_watcher_.get()));

    status_notifier_watcher_
        ->call("RegisterStatusNotifierItem", sigc::mem_fun(*this, &Impl::on_item_registered), make_variant_tuple(name_), 5000);
}

void SystemTrayIcon::Impl::on_item_registered(Glib::RefPtr<Gio::AsyncResult>& result)
{
    try
    {
        status_notifier_watcher_->call_finish(result);
    }
    catch (Glib::Error const& e)
    {
        fmt::print("on_item_registered: error: {}\n", e.what());
        return;
    }

    fmt::print("on_item_registered: {}\n", fmt::ptr(status_notifier_watcher_.get()));
}

void SystemTrayIcon::Impl::on_item_method_call(
    Glib::RefPtr<Gio::DBus::Connection> const& /*connection*/,
    Glib::ustring const& /*sender*/,
    Glib::ustring const& /*object_path*/,
    Glib::ustring const& /*interface_name*/,
    Glib::ustring const& method_name,
    Glib::VariantContainerBase const& parameters,
    Glib::RefPtr<Gio::DBus::MethodInvocation> const& invocation)
{
    fmt::print("on_item_method_call: {:?} {}", method_name, parameters.print());
    auto result = Glib::VariantContainerBase();

    if (method_name == "Activate")
    {
        gtr_action_activate("toggle-main-window");
    }
    else if (method_name == "ContextMenu")
    {
        auto const x = parameters.get_child(0).get_dynamic<gint32>();
        auto const y = parameters.get_child(1).get_dynamic<gint32>();

        if (popup_menu_ == nullptr)
        {
            popup_menu_ = std::make_unique<Gtk::PopoverMenu>(menu_);
        }

        popup_menu_->set_pointing_to({ x, y, 1, 1 });
        popup_menu_->popup();
    }
    else
    {
        invocation->return_error(Glib::QueryQuark(G_DBUS_ERROR), G_DBUS_ERROR_UNKNOWN_METHOD, "");
        fmt::print(" -> error\n");
        return;
    }

    fmt::print(" -> {}\n", result.gobj() != nullptr ? result.print() : "void");
    invocation->return_value(result);
}

void SystemTrayIcon::Impl::on_item_get_property(
    Glib::VariantBase& property,
    Glib::RefPtr<Gio::DBus::Connection> const& /*connection*/,
    Glib::ustring const& /*sender*/,
    Glib::ustring const& /*object_path*/,
    Glib::ustring const& /*interface_name*/,
    Glib::ustring const& property_name)
{
    fmt::print("on_item_get_property: {:?}", property_name);

    if (auto const property_it = properties_.find(property_name); property_it != properties_.end())
    {
        property = property_it->second;
    }
    else if (auto const property_info = item_iface_->lookup_property(property_name); property_info != nullptr)
    {
        property = Glib::VariantBase(g_variant_new_maybe(Glib::VariantType(property_info->gobj()->signature).gobj(), nullptr));
    }

    fmt::print(" -> {}\n", property.print());
}

template<typename T>
void SystemTrayIcon::Impl::for_each_menu_item(T const& callback) const
{
    auto models = std::stack<Glib::RefPtr<Gio::MenuModel>>({ menu_ });
    auto indices = std::stack<int>({ 0 });
    auto item_index = gint32{ 0 };
    bool last_was_section = false;

    while (!models.empty())
    {
        auto const& model = models.top();
        auto& index = indices.top();

        if (index >= model->get_n_items())
        {
            models.pop();
            indices.pop();
            last_was_section = true;
            continue;
        }

        if (auto const section = model->get_item_link(index, Gio::MenuModel::Link::SECTION); section != nullptr)
        {
            ++index;
            models.push(section);
            indices.push(0);
            last_was_section = true;
        }
        else
        {
            if (last_was_section && item_index > 0)
            {
                if (!callback(++item_index, nullptr, -1))
                {
                    break;
                }
            }

            last_was_section = false;

            if (!callback(++item_index, model, index++))
            {
                break;
            }
        }
    }
}

void SystemTrayIcon::Impl::on_menu_method_call(
    Glib::RefPtr<Gio::DBus::Connection> const& /*connection*/,
    Glib::ustring const& /*sender*/,
    Glib::ustring const& /*object_path*/,
    Glib::ustring const& /*interface_name*/,
    Glib::ustring const& method_name,
    Glib::VariantContainerBase const& parameters,
    Glib::RefPtr<Gio::DBus::MethodInvocation> const& invocation)
{
    fmt::print("on_menu_method_call: {:?} {}", method_name, parameters.print());
    auto result = Glib::VariantContainerBase();

    if (method_name == "GetLayout")
    {
        auto const parent_id = parameters.get_child(0).get_dynamic<gint32>();
        // auto const recursion_depth = parameters.get_child(1).get_dynamic<gint32>();
        // auto const property_names = parameters.get_child(2).get_dynamic<std::vector<Glib::ustring>>();

        using ChildValueType = Glib::Variant<
            std::tuple<gint32, std::map<Glib::ustring, Glib::VariantBase>, std::vector<Glib::VariantBase>>>;

        auto children = std::vector<Glib::VariantBase>();
        if (parent_id == 0)
        {
            for_each_menu_item(
                [&children](gint32 item_index, Glib::RefPtr<Gio::MenuModel> const& model, int index)
                {
                    children.push_back(ChildValueType::create({ item_index, get_menu_item_props(model, index), {} }));
                    return true;
                });
        }

        auto items = ChildValueType::CppContainerType(
            0,
            { { "children-display", StringValueType::create("submenu") } },
            children);

        result = make_variant_tuple(menu_revision_, std::move(items));
    }
    else if (method_name == "GetGroupProperties")
    {
        auto const ids = parameters.get_child(0).get_dynamic<std::vector<gint32>>();
        // auto const property_names = parameters.get_child(1).get_dynamic<std::vector<Glib::ustring>>();

        auto items = std::vector<std::tuple<gint32, std::map<Glib::ustring, Glib::VariantBase>>>();

        for_each_menu_item(
            [&ids, &items](gint32 item_index, Glib::RefPtr<Gio::MenuModel> const& model, int index)
            {
                if (std::find(ids.begin(), ids.end(), item_index) != ids.end())
                {
                    items.emplace_back(item_index, get_menu_item_props(model, index));
                }
                return true;
            });

        result = make_variant_tuple(std::move(items));
    }
    else if (method_name == "GetProperty")
    {
        // auto const id = parameters.get_child(0).get_dynamic<gint32>();
        // auto const name = parameters.get_child(1).get_dynamic<Glib::ustring>();

        result = make_variant_tuple(Glib::VariantBase{});
    }
    else if (method_name == "Event")
    {
        auto const idx = parameters.get_child(0).get_dynamic<gint32>();
        auto const event_id = parameters.get_child(1).get_dynamic<Glib::ustring>();
        // auto const data = parameters.get_child(2);
        // auto const timestamp = parameters.get_child(3).get_dynamic<guint32>();

        if (event_id == "clicked")
        {
            auto action_name = Glib::ustring();
            for_each_menu_item(
                [idx, &action_name](gint32 item_index, Glib::RefPtr<Gio::MenuModel> const& model, int index)
                {
                    if (item_index != idx)
                    {
                        return true;
                    }
                    action_name = get_action_name(*model, index);
                    return false;
                });

            if (!action_name.empty())
            {
                gtr_action_activate(action_name);
            }
        }
    }
    else if (method_name == "EventGroup")
    {
        // auto const
        //     events = parameters.get_child(0)
        //                  .get_dynamic<
        //                      std::vector<std::tuple<Int32ValueType, StringValueType, Glib::VariantBase, UInt32ValueType>>>();

        result = make_variant_tuple(std::vector<gint32>{});
    }
    else if (method_name == "AboutToShow")
    {
        auto const parent_id = parameters.get_child(0).get_dynamic<gint32>();

        connection_->emit_signal(
            MenuPath,
            "com.canonical.dbusmenu",
            "LayoutUpdated",
            {},
            make_variant_tuple(++menu_revision_, parent_id));

        result = make_variant_tuple(bool{});
    }
    else if (method_name == "AboutToShowGroup")
    {
        // auto const ids = parameters.get_child(0).get_dynamic<std::vector<gint32>>();

        result = make_variant_tuple(std::vector<gint32>{});
    }
    else
    {
        invocation->return_error(Glib::QueryQuark(G_DBUS_ERROR), G_DBUS_ERROR_UNKNOWN_METHOD, "");
        fmt::print(" -> error\n");
        return;
    }

    fmt::print(" -> {}\n", result.gobj() != nullptr ? result.print() : "void");
    invocation->return_value(result);
}

std::map<Glib::ustring, Glib::VariantBase> SystemTrayIcon::Impl::get_menu_item_props(
    Glib::RefPtr<Gio::MenuModel> const& model,
    int index)
{
    auto props = std::map<Glib::ustring, Glib::VariantBase>();

    if (model == nullptr)
    {
        props["type"] = StringValueType::create("separator");
        return props;
    }

    // props["type"] = StringValueType::create("standard");

    if (auto const attr = model->get_item_attribute(index, Gio::Menu::Attribute::LABEL, StringValueType::variant_type()); attr)
    {
        props["label"] = attr;
    }

    if (auto const attr = model->get_item_attribute(index, Gio::Menu::Attribute::ICON, StringValueType::variant_type()); attr)
    {
        props["icon-name"] = attr;
    }

    if (auto const action = gtr_action_find(get_action_name(*model, index)); action != nullptr)
    {
        if (!action->get_enabled())
        {
            props["enabled"] = BoolValueType::create(false);
        }

        if (auto const state_type = action->get_state_type(); state_type.gobj() != nullptr)
        {
            bool const is_bool = state_type.equal(BoolValueType::variant_type());
            props["toggle-type"] = StringValueType::create(is_bool ? "checkmark" : "radio");

            auto const state = action->get_state_variant();
            bool const is_active = is_bool ?
                state.get_dynamic<bool>() :
                state.equal(model->get_item_attribute(index, Gio::Menu::Attribute::TARGET, state_type));
            props["toggle-state"] = Int32ValueType::create(is_active ? 1 : 0);
        }
    }

    return props;
}

#endif

std::string SystemTrayIcon::Impl::make_tooltip_text() const
{
    auto const* const session = core_->get_session();
    return fmt::format(
        fmt::runtime(_("{upload_speed} ▲ {download_speed} ▼")),
        fmt::arg("upload_speed", Speed{ tr_sessionGetRawSpeed_KBps(session, TR_UP), Speed::Units::KByps }.to_string()),
        fmt::arg("download_speed", Speed{ tr_sessionGetRawSpeed_KBps(session, TR_DOWN), Speed::Units::KByps }.to_string()));
}
