/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>

#include "actions.h"
#include "conf.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

static TrCore* myCore = nullptr;
static GtkActionGroup* myGroup = nullptr;

static void action_cb(GtkAction* a, gpointer user_data)
{
    gtr_actions_handler(gtk_action_get_name(a), user_data);
}

static GtkRadioActionEntry sort_radio_entries[] = {
    { "sort-by-activity", nullptr, N_("Sort by _Activity"), nullptr, nullptr, 0 },
    { "sort-by-name", nullptr, N_("Sort by _Name"), nullptr, nullptr, 1 },
    { "sort-by-progress", nullptr, N_("Sort by _Progress"), nullptr, nullptr, 2 },
    { "sort-by-queue", nullptr, N_("Sort by _Queue"), nullptr, nullptr, 3 },
    { "sort-by-ratio", nullptr, N_("Sort by Rati_o"), nullptr, nullptr, 4 },
    { "sort-by-state", nullptr, N_("Sort by Stat_e"), nullptr, nullptr, 5 },
    { "sort-by-age", nullptr, N_("Sort by A_ge"), nullptr, nullptr, 6 },
    { "sort-by-time-left", nullptr, N_("Sort by Time _Left"), nullptr, nullptr, 7 },
    { "sort-by-size", nullptr, N_("Sort by Si_ze"), nullptr, nullptr, 8 },
};

static void sort_changed_cb(GtkAction* action, GtkRadioAction* current, gpointer user_data)
{
    TR_UNUSED(action);
    TR_UNUSED(user_data);

    tr_quark const key = TR_KEY_sort_mode;
    int const i = gtk_radio_action_get_current_value(current);
    char const* val = sort_radio_entries[i].name;

    gtr_core_set_pref(myCore, key, val);
}

static GtkToggleActionEntry show_toggle_entries[] = {
    { "toggle-main-window", nullptr, N_("_Show Transmission"), nullptr, nullptr, G_CALLBACK(action_cb), TRUE },
    { "toggle-message-log", nullptr, N_("Message _Log"), nullptr, nullptr, G_CALLBACK(action_cb), FALSE },
};

static void toggle_pref_cb(GtkToggleAction* action, gpointer user_data)
{
    TR_UNUSED(user_data);

    char const* key = gtk_action_get_name(GTK_ACTION(action));
    if (key != nullptr)
    {
        gboolean const val = gtk_toggle_action_get_active(action);
        gtr_core_set_pref_bool(myCore, tr_quark_new(key), val);
    }
}

static GtkToggleActionEntry pref_toggle_entries[] = {
    { "alt-speed-enabled",
      nullptr,
      N_("Enable Alternative Speed _Limits"),
      nullptr,
      nullptr,
      G_CALLBACK(toggle_pref_cb),
      FALSE },
    { "compact-view", nullptr, N_("_Compact View"), "<alt>C", nullptr, G_CALLBACK(toggle_pref_cb), FALSE },
    { "sort-reversed", nullptr, N_("Re_verse Sort Order"), nullptr, nullptr, G_CALLBACK(toggle_pref_cb), FALSE },
    { "show-filterbar", nullptr, N_("_Filterbar"), nullptr, nullptr, G_CALLBACK(toggle_pref_cb), FALSE },
    { "show-statusbar", nullptr, N_("_Statusbar"), nullptr, nullptr, G_CALLBACK(toggle_pref_cb), FALSE },
    { "show-toolbar", nullptr, N_("_Toolbar"), nullptr, nullptr, G_CALLBACK(toggle_pref_cb), FALSE },
};

