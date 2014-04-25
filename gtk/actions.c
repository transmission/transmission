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

#define TR_RESOURCE_PATH "/com/transmissionbt/transmission/"

static TrCore* myCore = NULL;

static void action_cb(GSimpleAction* a, GVariant* parameter UNUSED, gpointer user_data)
{
    gtr_actions_handler(g_action_get_name(G_ACTION(a)), user_data);
}

static void sort_action_cb(GSimpleAction* action, GVariant* parameter, gpointer user_data UNUSED)
{
    g_action_change_state(G_ACTION(action), parameter);
}

static void change_sort_cb(GSimpleAction* action, GVariant* state, gpointer user_data UNUSED)
{
    char const* val = g_variant_get_string(state, NULL);

    gtr_core_set_pref(myCore, PREF_KEY_SORT_MODE, val);
    g_simple_action_set_state(action, state);
}

static void toggle_action_cb(GSimpleAction* action, GVariant* parameter UNUSED, gpointer user_data UNUSED)
{
    GVariant* state = g_action_get_state(G_ACTION(action));
    gboolean const val = g_variant_get_boolean(state);

    g_action_change_state(G_ACTION(action), g_variant_new_boolean(!val));
    g_variant_unref(state);
}

static void change_pref_cb(GSimpleAction* action, GVariant* state, gpointer user_data UNUSED)
{
    char const* key = g_action_get_name(G_ACTION(action));
 
    g_return_if_fail(g_variant_is_of_type(state, G_VARIANT_TYPE_BOOLEAN));

    gtr_core_set_pref_bool(myCore, key, g_variant_get_boolean(state));
    g_simple_action_set_state(action, state);
}

static void change_toggle_cb(GSimpleAction* action, GVariant* state, gpointer user_data UNUSED)
{
    gtr_actions_handler(g_action_get_name(G_ACTION(action)), user_data);
    g_simple_action_set_state(action, state);
}

static GActionEntry win_entries[] =
{
    /* radio actions */
    { "sort-mode", sort_action_cb, "s", "\"sort-by-activity\"", change_sort_cb, {} },
    /* show toggle actions */
    { "toggle-main-window", toggle_action_cb, NULL, "true", change_toggle_cb, {} },
    { "toggle-message-log", toggle_action_cb, NULL, "false", change_toggle_cb, {} },
    /* pref toggle actions */
    { "alt-speed-enabled", toggle_action_cb, NULL, "false", change_pref_cb, {} },
    { "compact-view", toggle_action_cb, NULL, "false", change_pref_cb, {} },
    { "sort-reversed", toggle_action_cb, NULL, "false", change_pref_cb, {} },
    { "show-filterbar", toggle_action_cb, NULL, "true", change_pref_cb, {} },
    { "show-statusbar", toggle_action_cb, NULL, "true", change_pref_cb, {} },
    { "show-toolbar", toggle_action_cb, NULL, "true", change_pref_cb, {} },
    /* plain actions */
    { "seed-until", action_cb, NULL, NULL, NULL, {} },
    { "copy-magnet-link-to-clipboard", action_cb, NULL, NULL, NULL, {} },
    { "open-torrent-from-url", action_cb, NULL, NULL, NULL, {} },
    { "open-torrent", action_cb, NULL, NULL, NULL, {} },
    { "torrent-start", action_cb, NULL, NULL, NULL, {} },
    { "torrent-start-now", action_cb, NULL, NULL, NULL, {} },
    { "show-stats", action_cb, NULL, NULL, NULL, {} },
    { "donate", action_cb, NULL, NULL, NULL, {} },
    { "torrent-verify", action_cb, NULL, NULL, NULL, {} },
    { "torrent-stop", action_cb, NULL, NULL, NULL, {} },
    { "pause-all-torrents", action_cb, NULL, NULL, NULL, {} },
    { "start-all-torrents", action_cb, NULL, NULL, NULL, {} },
    { "relocate-torrent", action_cb, NULL, NULL, NULL, {} },
    { "remove-torrent", action_cb, NULL, NULL, NULL, {} },
    { "delete-torrent", action_cb, NULL, NULL, NULL, {} },
    { "new-torrent", action_cb, NULL, NULL, NULL, {} },
    { "quit", action_cb, NULL, NULL, NULL, {} },
    { "select-all", action_cb, NULL, NULL, NULL, {} },
    { "deselect-all", action_cb, NULL, NULL, NULL, {} },
    { "preferences", action_cb, NULL, NULL, NULL, {} },
    { "show-torrent-properties", action_cb, NULL, NULL, NULL, {} },
    { "open-torrent-folder", action_cb, NULL, NULL, NULL, {} },
    { "show-about-dialog", action_cb, NULL, NULL, NULL, {} },
    { "help", action_cb, NULL, NULL, NULL, {} },
    { "torrent-reannounce", action_cb, NULL, NULL, NULL, {} },
    { "queue-move-top", action_cb, NULL, NULL, NULL, {} },
    { "queue-move-up", action_cb, NULL, NULL, NULL, {} },
    { "queue-move-down", action_cb, NULL, NULL, NULL, {} },
    { "queue-move-bottom", action_cb, NULL, NULL, NULL, {} },
    { "present-main-window", action_cb, NULL, NULL, NULL, {} }
};

