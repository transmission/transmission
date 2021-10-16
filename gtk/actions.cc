/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string>
#include <string.h>
#include <unordered_map>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>

#include "actions.h"
#include "conf.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

namespace
{

TrCore* myCore = nullptr;

void action_cb(Glib::RefPtr<Gtk::Action> const& a, void* user_data)
{
    gtr_actions_handler(a->get_name(), user_data);
}

struct ActionEntryBase
{
    char const* name;
    char const* stock_id;
    char const* label;
    char const* accelerator;
    char const* tooltip;
};

ActionEntryBase sort_radio_entries[] = {
    { "sort-by-activity", nullptr, N_("Sort by _Activity"), nullptr, nullptr },
    { "sort-by-name", nullptr, N_("Sort by _Name"), nullptr, nullptr },
    { "sort-by-progress", nullptr, N_("Sort by _Progress"), nullptr, nullptr },
    { "sort-by-queue", nullptr, N_("Sort by _Queue"), nullptr, nullptr },
    { "sort-by-ratio", nullptr, N_("Sort by Rati_o"), nullptr, nullptr },
    { "sort-by-state", nullptr, N_("Sort by Stat_e"), nullptr, nullptr },
    { "sort-by-age", nullptr, N_("Sort by A_ge"), nullptr, nullptr },
    { "sort-by-time-left", nullptr, N_("Sort by Time _Left"), nullptr, nullptr },
    { "sort-by-size", nullptr, N_("Sort by Si_ze"), nullptr, nullptr },
};

void sort_changed_cb(Glib::RefPtr<Gtk::RadioAction> const& action, void* /*user_data*/)
{
    if (!action->get_active())
    {
        return;
    }

    myCore->set_pref(TR_KEY_sort_mode, action->get_name().c_str());
}

struct : ActionEntryBase
{
    bool is_active;
} show_toggle_entries[] = {
    { "toggle-main-window", nullptr, N_("_Show Transmission"), nullptr, nullptr, true },
    { "toggle-message-log", nullptr, N_("Message _Log"), nullptr, nullptr, false },
};

void toggle_pref_cb(Glib::RefPtr<Gtk::ToggleAction> const& action, void* /*user_data*/)
{
    auto const key = action->get_name();
    bool const val = action->get_active();

    myCore->set_pref(tr_quark_new({ key.c_str(), key.size() }), val);
}

struct : ActionEntryBase
{
    bool is_active;
} pref_toggle_entries[] = {
    { "alt-speed-enabled", nullptr, N_("Enable Alternative Speed _Limits"), nullptr, nullptr, false },
    { "compact-view", nullptr, N_("_Compact View"), "<alt>C", nullptr, false },
    { "sort-reversed", nullptr, N_("Re_verse Sort Order"), nullptr, nullptr, false },
    { "show-filterbar", nullptr, N_("_Filterbar"), nullptr, nullptr, false },
    { "show-statusbar", nullptr, N_("_Statusbar"), nullptr, nullptr, false },
    { "show-toolbar", nullptr, N_("_Toolbar"), nullptr, nullptr, false },
};

struct : ActionEntryBase
{
    bool is_actionable;
} entries[] = {
    { "file-menu", nullptr, N_("_File"), nullptr, nullptr, false },
    { "torrent-menu", nullptr, N_("_Torrent"), nullptr, nullptr, false },
    { "view-menu", nullptr, N_("_View"), nullptr, nullptr, false },
    { "sort-menu", nullptr, N_("_Sort Torrents By"), nullptr, nullptr, false },
    { "queue-menu", nullptr, N_("_Queue"), nullptr, nullptr, false },
    { "edit-menu", nullptr, N_("_Edit"), nullptr, nullptr, false },
    { "help-menu", nullptr, N_("_Help"), nullptr, nullptr, false },
    { "copy-magnet-link-to-clipboard", "edit-copy", N_("Copy _Magnet Link to Clipboard"), "", nullptr, true },
    { "open-torrent-from-url", "document-open", N_("Open _URL…"), "<control>U", N_("Open URL…"), true },
    { "open-torrent-toolbar", "document-open", N_("_Open"), nullptr, N_("Open a torrent"), true },
    { "open-torrent-menu", "document-open", N_("_Open"), nullptr, N_("Open a torrent"), true },
    { "torrent-start", "media-playback-start", N_("_Start"), "<control>S", N_("Start torrent"), true },
    { "torrent-start-now", "media-playback-start", N_("Start _Now"), "<shift><control>S", N_("Start torrent now"), true },
    { "show-stats", nullptr, N_("_Statistics"), nullptr, nullptr, true },
    { "donate", nullptr, N_("_Donate"), nullptr, nullptr, true },
    { "torrent-verify", nullptr, N_("_Verify Local Data"), "<control>V", nullptr, true },
    { "torrent-stop", "media-playback-pause", N_("_Pause"), "<control>P", N_("Pause torrent"), true },
    { "pause-all-torrents", "media-playback-pause", N_("_Pause All"), nullptr, N_("Pause all torrents"), true },
    { "start-all-torrents", "media-playback-start", N_("_Start All"), nullptr, N_("Start all torrents"), true },
    { "relocate-torrent", nullptr, N_("Set _Location…"), nullptr, nullptr, true },
    { "remove-torrent", "list-remove", N_("Remove torrent"), "Delete", nullptr, true },
    { "delete-torrent", "edit-delete", N_("_Delete Files and Remove"), "<shift>Delete", nullptr, true },
    { "new-torrent", "document-new", N_("_New…"), nullptr, N_("Create a torrent"), true },
    { "quit", "application-exit", N_("_Quit"), nullptr, nullptr, true },
    { "select-all", "edit-select-all", N_("Select _All"), "<control>A", nullptr, true },
    { "deselect-all", nullptr, N_("Dese_lect All"), "<shift><control>A", nullptr, true },
    { "edit-preferences", "preferences-system", N_("_Preferences"), nullptr, nullptr, true },
    { "show-torrent-properties", "document-properties", N_("_Properties"), "<alt>Return", N_("Torrent properties"), true },
    { "open-torrent-folder", "document-open", N_("Open Fold_er"), "<control>E", nullptr, true },
    { "show-about-dialog", "help-about", N_("_About"), nullptr, nullptr, true },
    { "help", "help-browser", N_("_Contents"), "F1", nullptr, true },
    { "torrent-reannounce", "network-workgroup", N_("Ask Tracker for _More Peers"), nullptr, nullptr, true },
    { "queue-move-top", "go-top", N_("Move to _Top"), nullptr, nullptr, true },
    { "queue-move-up", "go-up", N_("Move _Up"), "<control>Up", nullptr, true },
    { "queue-move-down", "go-down", N_("Move _Down"), "<control>Down", nullptr, true },
    { "queue-move-bottom", "go-bottom", N_("Move to _Bottom"), nullptr, nullptr, true },
    { "present-main-window", nullptr, N_("Present Main Window"), nullptr, nullptr, true },
};

struct BuiltinIconInfo
{
    char const* filename;
    char const* name;
};

BuiltinIconInfo const my_fallback_icons[] = {
    { "logo-48", WINDOW_ICON }, //
    { "logo-24", TRAY_ICON }, //
    { "logo-48", NOTIFICATION_ICON }, //
    { "lock", "transmission-lock" }, //
    { "utilities", "utilities" }, //
    { "turtle-blue", "alt-speed-on" }, //
    { "turtle-grey", "alt-speed-off" }, //
    { "ratio", "ratio" }, //
};

void register_my_icons()
{
    auto const theme = Gtk::IconTheme::get_default();
    auto const factory = Gtk::IconFactory::create();

    factory->add_default();

    for (auto const& icon : my_fallback_icons)
    {
        if (!theme->has_icon(icon.name))
        {
            auto const p = Gdk::Pixbuf::create_from_resource(
                Glib::ustring::sprintf(TR_RESOURCE_PATH "icons/%s.png", icon.filename));

            if (p != nullptr)
            {
                Gtk::IconTheme::add_builtin_icon(icon.name, p->get_width(), p);
                factory->add(Gtk::StockID(icon.name), Gtk::IconSet::create(p));
            }
        }
    }
}

Gtk::UIManager* myUIManager = nullptr;

} // namespace

