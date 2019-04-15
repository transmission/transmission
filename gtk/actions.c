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

static TrCore* myCore = NULL;
static GtkActionGroup* myGroup = NULL;

static void action_cb(GtkAction* a, gpointer user_data)
{
    gtr_actions_handler(gtk_action_get_name(a), user_data);
}

static GtkRadioActionEntry sort_radio_entries[] =
{
    { "sort-by-activity", NULL, N_("Sort by _Activity"), NULL, NULL, 0 },
    { "sort-by-name", NULL, N_("Sort by _Name"), NULL, NULL, 1 },
    { "sort-by-progress", NULL, N_("Sort by _Progress"), NULL, NULL, 2 },
    { "sort-by-queue", NULL, N_("Sort by _Queue"), NULL, NULL, 3 },
    { "sort-by-ratio", NULL, N_("Sort by Rati_o"), NULL, NULL, 4 },
    { "sort-by-state", NULL, N_("Sort by Stat_e"), NULL, NULL, 5 },
    { "sort-by-age", NULL, N_("Sort by A_ge"), NULL, NULL, 6 },
    { "sort-by-time-left", NULL, N_("Sort by Time _Left"), NULL, NULL, 7 },
    { "sort-by-size", NULL, N_("Sort by Si_ze"), NULL, NULL, 8 }
};

static void sort_changed_cb(GtkAction* action UNUSED, GtkRadioAction* current, gpointer user_data UNUSED)
{
    tr_quark const key = TR_KEY_sort_mode;
    int const i = gtk_radio_action_get_current_value(current);
    char const* val = sort_radio_entries[i].name;

    gtr_core_set_pref(myCore, key, val);
}

static GtkToggleActionEntry show_toggle_entries[] =
{
    { "toggle-main-window", NULL, N_("_Show Transmission"), NULL, NULL, G_CALLBACK(action_cb), TRUE },
    { "toggle-message-log", NULL, N_("Message _Log"), NULL, NULL, G_CALLBACK(action_cb), FALSE }
};

static void toggle_pref_cb(GtkToggleAction* action, gpointer user_data UNUSED)
{
    char const* key = gtk_action_get_name(GTK_ACTION(action));
    gboolean const val = gtk_toggle_action_get_active(action);

    gtr_core_set_pref_bool(myCore, tr_quark_new(key, TR_BAD_SIZE), val);
}

static GtkToggleActionEntry pref_toggle_entries[] =
{
    { "alt-speed-enabled", NULL, N_("Enable Alternative Speed _Limits"), NULL, NULL, G_CALLBACK(toggle_pref_cb), FALSE },
    { "compact-view", NULL, N_("_Compact View"), "<alt>C", NULL, G_CALLBACK(toggle_pref_cb), FALSE },
    { "sort-reversed", NULL, N_("Re_verse Sort Order"), NULL, NULL, G_CALLBACK(toggle_pref_cb), FALSE },
    { "show-filterbar", NULL, N_("_Filterbar"), NULL, NULL, G_CALLBACK(toggle_pref_cb), FALSE },
    { "show-statusbar", NULL, N_("_Statusbar"), NULL, NULL, G_CALLBACK(toggle_pref_cb), FALSE },
    { "show-toolbar", NULL, N_("_Toolbar"), NULL, NULL, G_CALLBACK(toggle_pref_cb), FALSE }
};