static void update_entry_states(GActionEntry* entries, int n_entries)
{
    g_return_if_fail(entries != NULL || n_entries == 0);

    for (int i = 0; n_entries == -1 ? entries[i].name != NULL : i < n_entries; i++)
    {
        GActionEntry* entry = &entries[i];

        if (strcmp(entry->name, "sort-mode") == 0)
        {
            char const* value = gtr_pref_string_get("sort-mode");
            entry->state = g_strdup_printf("\"%s\"", value); /* FIXME: leak */
        }
        else if (strcmp(entry->name, "alt-speed-enabled") == 0 || strcmp(entry->name, "compact-view") == 0 ||
            strcmp(entry->name, "sort-reversed") == 0 || strcmp(entry->name, "show-filterbar") == 0 ||
            strcmp(entry->name, "show-statusbar") == 0 || strcmp(entry->name, "show-toolbar") == 0)
        {
            gboolean value = gtr_pref_flag_get(entry->name);
            entry->state = value ? "true" : "false";
        }
    }
}

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

                width = gdk_pixbuf_get_width(p);

                gtk_icon_theme_add_builtin_icon(name, width, p);

                g_object_unref(p);
            }
        }
    }
}

void gtr_actions_set_core(TrCore* core)
{
    myCore = core;
}

void gtr_actions_init(GtkApplication* app, gpointer callback_user_data UNUSED)
{
    gtk_application_set_app_menu(app, gtr_action_get_menu_model("menubar"));
    register_my_icons();
}

void gtr_actions_add_to_map(GActionMap* map, gpointer callback_user_data)
{
    g_return_if_fail(GTK_IS_APPLICATION_WINDOW(map));

    update_entry_states(win_entries, G_N_ELEMENTS(win_entries));
    g_action_map_add_action_entries(map, win_entries, G_N_ELEMENTS(win_entries), callback_user_data);
}

/****
*****
****/

static GAction* get_action(char const* name)
{
    GtkApplication* app = GTK_APPLICATION(g_application_get_default());
    GList* windows = gtk_application_get_windows(app);

    if (windows->data != NULL)
    {
        return g_action_map_lookup_action(G_ACTION_MAP(windows->data), name);
    }

    return NULL;
}

void gtr_action_activate(char const* name)
{
    GAction* action = get_action(name);

    g_assert(action != NULL);
    g_action_activate(action, NULL);
}

void gtr_action_set_sensitive(char const* name, gboolean b)
{
    GAction* action = get_action(name);

    g_assert(action != NULL);
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), b);
}

void gtr_action_set_toggled(char const* name, gboolean b)
{
    GSimpleAction* action = G_SIMPLE_ACTION(get_action(name));

    g_simple_action_set_state(action, g_variant_new_boolean(b));
}

GMenuModel* gtr_action_get_menu_model(char const* id)
{
    static GtkBuilder* builder = NULL;

    if (builder == NULL)
    {
        builder = gtk_builder_new();
        gtk_builder_add_from_resource(builder, TR_RESOURCE_PATH "transmission-menus.ui", NULL);
    }

    return G_MENU_MODEL(gtk_builder_get_object(builder, id));
}
