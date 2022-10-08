// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>

#include "Actions.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

using namespace std::string_view_literals;

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
    myCore->set_pref(TR_KEY_sort_mode, Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get());
}

std::array<std::string_view, 2> const show_toggle_entries = {
    "toggle-main-window"sv,
    "toggle-message-log"sv,
};

void toggle_pref_cb(Gio::SimpleAction& action, gpointer /*user_data*/)
{
    auto const key = action.get_name();
    bool val = false;
    action.get_state(val);

    action.set_state(Glib::Variant<bool>::create(!val));

    myCore->set_pref(tr_quark_new({ key.c_str(), key.size() }), !val);
}

std::array<std::string_view, 6> const pref_toggle_entries = {
    "alt-speed-enabled"sv, //
    "compact-view"sv, //
    "sort-reversed"sv, //
    "show-filterbar"sv, //
    "show-statusbar"sv, //
    "show-toolbar"sv, //
};

std::array<std::string_view, 29> const entries = {
    "copy-magnet-link-to-clipboard"sv,
    "open-torrent-from-url"sv,
    "open-torrent"sv,
    "torrent-start"sv,
    "torrent-start-now"sv,
    "show-stats"sv,
    "donate"sv,
    "torrent-verify"sv,
    "torrent-stop"sv,
    "pause-all-torrents"sv,
    "start-all-torrents"sv,
    "relocate-torrent"sv,
    "remove-torrent"sv,
    "delete-torrent"sv,
    "new-torrent"sv,
    "quit"sv,
    "select-all"sv,
    "deselect-all"sv,
    "edit-preferences"sv,
    "show-torrent-properties"sv,
    "open-torrent-folder"sv,
    "show-about-dialog"sv,
    "help"sv,
    "torrent-reannounce"sv,
    "queue-move-top"sv,
    "queue-move-up"sv,
    "queue-move-down"sv,
    "queue-move-bottom"sv,
    "present-main-window"sv,
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

    auto const action_group = Gio::SimpleActionGroup::create();

    auto const match = gtr_pref_string_get(TR_KEY_sort_mode);

    {
        auto const action_name = Glib::ustring("sort-torrents");
        auto const action = Gio::SimpleAction::create_radio_string(action_name, match);
        action->signal_activate().connect([a = action.get(), callback_user_data](auto const& value)
                                          { sort_changed_cb(*a, value, callback_user_data); });
        action_group->add_action(action);
        key_to_action.try_emplace(action_name, action);
    }

    for (auto const& action_name_view : show_toggle_entries)
    {
        auto const action_name = Glib::ustring(std::string(action_name_view));
        auto const action = Gio::SimpleAction::create_bool(action_name);
        action->signal_activate().connect([a = action.get(), callback_user_data](auto const& /*value*/)
                                          { action_cb(*a, callback_user_data); });
        action_group->add_action(action);
        key_to_action.try_emplace(action_name, action);
    }

    for (auto const& action_name_view : pref_toggle_entries)
    {
        auto const action_name = Glib::ustring(std::string(action_name_view));
        auto const action = Gio::SimpleAction::create_bool(action_name, gtr_pref_flag_get(tr_quark_new(action_name_view)));
        action->signal_activate().connect([a = action.get(), callback_user_data](auto const& /*value*/)
                                          { toggle_pref_cb(*a, callback_user_data); });
        action_group->add_action(action);
        key_to_action.try_emplace(action_name, action);
    }

    for (auto const& action_name_view : entries)
    {
        auto const action_name = Glib::ustring(std::string(action_name_view));
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

void gtr_action_set_sensitive(Glib::ustring const& name, bool b)
{
    get_action(name)->set_enabled(b);
}

void gtr_action_set_toggled(Glib::ustring const& name, bool b)
{
    get_action(name)->set_state(Glib::Variant<bool>::create(b));
}

Gtk::Widget* gtr_action_get_widget(Glib::ustring const& name)
{
    Gtk::Widget* widget;
    myBuilder->get_widget(name, widget);
    return widget;
}

Glib::RefPtr<Glib::Object> gtr_action_get_object(Glib::ustring const& name)
{
    return myBuilder->get_object(name);
}
