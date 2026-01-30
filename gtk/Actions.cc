// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Actions.h"

#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/quark.h>

#include <giomm/simpleaction.h>
#include <glibmm/i18n.h>
#include <glibmm/variant.h>

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <giomm/liststore.h>
#include <giomm/menuattributeiter.h>
#include <giomm/menulinkiter.h>
#include <gtkmm/shortcut.h>
#include <gtkmm/shortcutaction.h>
#include <gtkmm/shortcuttrigger.h>

#include <stack>
#include <utility>
#endif

using namespace std::string_view_literals;

using VariantString = Glib::Variant<Glib::ustring>;

namespace
{

Session* myCore = nullptr;

void action_cb(Gio::SimpleAction& action, gpointer user_data)
{
    gtr_actions_handler(action.get_name(), user_data);
}

void sort_changed_cb(Gio::SimpleAction& action, Glib::VariantBase const& value, gpointer /*user_data*/)
{
    action.set_state(value);
    myCore->set_pref(TR_KEY_sort_mode, Glib::VariantBase::cast_dynamic<VariantString>(value).get());
}

constexpr std::array<std::string_view, 2U> const ShowToggleEntries = {
    "toggle-main-window"sv,
    "toggle-message-log"sv,
};

void toggle_pref_cb(Gio::SimpleAction& action, gpointer prefs_key)
{
    bool val = false;
    action.get_state(val);
    val = !val;

    action.set_state(Glib::Variant<bool>::create(val));
    myCore->set_pref(GPOINTER_TO_INT(prefs_key), val);
}

// action-name, prefs_name
constexpr std::array<std::pair<std::string_view, tr_quark>, 6U> const PrefToggleEntries = { {
    { "alt-speed-enabled"sv, TR_KEY_alt_speed_enabled },
    { "compact-view"sv, TR_KEY_compact_view },
    { "show-filterbar"sv, TR_KEY_show_filterbar },
    { "show-statusbar"sv, TR_KEY_show_statusbar },
    { "show-toolbar"sv, TR_KEY_show_toolbar },
    { "sort-reversed"sv, TR_KEY_sort_reversed },
} };

constexpr std::array<std::string_view, 29U> const Entries = {
    "copy-magnet-link-to-clipboard"sv,
    "delete-torrent"sv,
    "deselect-all"sv,
    "donate"sv,
    "edit-preferences"sv,
    "help"sv,
    "new-torrent"sv,
    "open-torrent"sv,
    "open-torrent-folder"sv,
    "open-torrent-from-url"sv,
    "pause-all-torrents"sv,
    "present-main-window"sv,
    "queue-move-bottom"sv,
    "queue-move-down"sv,
    "queue-move-top"sv,
    "queue-move-up"sv,
    "quit"sv,
    "relocate-torrent"sv,
    "remove-torrent"sv,
    "select-all"sv,
    "show-about-dialog"sv,
    "show-stats"sv,
    "show-torrent-properties"sv,
    "start-all-torrents"sv,
    "torrent-reannounce"sv,
    "torrent-start"sv,
    "torrent-start-now"sv,
    "torrent-stop"sv,
    "torrent-verify"sv,
};

Gtk::Builder* myBuilder = nullptr;

std::unordered_map<Glib::ustring, Glib::RefPtr<Gio::SimpleAction>> key_to_action;

} // namespace

void gtr_actions_set_core(Glib::RefPtr<Session> const& core)
{
    myCore = core.get();
}