void gtr_actions_set_core(Glib::RefPtr<TrCore> const& core)
{
    myCore = core.get();
}

void gtr_actions_init(Glib::RefPtr<Gtk::UIManager> const& ui_manager, void* callback_user_data)
{
    myUIManager = ui_manager.get();

    register_my_icons();

    auto const action_group = Gtk::ActionGroup::create("Actions");

    char const* const match = gtr_pref_string_get(TR_KEY_sort_mode);
    Gtk::RadioAction::Group sort_group;

    for (auto const& entry : sort_radio_entries)
    {
        auto const action = Gtk::RadioAction::create(sort_group, entry.name, _(entry.label));

        if (g_strcmp0(entry.name, match) == 0)
        {
            action->set_active(true);
        }

        action_group->add(action, [action, callback_user_data]() { sort_changed_cb(action, callback_user_data); });
    }

    for (auto const& entry : show_toggle_entries)
    {
        auto const action = Gtk::ToggleAction::create(entry.name, _(entry.label), {}, entry.is_active);
        action_group->add(action, [action, callback_user_data]() { action_cb(action, callback_user_data); });
    }

    for (auto& entry : pref_toggle_entries)
    {
        entry.is_active = gtr_pref_flag_get(tr_quark_new(entry.name));
    }

    for (auto const& entry : pref_toggle_entries)
    {
        auto const action = Gtk::ToggleAction::create(entry.name, _(entry.label), {}, entry.is_active);
        action->signal_activate().connect([action, callback_user_data]() { toggle_pref_cb(action, callback_user_data); });
        if (entry.accelerator != nullptr)
        {
            action_group->add(action, Gtk::AccelKey(entry.accelerator));
        }
        else
        {
            action_group->add(action);
        }
    }

    for (auto const& entry : entries)
    {
        auto const action = Gtk::Action::create(
            entry.name,
            entry.stock_id != nullptr ? Gtk::StockID(entry.stock_id) : Gtk::StockID(),
            _(entry.label),
            entry.tooltip != nullptr ? _(entry.tooltip) : Glib::ustring());
        if (entry.stock_id != nullptr && Gtk::IconTheme::get_default()->has_icon(entry.stock_id))
        {
            action->set_icon_name(entry.stock_id);
        }
        if (entry.is_actionable)
        {
            action->signal_activate().connect([action, callback_user_data]() { action_cb(action, callback_user_data); });
        }
        if (entry.accelerator != nullptr)
        {
            action_group->add(action, Gtk::AccelKey(entry.accelerator));
        }
        else
        {
            action_group->add(action);
        }
    }

    ui_manager->insert_action_group(action_group, 0);
}