static GtkActionEntry entries[] =
{
    { "file-menu", NULL, N_("_File"), NULL, NULL, NULL },
    { "torrent-menu", NULL, N_("_Torrent"), NULL, NULL, NULL },
    { "view-menu", NULL, N_("_View"), NULL, NULL, NULL },
    { "sort-menu", NULL, N_("_Sort Torrents By"), NULL, NULL, NULL },
    { "queue-menu", NULL, N_("_Queue"), NULL, NULL, NULL },
    { "edit-menu", NULL, N_("_Edit"), NULL, NULL, NULL },
    { "help-menu", NULL, N_("_Help"), NULL, NULL, NULL },
    { "copy-magnet-link-to-clipboard", GTK_STOCK_COPY, N_("Copy _Magnet Link to Clipboard"), "", NULL, G_CALLBACK(action_cb) },
    { "open-torrent-from-url", GTK_STOCK_OPEN, N_("Open _URL…"), "<control>U", N_("Open URL…"), G_CALLBACK(action_cb) },
    { "open-torrent-toolbar", GTK_STOCK_OPEN, NULL, NULL, N_("Open a torrent"), G_CALLBACK(action_cb) },
    { "open-torrent-menu", GTK_STOCK_OPEN, NULL, NULL, N_("Open a torrent"), G_CALLBACK(action_cb) },
    { "torrent-start", GTK_STOCK_MEDIA_PLAY, N_("_Start"), "<control>S", N_("Start torrent"), G_CALLBACK(action_cb) },
    { "torrent-start-now", GTK_STOCK_MEDIA_PLAY, N_("Start _Now"), "<shift><control>S", N_("Start torrent now"),
        G_CALLBACK(action_cb) },
    { "show-stats", NULL, N_("_Statistics"), NULL, NULL, G_CALLBACK(action_cb) },
    { "donate", NULL, N_("_Donate"), NULL, NULL, G_CALLBACK(action_cb) },
    { "torrent-verify", NULL, N_("_Verify Local Data"), "<control>V", NULL, G_CALLBACK(action_cb) },
    { "torrent-stop", GTK_STOCK_MEDIA_PAUSE, N_("_Pause"), "<control>P", N_("Pause torrent"), G_CALLBACK(action_cb) },
    { "pause-all-torrents", GTK_STOCK_MEDIA_PAUSE, N_("_Pause All"), NULL, N_("Pause all torrents"), G_CALLBACK(action_cb) },
    { "start-all-torrents", GTK_STOCK_MEDIA_PLAY, N_("_Start All"), NULL, N_("Start all torrents"), G_CALLBACK(action_cb) },
    { "relocate-torrent", NULL, N_("Set _Location…"), NULL, NULL, G_CALLBACK(action_cb) },
    { "remove-torrent", GTK_STOCK_REMOVE, NULL, "Delete", N_("Remove torrent"), G_CALLBACK(action_cb) },
    { "delete-torrent", GTK_STOCK_DELETE, N_("_Delete Files and Remove"), "<shift>Delete", NULL, G_CALLBACK(action_cb) },
    { "new-torrent", GTK_STOCK_NEW, N_("_New…"), NULL, N_("Create a torrent"), G_CALLBACK(action_cb) },
    { "quit", GTK_STOCK_QUIT, N_("_Quit"), NULL, NULL, G_CALLBACK(action_cb) },
    { "select-all", GTK_STOCK_SELECT_ALL, N_("Select _All"), "<control>A", NULL, G_CALLBACK(action_cb) },
    { "deselect-all", NULL, N_("Dese_lect All"), "<shift><control>A", NULL, G_CALLBACK(action_cb) },
    { "edit-preferences", GTK_STOCK_PREFERENCES, NULL, NULL, NULL, G_CALLBACK(action_cb) },
    { "show-torrent-properties", GTK_STOCK_PROPERTIES, NULL, "<alt>Return", N_("Torrent properties"), G_CALLBACK(action_cb) },
    { "open-torrent-folder", GTK_STOCK_OPEN, N_("Open Fold_er"), "<control>E", NULL, G_CALLBACK(action_cb) },
    { "show-about-dialog", GTK_STOCK_ABOUT, NULL, NULL, NULL, G_CALLBACK(action_cb) },
    { "help", GTK_STOCK_HELP, N_("_Contents"), "F1", NULL, G_CALLBACK(action_cb) },
    { "torrent-reannounce", GTK_STOCK_NETWORK, N_("Ask Tracker for _More Peers"), NULL, NULL, G_CALLBACK(action_cb) },
    { "queue-move-top", GTK_STOCK_GOTO_TOP, N_("Move to _Top"), NULL, NULL, G_CALLBACK(action_cb) },
    { "queue-move-up", GTK_STOCK_GO_UP, N_("Move _Up"), "<control>Up", NULL, G_CALLBACK(action_cb) },
    { "queue-move-down", GTK_STOCK_GO_DOWN, N_("Move _Down"), "<control>Down", NULL, G_CALLBACK(action_cb) },
    { "queue-move-bottom", GTK_STOCK_GOTO_BOTTOM, N_("Move to _Bottom"), NULL, NULL, G_CALLBACK(action_cb) },
    { "present-main-window", NULL, N_("Present Main Window"), NULL, NULL, G_CALLBACK(action_cb) }
};