Glib::RefPtr<Gio::SimpleActionGroup> gtr_actions_init(Glib::RefPtr<Gtk::Builder> const& builder, gpointer callback_user_data)
{
    myBuilder = builder.get();

    auto action_group = Gio::SimpleActionGroup::create();

    {
        auto const action_name = Glib::ustring{ "sort-torrents" };
        auto const current_val = gtr_pref_string_get(TR_KEY_sort_mode);
        auto const action = Gio::SimpleAction::create_radio_string(action_name, current_val);
        action->signal_activate().connect([a = action.get()](auto const& value) { sort_changed_cb(*a, value, nullptr); });
        action_group->add_action(action);
        key_to_action.try_emplace(action_name, action);
    }

    for (auto const& action_name_view : ShowToggleEntries)
    {
        auto const action_name = Glib::ustring{ std::string{ action_name_view } };
        auto const action = Gio::SimpleAction::create_bool(action_name);
        action->signal_activate().connect([a = action.get(), callback_user_data](auto const& /*value*/)
                                          { action_cb(*a, callback_user_data); });
        action_group->add_action(action);
        key_to_action.try_emplace(action_name, action);
    }

    for (auto const& [action_name_view, prefs_name_quark] : PrefToggleEntries)
    {
        auto const action_name = Glib::ustring{ std::string{ action_name_view } };
        auto const action = Gio::SimpleAction::create_bool(action_name, gtr_pref_flag_get(prefs_name_quark));
        action->signal_activate().connect([a = action.get(), prefs_name_quark](auto const& /*value*/)
                                          { toggle_pref_cb(*a, GINT_TO_POINTER(prefs_name_quark)); });
        action_group->add_action(action);
        key_to_action.try_emplace(action_name, action);
    }

    for (auto const& action_name_view : Entries)
    {
        auto const action_name = Glib::ustring{ std::string{ action_name_view } };
        auto const action = Gio::SimpleAction::create(action_name);
        action->signal_activate().connect([a = action.get(), callback_user_data](auto const& /*value*/)
                                          { action_cb(*a, callback_user_data); });
        action_group->add_action(action);
        key_to_action.try_emplace(action_name, action);
    }

    return action_group;
}

/****
*****
****/

namespace
{

Glib::RefPtr<Gio::SimpleAction> get_action(Glib::ustring const& name)
{
    return key_to_action.at(name);
}

} // namespace

void gtr_action_activate(Glib::ustring const& name)
{
    get_action(name)->activate();
}

void gtr_action_set_sensitive(Glib::ustring const& name, bool is_sensitive)
{
    get_action(name)->set_enabled(is_sensitive);
}

void gtr_action_set_toggled(Glib::ustring const& name, bool is_toggled)
{
    get_action(name)->change_state(is_toggled);
}

Glib::RefPtr<Glib::Object> gtr_action_get_object(Glib::ustring const& name)
{
    return myBuilder->get_object(name);
}

#if GTKMM_CHECK_VERSION(4, 0, 0)

Glib::RefPtr<Gio::ListModel> gtr_shortcuts_get_from_menu(Glib::RefPtr<Gio::MenuModel> const& menu)
{
    auto result = Gio::ListStore<Gtk::Shortcut>::create();

    std::stack<Glib::RefPtr<Gio::MenuModel>> links;
    links.push(menu);

    while (!links.empty())
    {
        auto const link = links.top();
        links.pop();

        for (int i = 0; i < link->get_n_items(); ++i)
        {
            Glib::ustring action_name;
            Glib::ustring action_accel;

            for (auto it = link->iterate_item_attributes(i); it->next();)
            {
                if (auto const name = it->get_name(); name == "action")
                {
                    action_name = Glib::VariantBase::cast_dynamic<VariantString>(it->get_value()).get();
                }
                else if (name == "accel")
                {
                    action_accel = Glib::VariantBase::cast_dynamic<VariantString>(it->get_value()).get();
                }
            }

            if (!action_name.empty() && !action_accel.empty())
            {
                result->append(
                    Gtk::Shortcut::create(
                        Gtk::ShortcutTrigger::parse_string(action_accel),
                        Gtk::NamedAction::create(action_name)));
            }

            for (auto it = link->iterate_item_links(i); it->next();)
            {
                links.push(it->get_value());
            }
        }
    }

    return result;
}

#endif