static GtkActionEntry entries[] = {
    { "file-menu", nullptr, N_("_File"), nullptr, nullptr, nullptr },
    { "torrent-menu", nullptr, N_("_Torrent"), nullptr, nullptr, nullptr },
    { "view-menu", nullptr, N_("_View"), nullptr, nullptr, nullptr },
    { "sort-menu", nullptr, N_("_Sort Torrents By"), nullptr, nullptr, nullptr },
    { "queue-menu", nullptr, N_("_Queue"), nullptr, nullptr, nullptr },
    { "edit-menu", nullptr, N_("_Edit"), nullptr, nullptr, nullptr },
    { "help-menu", nullptr, N_("_Help"), nullptr, nullptr, nullptr },
    { "copy-magnet-link-to-clipboard", "edit-copy", N_("Copy _Magnet Link to Clipboard"), "", nullptr, G_CALLBACK(action_cb) },
    { "open-torrent-from-url", "document-open", N_("Open _URL…"), "<control>U", N_("Open URL…"), G_CALLBACK(action_cb) },
    { "open-torrent-toolbar", "document-open", N_("_Open"), nullptr, N_("Open a torrent"), G_CALLBACK(action_cb) },
    { "open-torrent-menu", "document-open", N_("_Open"), nullptr, N_("Open a torrent"), G_CALLBACK(action_cb) },
    { "torrent-start", "media-playback-start", N_("_Start"), "<control>S", N_("Start torrent"), G_CALLBACK(action_cb) },
    { "torrent-start-now",
      "media-playback-start",
      N_("Start _Now"),
      "<shift><control>S",
      N_("Start torrent now"),
      G_CALLBACK(action_cb) },
    { "show-stats", nullptr, N_("_Statistics"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "donate", nullptr, N_("_Donate"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "torrent-verify", nullptr, N_("_Verify Local Data"), "<control>V", nullptr, G_CALLBACK(action_cb) },
    { "torrent-stop", "media-playback-pause", N_("_Pause"), "<control>P", N_("Pause torrent"), G_CALLBACK(action_cb) },
    { "pause-all-torrents",
      "media-playback-pause",
      N_("_Pause All"),
      nullptr,
      N_("Pause all torrents"),
      G_CALLBACK(action_cb) },
    { "start-all-torrents",
      "media-playback-start",
      N_("_Start All"),
      nullptr,
      N_("Start all torrents"),
      G_CALLBACK(action_cb) },
    { "relocate-torrent", nullptr, N_("Set _Location…"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "remove-torrent", "list-remove", N_("Remove torrent"), "Delete", nullptr, G_CALLBACK(action_cb) },
    { "delete-torrent", "edit-delete", N_("_Delete Files and Remove"), "<shift>Delete", nullptr, G_CALLBACK(action_cb) },
    { "new-torrent", "document-new", N_("_New…"), nullptr, N_("Create a torrent"), G_CALLBACK(action_cb) },
    { "quit", "application-exit", N_("_Quit"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "select-all", "edit-select-all", N_("Select _All"), "<control>A", nullptr, G_CALLBACK(action_cb) },
    { "deselect-all", nullptr, N_("Dese_lect All"), "<shift><control>A", nullptr, G_CALLBACK(action_cb) },
    { "edit-preferences", "preferences-system", N_("_Preferences"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "show-torrent-properties",
      "document-properties",
      N_("_Properties"),
      "<alt>Return",
      N_("Torrent properties"),
      G_CALLBACK(action_cb) },
    { "open-torrent-folder", "document-open", N_("Open Fold_er"), "<control>E", nullptr, G_CALLBACK(action_cb) },
    { "show-about-dialog", "help-about", N_("_About"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "help", "help-browser", N_("_Contents"), "F1", nullptr, G_CALLBACK(action_cb) },
    { "torrent-reannounce", "network-workgroup", N_("Ask Tracker for _More Peers"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "queue-move-top", "go-top", N_("Move to _Top"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "queue-move-up", "go-up", N_("Move _Up"), "<control>Up", nullptr, G_CALLBACK(action_cb) },
    { "queue-move-down", "go-down", N_("Move _Down"), "<control>Down", nullptr, G_CALLBACK(action_cb) },
    { "queue-move-bottom", "go-bottom", N_("Move to _Bottom"), nullptr, nullptr, G_CALLBACK(action_cb) },
    { "present-main-window", nullptr, N_("Present Main Window"), nullptr, nullptr, G_CALLBACK(action_cb) },
};

typedef struct
{
    char const* filename;
    char const* name;
} BuiltinIconInfo;

static BuiltinIconInfo const my_fallback_icons[] = {
    { "logo-48", WINDOW_ICON }, //
    { "logo-24", TRAY_ICON }, //
    { "logo-48", NOTIFICATION_ICON }, //
    { "lock", "transmission-lock" }, //
    { "utilities", "utilities" }, //
    { "turtle-blue", "alt-speed-on" }, //
    { "turtle-grey", "alt-speed-off" }, //
    { "ratio", "ratio" }, //
};

static void register_my_icons(void)
{
    GtkIconTheme* theme = gtk_icon_theme_get_default();
    GtkIconFactory* factory = gtk_icon_factory_new();

    gtk_icon_factory_add_default(factory);

    for (size_t i = 0; i < G_N_ELEMENTS(my_fallback_icons); ++i)
    {
        char const* name = my_fallback_icons[i].name;

        if (!gtk_icon_theme_has_icon(theme, name))
        {
            GdkPixbuf* p;
            gchar* resource_path = g_strdup_printf(TR_RESOURCE_PATH "icons/%s.png", my_fallback_icons[i].filename);

            p = gdk_pixbuf_new_from_resource(resource_path, nullptr);

            g_free(resource_path);

            if (p != nullptr)
            {
                int width;
                GtkIconSet* icon_set;

                width = gdk_pixbuf_get_width(p);
                icon_set = gtk_icon_set_new_from_pixbuf(p);

                gtk_icon_theme_add_builtin_icon(name, width, p);
                gtk_icon_factory_add(factory, name, icon_set);

                g_object_unref(p);
                gtk_icon_set_unref(icon_set);
            }
        }
    }

    g_object_unref(G_OBJECT(factory));
}

static GtkUIManager* myUIManager = nullptr;

void gtr_actions_set_core(TrCore* core)
{
    myCore = core;
}

void gtr_actions_init(GtkUIManager* ui_manager, gpointer callback_user_data)
{
    int active = -1;
    char const* match;
    int const n_entries = G_N_ELEMENTS(entries);
    GtkActionGroup* action_group;

    myUIManager = ui_manager;

    register_my_icons();

    action_group = myGroup = gtk_action_group_new("Actions");
    gtk_action_group_set_translation_domain(action_group, nullptr);

    match = gtr_pref_string_get(TR_KEY_sort_mode);

    for (size_t i = 0; active == -1 && i < G_N_ELEMENTS(sort_radio_entries); ++i)
    {
        if (g_strcmp0(sort_radio_entries[i].name, match) == 0)
        {
            active = i;
        }
    }

    gtk_action_group_add_radio_actions(
        action_group,
        sort_radio_entries,
        G_N_ELEMENTS(sort_radio_entries),
        active,
        G_CALLBACK(sort_changed_cb),
        nullptr);

    gtk_action_group_add_toggle_actions(
        action_group,
        show_toggle_entries,
        G_N_ELEMENTS(show_toggle_entries),
        callback_user_data);

    for (size_t i = 0; i < G_N_ELEMENTS(pref_toggle_entries); ++i)
    {
        pref_toggle_entries[i].is_active = gtr_pref_flag_get(tr_quark_new(pref_toggle_entries[i].name));
    }

    gtk_action_group_add_toggle_actions(
        action_group,
        pref_toggle_entries,
        G_N_ELEMENTS(pref_toggle_entries),
        callback_user_data);

    gtk_action_group_add_actions(action_group, entries, n_entries, callback_user_data);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);
    g_object_unref(G_OBJECT(action_group));
}

/****
*****
****/

static GHashTable* key_to_action = nullptr;

static void ensure_action_map_loaded(GtkUIManager* uim)
{
    if (key_to_action != nullptr)
    {
        return;
    }

    key_to_action = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);

    for (GList* l = gtk_ui_manager_get_action_groups(uim); l != nullptr; l = l->next)
    {
        GtkActionGroup* action_group = GTK_ACTION_GROUP(l->data);
        GList* actions = gtk_action_group_list_actions(action_group);

        for (GList* ait = actions; ait != nullptr; ait = ait->next)
        {
            GtkAction* action = GTK_ACTION(ait->data);
            char const* name = gtk_action_get_name(action);
            g_hash_table_insert(key_to_action, g_strdup(name), action);
        }

        g_list_free(actions);
    }
}

static GtkAction* get_action(char const* name)
{
    ensure_action_map_loaded(myUIManager);
    return (GtkAction*)g_hash_table_lookup(key_to_action, name);
}

void gtr_action_activate(char const* name)
{
    GtkAction* action = get_action(name);

    g_assert(action != nullptr);
    gtk_action_activate(action);
}

void gtr_action_set_sensitive(char const* name, gboolean b)
{
    GtkAction* action = get_action(name);

    g_assert(action != nullptr);
    g_object_set(action, "sensitive", b, nullptr);
}

void gtr_action_set_important(char const* name, gboolean b)
{
    GtkAction* action = get_action(name);

    g_assert(action != nullptr);
    g_object_set(action, "is-important", b, nullptr);
}

void gtr_action_set_toggled(char const* name, gboolean b)
{
    GtkAction* action = get_action(name);

    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), b);
}

GtkWidget* gtr_action_get_widget(char const* path)
{
    return gtk_ui_manager_get_widget(myUIManager, path);
}