/****
*****
****/

namespace
{

std::unordered_map<Glib::ustring, Glib::RefPtr<Gtk::Action>> key_to_action;

void ensure_action_map_loaded(Gtk::UIManager& uim)
{
    if (!key_to_action.empty())
    {
        return;
    }

    for (auto const& action_group : uim.get_action_groups())
    {
        for (auto const& action : action_group->get_actions())
        {
            key_to_action.emplace(action->get_name(), action);
        }
    }
}

Glib::RefPtr<Gtk::Action> get_action(Glib::ustring const& name)
{
    ensure_action_map_loaded(*myUIManager);
    return key_to_action.at(name);
}

} // namespace

void gtr_action_activate(Glib::ustring const& name)
{
    get_action(name)->activate();
}

void gtr_action_set_sensitive(Glib::ustring const& name, bool b)
{
    get_action(name)->set_sensitive(b);
}

void gtr_action_set_important(Glib::ustring const& name, bool b)
{
    get_action(name)->set_is_important(b);
}

void gtr_action_set_toggled(Glib::ustring const& name, bool b)
{
    dynamic_cast<Gtk::ToggleAction*>(get_action(name).get())->set_active(b);
}

Gtk::Widget* gtr_action_get_widget(Glib::ustring const& path)
{
    return myUIManager->get_widget(path);
}