typedef struct
{
    char const* filename;
    char const* name;
}
BuiltinIconInfo;

static BuiltinIconInfo const my_fallback_icons[] =
{
    { "logo-48", WINDOW_ICON },
    { "logo-24", TRAY_ICON },
    { "logo-48", NOTIFICATION_ICON },
    { "lock", "transmission-lock" },
    { "utilities", "utilities" },
    { "turtle-blue", "alt-speed-on" },
    { "turtle-grey", "alt-speed-off" },
    { "ratio", "ratio" }
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

            p = gdk_pixbuf_new_from_resource(resource_path, NULL);

            g_free(resource_path);

            if (p != NULL)
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

static GtkUIManager* myUIManager = NULL;

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
    gtk_action_group_set_translation_domain(action_group, NULL);

    match = gtr_pref_string_get(TR_KEY_sort_mode);

    for (size_t i = 0; active == -1 && i < G_N_ELEMENTS(sort_radio_entries); ++i)
    {
        if (g_strcmp0(sort_radio_entries[i].name, match) == 0)
        {
            active = i;
        }
    }

    gtk_action_group_add_radio_actions(action_group, sort_radio_entries, G_N_ELEMENTS(sort_radio_entries), active,
        G_CALLBACK(sort_changed_cb), NULL);

    gtk_action_group_add_toggle_actions(action_group, show_toggle_entries, G_N_ELEMENTS(show_toggle_entries),
        callback_user_data);

    for (size_t i = 0; i < G_N_ELEMENTS(pref_toggle_entries); ++i)
    {
        pref_toggle_entries[i].is_active = gtr_pref_flag_get(tr_quark_new(pref_toggle_entries[i].name, TR_BAD_SIZE));
    }

    gtk_action_group_add_toggle_actions(action_group, pref_toggle_entries, G_N_ELEMENTS(pref_toggle_entries),
        callback_user_data);

    gtk_action_group_add_actions(action_group, entries, n_entries, callback_user_data);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);
    g_object_unref(G_OBJECT(action_group));
}

/****
*****
****/

static GHashTable* key_to_action = NULL;

static void ensure_action_map_loaded(GtkUIManager* uim)
{
    if (key_to_action != NULL)
    {
        return;
    }

    key_to_action = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (GList* l = gtk_ui_manager_get_action_groups(uim); l != NULL; l = l->next)
    {
        GtkActionGroup* action_group = GTK_ACTION_GROUP(l->data);
        GList* actions = gtk_action_group_list_actions(action_group);

        for (GList* ait = actions; ait != NULL; ait = ait->next)
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

    g_assert(action != NULL);
    gtk_action_activate(action);
}

void gtr_action_set_sensitive(char const* name, gboolean b)
{
    GtkAction* action = get_action(name);

    g_assert(action != NULL);
    g_object_set(action, "sensitive", b, NULL);
}

void gtr_action_set_important(char const* name, gboolean b)
{
    GtkAction* action = get_action(name);

    g_assert(action != NULL);
    g_object_set(action, "is-important", b, NULL);
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
