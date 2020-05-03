/******************************************************************************
 * Copyright (c) Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <locale.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h> /* exit() */
#include <time.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "actions.h"
#include "conf.h"
#include "details.h"
#include "dialogs.h"
#include "hig.h"
#include "makemeta-ui.h"
#include "msgwin.h"
#include "notify.h"
#include "open-dialog.h"
#include "relocate.h"
#include "stats.h"
#include "tr-core.h"
#include "tr-icon.h"
#include "tr-prefs.h"
#include "tr-window.h"
#include "util.h"

#define MY_CONFIG_NAME "transmission"
#define MY_READABLE_NAME "transmission-gtk"

#define SHOW_LICENSE
static char const* LICENSE =
    "Copyright 2005-2020. All code is copyrighted by the respective authors.\n"
    "\n"
    "Transmission can be redistributed and/or modified under the terms of the "
    "GNU GPL versions 2 or 3 or by any future license endorsed by Mnemosyne LLC.\n"
    "\n"
    "In addition, linking to and/or using OpenSSL is allowed.\n"
    "\n"
    "This program is distributed in the hope that it will be useful, "
    "but WITHOUT ANY WARRANTY; without even the implied warranty of "
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
    "\n"
    "Some of Transmission's source files have more permissive licenses. "
    "Those files may, of course, be used on their own under their own terms.\n";

struct cbdata
{
    char* config_dir;
    gboolean start_paused;
    gboolean is_iconified;
    gboolean is_closing;

    guint activation_count;
    guint timer;
    guint update_model_soon_tag;
    guint refresh_actions_tag;
    gpointer icon;
    GtkWindow* wind;
    TrCore* core;
    GtkWidget* msgwin;
    GtkWidget* prefs;
    GSList* error_list;
    GSList* duplicates_list;
    GSList* details;
    GtkTreeSelection* sel;
};

static void gtr_window_present(GtkWindow* window)
{
    gtk_window_present_with_time(window, gtk_get_current_event_time());
}

/***
****
****  DETAILS DIALOGS MANAGEMENT
****
***/

static int compare_integers(gconstpointer a, gconstpointer b)
{
    return GPOINTER_TO_INT(a) - GPOINTER_TO_INT(b);
}

static char* get_details_dialog_key(GSList* id_list)
{
    GSList* tmp = g_slist_sort(g_slist_copy(id_list), compare_integers);
    GString* gstr = g_string_new(NULL);

    for (GSList* l = tmp; l != NULL; l = l->next)
    {
        g_string_append_printf(gstr, "%d ", GPOINTER_TO_INT(l->data));
    }

    g_slist_free(tmp);
    return g_string_free(gstr, FALSE);
}

static void get_selected_torrent_ids_foreach(GtkTreeModel* model, GtkTreePath* p UNUSED, GtkTreeIter* iter, gpointer gdata)
{
    int id;
    GSList** ids = gdata;
    gtk_tree_model_get(model, iter, MC_TORRENT_ID, &id, -1);
    *ids = g_slist_append(*ids, GINT_TO_POINTER(id));
}

static GSList* get_selected_torrent_ids(struct cbdata* data)
{
    GSList* ids = NULL;
    gtk_tree_selection_selected_foreach(data->sel, get_selected_torrent_ids_foreach, &ids);
    return ids;
}

static void on_details_dialog_closed(gpointer gdata, GObject* dead)
{
    struct cbdata* data = gdata;

    data->details = g_slist_remove(data->details, dead);
}

static void show_details_dialog_for_selected_torrents(struct cbdata* data)
{
    GtkWidget* dialog = NULL;
    GSList* ids = get_selected_torrent_ids(data);
    char* key = get_details_dialog_key(ids);

    for (GSList* l = data->details; dialog == NULL && l != NULL; l = l->next)
    {
        if (g_strcmp0(key, g_object_get_data(l->data, "key")) == 0)
        {
            dialog = l->data;
        }
    }

    if (dialog == NULL)
    {
        dialog = gtr_torrent_details_dialog_new(GTK_WINDOW(data->wind), data->core);
        gtr_torrent_details_dialog_set_torrents(dialog, ids);
        g_object_set_data_full(G_OBJECT(dialog), "key", g_strdup(key), g_free);
        g_object_weak_ref(G_OBJECT(dialog), on_details_dialog_closed, data);
        data->details = g_slist_append(data->details, dialog);
        gtk_widget_show(dialog);
    }

    gtr_window_present(GTK_WINDOW(dialog));
    g_free(key);
    g_slist_free(ids);
}

/****
*****
*****  ON SELECTION CHANGED
*****
****/

struct counts_data
{
    int total_count;
    int queued_count;
    int stopped_count;
};

static void get_selected_torrent_counts_foreach(GtkTreeModel* model, GtkTreePath* path UNUSED, GtkTreeIter* iter,
    gpointer user_data)
{
    int activity = 0;
    struct counts_data* counts = user_data;

    ++counts->total_count;

    gtk_tree_model_get(model, iter, MC_ACTIVITY, &activity, -1);

    if (activity == TR_STATUS_DOWNLOAD_WAIT || activity == TR_STATUS_SEED_WAIT)
    {
        ++counts->queued_count;
    }

    if (activity == TR_STATUS_STOPPED)
    {
        ++counts->stopped_count;
    }
}

static void get_selected_torrent_counts(struct cbdata* data, struct counts_data* counts)
{
    counts->total_count = 0;
    counts->queued_count = 0;
    counts->stopped_count = 0;

    gtk_tree_selection_selected_foreach(data->sel, get_selected_torrent_counts_foreach, counts);
}

static void count_updatable_foreach(GtkTreeModel* model, GtkTreePath* path UNUSED, GtkTreeIter* iter,
    gpointer accumulated_status)
{
    tr_torrent* tor;
    gtk_tree_model_get(model, iter, MC_TORRENT, &tor, -1);
    *(int*)accumulated_status |= tr_torrentCanManualUpdate(tor);
}

static gboolean refresh_actions(gpointer gdata)
{
    struct cbdata* data = gdata;

    if (!data->is_closing)
    {
        int canUpdate;
        struct counts_data sel_counts;
        size_t const total = gtr_core_get_torrent_count(data->core);
        size_t const active = gtr_core_get_active_torrent_count(data->core);
        int const torrent_count = gtk_tree_model_iter_n_children(gtr_core_model(data->core), NULL);
        bool has_selection;

        get_selected_torrent_counts(data, &sel_counts);
        has_selection = sel_counts.total_count > 0;

        gtr_action_set_sensitive("select-all", torrent_count != 0);
        gtr_action_set_sensitive("deselect-all", torrent_count != 0);
        gtr_action_set_sensitive("pause-all-torrents", active != 0);
        gtr_action_set_sensitive("start-all-torrents", active != total);

        gtr_action_set_sensitive("torrent-stop", (sel_counts.stopped_count < sel_counts.total_count));
        gtr_action_set_sensitive("torrent-start", (sel_counts.stopped_count) > 0);
        gtr_action_set_sensitive("torrent-start-now", (sel_counts.stopped_count + sel_counts.queued_count) > 0);
        gtr_action_set_sensitive("torrent-verify", has_selection);
        gtr_action_set_sensitive("remove-torrent", has_selection);
        gtr_action_set_sensitive("delete-torrent", has_selection);
        gtr_action_set_sensitive("relocate-torrent", has_selection);
        gtr_action_set_sensitive("queue-move-top", has_selection);
        gtr_action_set_sensitive("queue-move-up", has_selection);
        gtr_action_set_sensitive("queue-move-down", has_selection);
        gtr_action_set_sensitive("queue-move-bottom", has_selection);
        gtr_action_set_sensitive("show-torrent-properties", has_selection);
        gtr_action_set_sensitive("open-torrent-folder", sel_counts.total_count == 1);
        gtr_action_set_sensitive("copy-magnet-link-to-clipboard", sel_counts.total_count == 1);

        canUpdate = 0;
        gtk_tree_selection_selected_foreach(data->sel, count_updatable_foreach, &canUpdate);
        gtr_action_set_sensitive("torrent-reannounce", canUpdate != 0);
    }

    data->refresh_actions_tag = 0;
    return G_SOURCE_REMOVE;
}

static void refresh_actions_soon(gpointer gdata)
{
    struct cbdata* data = gdata;

    if (!data->is_closing && data->refresh_actions_tag == 0)
    {
        data->refresh_actions_tag = gdk_threads_add_idle(refresh_actions, data);
    }
}

static void on_selection_changed(GtkTreeSelection* s UNUSED, gpointer gdata)
{
    refresh_actions_soon(gdata);
}

/***
****
***/

static gboolean has_magnet_link_handler(void)
{
    GAppInfo* app_info = g_app_info_get_default_for_uri_scheme("magnet");
    gboolean const has_handler = app_info != NULL;
    g_clear_object(&app_info);
    return has_handler;
}

static void register_magnet_link_handler(void)
{
    GError* error;
    GAppInfo* app;
    char const* const content_type = "x-scheme-handler/magnet";

    error = NULL;
    app = g_app_info_create_from_commandline("transmission-gtk", "transmission-gtk", G_APP_INFO_CREATE_SUPPORTS_URIS, &error);
    g_app_info_set_as_default_for_type(app, content_type, &error);

    if (error != NULL)
    {
        g_warning(_("Error registering Transmission as a %s handler: %s"), content_type, error->message);
        g_error_free(error);
    }

    g_clear_object(&app);
}

static void ensure_magnet_handler_exists(void)
{
    if (!has_magnet_link_handler())
    {
        register_magnet_link_handler();
    }
}

static void on_main_window_size_allocated(GtkWidget* gtk_window, GtkAllocation* alloc UNUSED, gpointer gdata UNUSED)
{
    GdkWindow* gdk_window = gtk_widget_get_window(gtk_window);
    gboolean const isMaximized = gdk_window != NULL && (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_MAXIMIZED) != 0;

    gtr_pref_int_set(TR_KEY_main_window_is_maximized, isMaximized);

    if (!isMaximized)
    {
        int x;
        int y;
        int w;
        int h;
        gtk_window_get_position(GTK_WINDOW(gtk_window), &x, &y);
        gtk_window_get_size(GTK_WINDOW(gtk_window), &w, &h);
        gtr_pref_int_set(TR_KEY_main_window_x, x);
        gtr_pref_int_set(TR_KEY_main_window_y, y);
        gtr_pref_int_set(TR_KEY_main_window_width, w);
        gtr_pref_int_set(TR_KEY_main_window_height, h);
    }
}

/***
**** listen to changes that come from RPC
***/

struct on_rpc_changed_struct
{
    TrCore* core;
    tr_rpc_callback_type type;
    int torrent_id;
};

static gboolean on_rpc_changed_idle(gpointer gdata)
{
    tr_torrent* tor;
    struct on_rpc_changed_struct* data = gdata;

    switch (data->type)
    {
    case TR_RPC_SESSION_CLOSE:
        gtr_action_activate("quit");
        break;

    case TR_RPC_TORRENT_ADDED:
        if ((tor = gtr_core_find_torrent(data->core, data->torrent_id)) != NULL)
        {
            gtr_core_add_torrent(data->core, tor, true);
        }

        break;

    case TR_RPC_TORRENT_REMOVING:
        gtr_core_remove_torrent(data->core, data->torrent_id, false);
        break;

    case TR_RPC_TORRENT_TRASHING:
        gtr_core_remove_torrent(data->core, data->torrent_id, true);
        break;

    case TR_RPC_SESSION_CHANGED:
        {
            tr_variant tmp;
            tr_variant* newval;
            tr_variant* oldvals = gtr_pref_get_all();
            tr_quark key;
            GSList* changed_keys = NULL;
            tr_session* session = gtr_core_session(data->core);
            tr_variantInitDict(&tmp, 100);
            tr_sessionGetSettings(session, &tmp);

            for (int i = 0; tr_variantDictChild(&tmp, i, &key, &newval); ++i)
            {
                bool changed;
                tr_variant* oldval = tr_variantDictFind(oldvals, key);

                if (oldval == NULL)
                {
                    changed = true;
                }
                else
                {
                    char* a = tr_variantToStr(oldval, TR_VARIANT_FMT_BENC, NULL);
                    char* b = tr_variantToStr(newval, TR_VARIANT_FMT_BENC, NULL);
                    changed = g_strcmp0(a, b) != 0;
                    tr_free(b);
                    tr_free(a);
                }

                if (changed)
                {
                    changed_keys = g_slist_append(changed_keys, GINT_TO_POINTER(key));
                }
            }

            tr_sessionGetSettings(session, oldvals);

            for (GSList* l = changed_keys; l != NULL; l = l->next)
            {
                gtr_core_pref_changed(data->core, GPOINTER_TO_INT(l->data));
            }

            g_slist_free(changed_keys);
            tr_variantFree(&tmp);
            break;
        }

    case TR_RPC_TORRENT_CHANGED:
    case TR_RPC_TORRENT_MOVED:
    case TR_RPC_TORRENT_STARTED:
    case TR_RPC_TORRENT_STOPPED:
    case TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED:
        /* nothing interesting to do here */
        break;
    }

    g_free(data);
    return G_SOURCE_REMOVE;
}

static tr_rpc_callback_status on_rpc_changed(tr_session* session G_GNUC_UNUSED, tr_rpc_callback_type type,
    struct tr_torrent* tor, void* gdata)
{
    struct cbdata* cbdata = gdata;
    struct on_rpc_changed_struct* data;

    data = g_new(struct on_rpc_changed_struct, 1);
    data->core = cbdata->core;
    data->type = type;
    data->torrent_id = tr_torrentId(tor);
    gdk_threads_add_idle(on_rpc_changed_idle, data);

    return TR_RPC_NOREMOVE;
}

/***
****  signal handling
***/

static sig_atomic_t global_sigcount = 0;
static struct cbdata* sighandler_cbdata = NULL;

static void signal_handler(int sig)
{
    if (++global_sigcount > 1)
    {
        signal(sig, SIG_DFL);
        raise(sig);
    }
    else if (sig == SIGINT || sig == SIGTERM)
    {
        g_message(_("Got signal %d; trying to shut down cleanly. Do it again if it gets stuck."), sig);
        gtr_actions_handler("quit", sighandler_cbdata);
    }
}

/****
*****
*****
****/

static void app_setup(GtkWindow* wind, struct cbdata* cbdata);

static void on_startup(GApplication* application, gpointer user_data)
{
    GError* error;
    char const* str;
    GtkWindow* win;
    GtkUIManager* ui_manager;
    tr_session* session;
    struct cbdata* cbdata = user_data;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    sighandler_cbdata = cbdata;

    /* ensure the directories are created */
    if ((str = gtr_pref_string_get(TR_KEY_download_dir)) != NULL)
    {
        g_mkdir_with_parents(str, 0777);
    }

    if ((str = gtr_pref_string_get(TR_KEY_incomplete_dir)) != NULL)
    {
        g_mkdir_with_parents(str, 0777);
    }

    /* initialize the libtransmission session */
    session = tr_sessionInit(cbdata->config_dir, TRUE, gtr_pref_get_all());

    gtr_pref_flag_set(TR_KEY_alt_speed_enabled, tr_sessionUsesAltSpeed(session));
    gtr_pref_int_set(TR_KEY_peer_port, tr_sessionGetPeerPort(session));
    cbdata->core = gtr_core_new(session);

    /* init the ui manager */
    error = NULL;
    ui_manager = gtk_ui_manager_new();
    gtr_actions_init(ui_manager, cbdata);
    gtk_ui_manager_add_ui_from_resource(ui_manager, TR_RESOURCE_PATH "transmission-ui.xml", &error);
    g_assert_no_error(error);
    gtk_ui_manager_ensure_update(ui_manager);

    /* create main window now to be a parent to any error dialogs */
    win = GTK_WINDOW(gtr_window_new(GTK_APPLICATION(application), ui_manager, cbdata->core));
    g_signal_connect(win, "size-allocate", G_CALLBACK(on_main_window_size_allocated), cbdata);
    g_application_hold(application);
    g_object_weak_ref(G_OBJECT(win), (GWeakNotify)(GCallback)g_application_release, application);
    app_setup(win, cbdata);
    tr_sessionSetRPCCallback(session, on_rpc_changed, cbdata);

    /* check & see if it's time to update the blocklist */
    if (gtr_pref_flag_get(TR_KEY_blocklist_enabled))
    {
        if (gtr_pref_flag_get(TR_KEY_blocklist_updates_enabled))
        {
            int64_t const last_time = gtr_pref_int_get(TR_KEY_blocklist_date);
            int const SECONDS_IN_A_WEEK = 7 * 24 * 60 * 60;
            time_t const now = time(NULL);

            if (last_time + SECONDS_IN_A_WEEK < now)
            {
                gtr_core_blocklist_update(cbdata->core);
            }
        }
    }

    /* if there's no magnet link handler registered, register us */
    ensure_magnet_handler_exists();
}

static void on_activate(GApplication* app UNUSED, struct cbdata* cbdata)
{
    cbdata->activation_count++;

    /* GApplication emits an 'activate' signal when bootstrapping the primary.
     * Ordinarily we handle that by presenting the main window, but if the user
     * user started Transmission minimized, ignore that initial signal... */
    if (cbdata->is_iconified && cbdata->activation_count == 1)
    {
        return;
    }

    gtr_action_activate("present-main-window");
}

static void open_files(GSList* files, gpointer gdata)
{
    struct cbdata* cbdata = gdata;
    gboolean const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents) && !cbdata->start_paused;
    gboolean const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);
    gboolean const do_notify = TRUE;

    gtr_core_add_files(cbdata->core, files, do_start, do_prompt, do_notify);
}

static void on_open(GApplication* application UNUSED, GFile** f, gint file_count, gchar* hint UNUSED, gpointer gdata)
{
    GSList* files = NULL;

    for (gint i = 0; i < file_count; i++)
    {
        files = g_slist_prepend(files, f[i]);
    }

    open_files(files, gdata);

    g_slist_free(files);
}

/***
****
***/

int main(int argc, char** argv)
{
    int ret;
    struct stat sb;
    char* application_id;
    GtkApplication* app;
    GOptionContext* option_context;
    bool show_version = false;
    GError* error = NULL;
    struct cbdata cbdata;

    GOptionEntry option_entries[] =
    {
        { "config-dir", 'g', 0, G_OPTION_ARG_FILENAME, &cbdata.config_dir, _("Where to look for configuration files"), NULL },
        { "paused", 'p', 0, G_OPTION_ARG_NONE, &cbdata.start_paused, _("Start with all torrents paused"), NULL },
        { "minimized", 'm', 0, G_OPTION_ARG_NONE, &cbdata.is_iconified, _("Start minimized in notification area"), NULL },
        { "version", 'v', 0, G_OPTION_ARG_NONE, &show_version, _("Show version number and exit"), NULL },
        { NULL, 0, 0, 0, NULL, NULL, NULL }
    };

    /* default settings */
    memset(&cbdata, 0, sizeof(struct cbdata));
    cbdata.config_dir = (char*)tr_getDefaultConfigDir(MY_CONFIG_NAME);

    /* init i18n */
    setlocale(LC_ALL, "");
    bindtextdomain(MY_READABLE_NAME, TRANSMISSIONLOCALEDIR);
    bind_textdomain_codeset(MY_READABLE_NAME, "UTF-8");
    textdomain(MY_READABLE_NAME);

    /* init glib/gtk */
#if !GLIB_CHECK_VERSION(2, 35, 4)
    g_type_init();
#endif
    g_set_application_name(_("Transmission"));

    /* parse the command line */
    option_context = g_option_context_new(_("[torrent files or urls]"));
    g_option_context_add_main_entries(option_context, option_entries, GETTEXT_PACKAGE);
    g_option_context_add_group(option_context, gtk_get_option_group(FALSE));
    g_option_context_set_translation_domain(option_context, GETTEXT_PACKAGE);

    if (!g_option_context_parse(option_context, &argc, &argv, &error))
    {
        g_print(_("%s\nRun '%s --help' to see a full list of available command line options.\n"), error->message, argv[0]);
        g_error_free(error);
        g_option_context_free(option_context);
        return 1;
    }

    g_option_context_free(option_context);

    /* handle the trivial "version" option */
    if (show_version)
    {
        fprintf(stderr, "%s %s\n", MY_READABLE_NAME, LONG_VERSION_STRING);
        return 0;
    }

    gtk_window_set_default_icon_name(MY_CONFIG_NAME);

    /* init the unit formatters */
    tr_formatter_mem_init(mem_K, _(mem_K_str), _(mem_M_str), _(mem_G_str), _(mem_T_str));
    tr_formatter_size_init(disk_K, _(disk_K_str), _(disk_M_str), _(disk_G_str), _(disk_T_str));
    tr_formatter_speed_init(speed_K, _(speed_K_str), _(speed_M_str), _(speed_G_str), _(speed_T_str));

    /* set up the config dir */
    gtr_pref_init(cbdata.config_dir);
    g_mkdir_with_parents(cbdata.config_dir, 0755);

    /* init notifications */
    gtr_notify_init();

    /* init the application for the specified config dir */
    stat(cbdata.config_dir, &sb);
    application_id = g_strdup_printf("com.transmissionbt.transmission_%lu_%lu", (unsigned long)sb.st_dev,
        (unsigned long)sb.st_ino);
    app = gtk_application_new(application_id, G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "open", G_CALLBACK(on_open), &cbdata);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), &cbdata);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &cbdata);
    ret = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    g_free(application_id);
    return ret;
}

static void on_core_busy(TrCore* core UNUSED, gboolean busy, struct cbdata* c)
{
    gtr_window_set_busy(c->wind, busy);
}

static void on_core_error(TrCore*, guint, char const*, struct cbdata*);
static void on_add_torrent(TrCore*, tr_ctor*, gpointer);
static void on_prefs_changed(TrCore* core, tr_quark const key, gpointer);
static void main_window_setup(struct cbdata* cbdata, GtkWindow* wind);
static gboolean update_model_loop(gpointer gdata);
static gboolean update_model_once(gpointer gdata);

static void app_setup(GtkWindow* wind, struct cbdata* cbdata)
{
    if (cbdata->is_iconified)
    {
        gtr_pref_flag_set(TR_KEY_show_notification_area_icon, TRUE);
    }

    gtr_actions_set_core(cbdata->core);

    /* set up core handlers */
    g_signal_connect(cbdata->core, "busy", G_CALLBACK(on_core_busy), cbdata);
    g_signal_connect(cbdata->core, "add-error", G_CALLBACK(on_core_error), cbdata);
    g_signal_connect(cbdata->core, "add-prompt", G_CALLBACK(on_add_torrent), cbdata);
    g_signal_connect(cbdata->core, "prefs-changed", G_CALLBACK(on_prefs_changed), cbdata);

    /* add torrents from command-line and saved state */
    gtr_core_load(cbdata->core, cbdata->start_paused);
    gtr_core_torrents_added(cbdata->core);

    /* set up main window */
    main_window_setup(cbdata, wind);

    /* set up the icon */
    on_prefs_changed(cbdata->core, TR_KEY_show_notification_area_icon, cbdata);

    /* start model update timer */
    cbdata->timer = gdk_threads_add_timeout_seconds(MAIN_WINDOW_REFRESH_INTERVAL_SECONDS, update_model_loop, cbdata);
    update_model_once(cbdata);

    /* either show the window or iconify it */
    if (!cbdata->is_iconified)
    {
        gtk_widget_show(GTK_WIDGET(wind));
    }
    else
    {
        gtk_window_set_skip_taskbar_hint(cbdata->wind, cbdata->icon != NULL);
        cbdata->is_iconified = FALSE; // ensure that the next toggle iconifies
        gtr_action_set_toggled("toggle-main-window", FALSE);
    }

    if (!gtr_pref_flag_get(TR_KEY_user_has_given_informed_consent))
    {
        GtkWidget* w = gtk_message_dialog_new(GTK_WINDOW(wind), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_OTHER,
            GTK_BUTTONS_NONE, "%s", _("Transmission is a file sharing program. When you run a torrent, its data will be "
            "made available to others by means of upload. Any content you share is your sole responsibility."));
        gtk_dialog_add_button(GTK_DIALOG(w), GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT);
        gtk_dialog_add_button(GTK_DIALOG(w), _("I _Agree"), GTK_RESPONSE_ACCEPT);
        gtk_dialog_set_default_response(GTK_DIALOG(w), GTK_RESPONSE_ACCEPT);

        switch (gtk_dialog_run(GTK_DIALOG(w)))
        {
        case GTK_RESPONSE_ACCEPT:
            /* only show it once */
            gtr_pref_flag_set(TR_KEY_user_has_given_informed_consent, TRUE);
            gtk_widget_destroy(w);
            break;

        default:
            exit(0);
        }
    }
}

static void presentMainWindow(struct cbdata* cbdata)
{
    GtkWindow* window = cbdata->wind;

    if (cbdata->is_iconified)
    {
        cbdata->is_iconified = false;

        gtk_window_set_skip_taskbar_hint(window, FALSE);
    }

    if (!gtk_widget_get_visible(GTK_WIDGET(window)))
    {
        gtk_window_resize(window, gtr_pref_int_get(TR_KEY_main_window_width), gtr_pref_int_get(TR_KEY_main_window_height));
        gtk_window_move(window, gtr_pref_int_get(TR_KEY_main_window_x), gtr_pref_int_get(TR_KEY_main_window_y));
        gtr_widget_set_visible(GTK_WIDGET(window), TRUE);
    }

    gtr_window_present(window);
    gdk_window_raise(gtk_widget_get_window(GTK_WIDGET(window)));
}

static void hideMainWindow(struct cbdata* cbdata)
{
    GtkWindow* window = cbdata->wind;
    gtk_window_set_skip_taskbar_hint(window, TRUE);
    gtr_widget_set_visible(GTK_WIDGET(window), FALSE);
    cbdata->is_iconified = true;
}

static void toggleMainWindow(struct cbdata* cbdata)
{
    if (cbdata->is_iconified)
    {
        presentMainWindow(cbdata);
    }
    else
    {
        hideMainWindow(cbdata);
    }
}

static void on_app_exit(gpointer vdata);

static gboolean winclose(GtkWidget* w UNUSED, GdkEvent* event UNUSED, gpointer gdata)
{
    struct cbdata* cbdata = gdata;

    if (cbdata->icon != NULL)
    {
        gtr_action_activate("toggle-main-window");
    }
    else
    {
        on_app_exit(cbdata);
    }

    return TRUE; /* don't propagate event further */
}

static void rowChangedCB(GtkTreeModel* model UNUSED, GtkTreePath* path, GtkTreeIter* iter UNUSED, gpointer gdata)
{
    struct cbdata* data = gdata;

    if (gtk_tree_selection_path_is_selected(data->sel, path))
    {
        refresh_actions_soon(data);
    }
}

static void on_drag_data_received(GtkWidget* widget UNUSED, GdkDragContext* drag_context, gint x UNUSED, gint y UNUSED,
    GtkSelectionData* selection_data, guint info UNUSED, guint time_, gpointer gdata)
{
    char** uris = gtk_selection_data_get_uris(selection_data);
    guint const file_count = g_strv_length(uris);
    GSList* files = NULL;

    for (guint i = 0; i < file_count; ++i)
    {
        files = g_slist_prepend(files, g_file_new_for_uri(uris[i]));
    }

    open_files(files, gdata);

    /* cleanup */
    g_slist_foreach(files, (GFunc)(GCallback)g_object_unref, NULL);
    g_slist_free(files);
    g_strfreev(uris);

    gtk_drag_finish(drag_context, true, FALSE, time_);
}

static void main_window_setup(struct cbdata* cbdata, GtkWindow* wind)
{
    GtkWidget* w;
    GtkTreeModel* model;
    GtkTreeSelection* sel;

    g_assert(NULL == cbdata->wind);
    cbdata->wind = wind;
    cbdata->sel = sel = GTK_TREE_SELECTION(gtr_window_get_selection(cbdata->wind));

    g_signal_connect(sel, "changed", G_CALLBACK(on_selection_changed), cbdata);
    on_selection_changed(sel, cbdata);
    model = gtr_core_model(cbdata->core);
    g_signal_connect(model, "row-changed", G_CALLBACK(rowChangedCB), cbdata);
    g_signal_connect(wind, "delete-event", G_CALLBACK(winclose), cbdata);
    refresh_actions(cbdata);

    /* register to handle URIs that get dragged onto our main window */
    w = GTK_WIDGET(wind);
    gtk_drag_dest_set(w, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(w);
    g_signal_connect(w, "drag-data-received", G_CALLBACK(on_drag_data_received), cbdata);
}

static gboolean on_session_closed(gpointer gdata)
{
    GSList* tmp;
    struct cbdata* cbdata = gdata;

    tmp = g_slist_copy(cbdata->details);
    g_slist_foreach(tmp, (GFunc)(GCallback)gtk_widget_destroy, NULL);
    g_slist_free(tmp);

    if (cbdata->prefs != NULL)
    {
        gtk_widget_destroy(GTK_WIDGET(cbdata->prefs));
    }

    if (cbdata->wind != NULL)
    {
        gtk_widget_destroy(GTK_WIDGET(cbdata->wind));
    }

    g_object_unref(cbdata->core);

    if (cbdata->icon != NULL)
    {
        g_object_unref(cbdata->icon);
    }

    g_slist_foreach(cbdata->error_list, (GFunc)(GCallback)g_free, NULL);
    g_slist_free(cbdata->error_list);
    g_slist_foreach(cbdata->duplicates_list, (GFunc)(GCallback)g_free, NULL);
    g_slist_free(cbdata->duplicates_list);

    return G_SOURCE_REMOVE;
}

struct session_close_struct
{
    tr_session* session;
    struct cbdata* cbdata;
};

/* since tr_sessionClose () is a blocking function,
 * delegate its call to another thread here... when it's done,
 * punt the GUI teardown back to the GTK+ thread */
static gpointer session_close_threadfunc(gpointer gdata)
{
    struct session_close_struct* data = gdata;
    tr_sessionClose(data->session);
    gdk_threads_add_idle(on_session_closed, data->cbdata);
    g_free(data);
    return NULL;
}

static void exit_now_cb(GtkWidget* w UNUSED, gpointer data UNUSED)
{
    exit(0);
}

static void on_app_exit(gpointer vdata)
{
    GtkWidget* p;
    GtkWidget* w;
    GtkWidget* c;
    struct cbdata* cbdata = vdata;
    struct session_close_struct* session_close_data;

    if (cbdata->is_closing)
    {
        return;
    }

    cbdata->is_closing = true;

    /* stop the update timer */
    if (cbdata->timer != 0)
    {
        g_source_remove(cbdata->timer);
        cbdata->timer = 0;
    }

    /* stop the refresh-actions timer */
    if (cbdata->refresh_actions_tag != 0)
    {
        g_source_remove(cbdata->refresh_actions_tag);
        cbdata->refresh_actions_tag = 0;
    }

    c = GTK_WIDGET(cbdata->wind);
    gtk_container_remove(GTK_CONTAINER(c), gtk_bin_get_child(GTK_BIN(c)));

    p =
        g_object_new(GTK_TYPE_GRID, "column-spacing", GUI_PAD_BIG, "halign", GTK_ALIGN_CENTER, "valign", GTK_ALIGN_CENTER,
        NULL);
    gtk_container_add(GTK_CONTAINER(c), p);

    w = gtk_image_new_from_icon_name(GTK_STOCK_NETWORK, GTK_ICON_SIZE_DIALOG);
    gtk_grid_attach(GTK_GRID(p), w, 0, 0, 1, 2);

    w = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(w), _("<b>Closing Connections</b>"));
    g_object_set(w, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    gtk_grid_attach(GTK_GRID(p), w, 1, 0, 1, 1);

    w = gtk_label_new(_("Sending upload/download totals to tracker…"));
    g_object_set(w, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_CENTER, NULL);
    gtk_grid_attach(GTK_GRID(p), w, 1, 1, 1, 1);

    w = gtk_button_new_with_mnemonic(_("_Quit Now"));
    g_object_set(w, "margin-top", GUI_PAD, "halign", GTK_ALIGN_START, "valign", GTK_ALIGN_END, NULL);
    g_signal_connect(w, "clicked", G_CALLBACK(exit_now_cb), NULL);
    gtk_grid_attach(GTK_GRID(p), w, 1, 2, 1, 1);

    gtk_widget_show_all(p);
    gtk_widget_grab_focus(w);

    /* clear the UI */
    gtr_core_clear(cbdata->core);

    /* ensure the window is in its previous position & size.
     * this seems to be necessary because changing the main window's
     * child seems to unset the size */
    gtk_window_resize(cbdata->wind, gtr_pref_int_get(TR_KEY_main_window_width), gtr_pref_int_get(TR_KEY_main_window_height));
    gtk_window_move(cbdata->wind, gtr_pref_int_get(TR_KEY_main_window_x), gtr_pref_int_get(TR_KEY_main_window_y));

    /* shut down libT */
    session_close_data = g_new(struct session_close_struct, 1);
    session_close_data->cbdata = cbdata;
    session_close_data->session = gtr_core_close(cbdata->core);
    g_thread_new("shutdown-thread", session_close_threadfunc, session_close_data);
}

static void show_torrent_errors(GtkWindow* window, char const* primary, GSList** files)
{
    GtkWidget* w;
    GString* s = g_string_new(NULL);
    char const* leader = g_slist_length(*files) > 1 ? gtr_get_unicode_string(GTR_UNICODE_BULLET) : "";

    for (GSList* l = *files; l != NULL; l = l->next)
    {
        g_string_append_printf(s, "%s %s\n", leader, (char const*)l->data);
    }

    w = gtk_message_dialog_new(window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", primary);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(w), "%s", s->str);
    g_signal_connect_swapped(w, "response", G_CALLBACK(gtk_widget_destroy), w);
    gtk_widget_show(w);
    g_string_free(s, TRUE);

    g_slist_foreach(*files, (GFunc)(GCallback)g_free, NULL);
    g_slist_free(*files);
    *files = NULL;
}

static void flush_torrent_errors(struct cbdata* cbdata)
{
    if (cbdata->error_list != NULL)
    {
        show_torrent_errors(cbdata->wind, ngettext("Couldn't add corrupt torrent", "Couldn't add corrupt torrents",
            g_slist_length(cbdata->error_list)), &cbdata->error_list);
    }

    if (cbdata->duplicates_list != NULL)
    {
        show_torrent_errors(cbdata->wind, ngettext("Couldn't add duplicate torrent", "Couldn't add duplicate torrents",
            g_slist_length(cbdata->duplicates_list)), &cbdata->duplicates_list);
    }
}

static void on_core_error(TrCore* core UNUSED, guint code, char const* msg, struct cbdata* c)
{
    switch (code)
    {
    case TR_PARSE_ERR:
        c->error_list = g_slist_append(c->error_list, g_path_get_basename(msg));
        break;

    case TR_PARSE_DUPLICATE:
        c->duplicates_list = g_slist_append(c->duplicates_list, g_strdup(msg));
        break;

    case TR_CORE_ERR_NO_MORE_TORRENTS:
        flush_torrent_errors(c);
        break;

    default:
        g_assert_not_reached();
        break;
    }
}

static gboolean on_main_window_focus_in(GtkWidget* widget UNUSED, GdkEventFocus* event UNUSED, gpointer gdata)
{
    struct cbdata* cbdata = gdata;

    if (cbdata->wind != NULL)
    {
        gtk_window_set_urgency_hint(cbdata->wind, FALSE);
    }

    return FALSE;
}

static void on_add_torrent(TrCore* core, tr_ctor* ctor, gpointer gdata)
{
    struct cbdata* cbdata = gdata;
    GtkWidget* w = gtr_torrent_options_dialog_new(cbdata->wind, core, ctor);

    g_signal_connect(w, "focus-in-event", G_CALLBACK(on_main_window_focus_in), cbdata);

    if (cbdata->wind != NULL)
    {
        gtk_window_set_urgency_hint(cbdata->wind, TRUE);
    }

    gtk_widget_show(w);
}

static void on_prefs_changed(TrCore* core UNUSED, tr_quark const key, gpointer data)
{
    struct cbdata* cbdata = data;
    tr_session* tr = gtr_core_session(cbdata->core);

    switch (key)
    {
    case TR_KEY_encryption:
        tr_sessionSetEncryption(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_download_dir:
        tr_sessionSetDownloadDir(tr, gtr_pref_string_get(key));
        break;

    case TR_KEY_message_level:
        tr_logSetLevel(gtr_pref_int_get(key));
        break;

    case TR_KEY_peer_port:
        tr_sessionSetPeerPort(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_blocklist_enabled:
        tr_blocklistSetEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_blocklist_url:
        tr_blocklistSetURL(tr, gtr_pref_string_get(key));
        break;

    case TR_KEY_show_notification_area_icon:
        {
            bool const show = gtr_pref_flag_get(key);

            if (show && cbdata->icon == NULL)
            {
                cbdata->icon = gtr_icon_new(cbdata->core);
            }
            else if (!show && cbdata->icon != NULL)
            {
                g_clear_object(&cbdata->icon);
            }

            break;
        }

    case TR_KEY_speed_limit_down_enabled:
        tr_sessionLimitSpeed(tr, TR_DOWN, gtr_pref_flag_get(key));
        break;

    case TR_KEY_speed_limit_down:
        tr_sessionSetSpeedLimit_KBps(tr, TR_DOWN, gtr_pref_int_get(key));
        break;

    case TR_KEY_speed_limit_up_enabled:
        tr_sessionLimitSpeed(tr, TR_UP, gtr_pref_flag_get(key));
        break;

    case TR_KEY_speed_limit_up:
        tr_sessionSetSpeedLimit_KBps(tr, TR_UP, gtr_pref_int_get(key));
        break;

    case TR_KEY_ratio_limit_enabled:
        tr_sessionSetRatioLimited(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_ratio_limit:
        tr_sessionSetRatioLimit(tr, gtr_pref_double_get(key));
        break;

    case TR_KEY_idle_seeding_limit:
        tr_sessionSetIdleLimit(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_idle_seeding_limit_enabled:
        tr_sessionSetIdleLimited(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_port_forwarding_enabled:
        tr_sessionSetPortForwardingEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_pex_enabled:
        tr_sessionSetPexEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_rename_partial_files:
        tr_sessionSetIncompleteFileNamingEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_download_queue_size:
        tr_sessionSetQueueSize(tr, TR_DOWN, gtr_pref_int_get(key));
        break;

    case TR_KEY_queue_stalled_minutes:
        tr_sessionSetQueueStalledMinutes(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_dht_enabled:
        tr_sessionSetDHTEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_utp_enabled:
        tr_sessionSetUTPEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_lpd_enabled:
        tr_sessionSetLPDEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_rpc_port:
        tr_sessionSetRPCPort(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_rpc_enabled:
        tr_sessionSetRPCEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_rpc_whitelist:
        tr_sessionSetRPCWhitelist(tr, gtr_pref_string_get(key));
        break;

    case TR_KEY_rpc_whitelist_enabled:
        tr_sessionSetRPCWhitelistEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_rpc_username:
        tr_sessionSetRPCUsername(tr, gtr_pref_string_get(key));
        break;

    case TR_KEY_rpc_password:
        tr_sessionSetRPCPassword(tr, gtr_pref_string_get(key));
        break;

    case TR_KEY_rpc_authentication_required:
        tr_sessionSetRPCPasswordEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_alt_speed_up:
        tr_sessionSetAltSpeed_KBps(tr, TR_UP, gtr_pref_int_get(key));
        break;

    case TR_KEY_alt_speed_down:
        tr_sessionSetAltSpeed_KBps(tr, TR_DOWN, gtr_pref_int_get(key));
        break;

    case TR_KEY_alt_speed_enabled:
        {
            bool const b = gtr_pref_flag_get(key);
            tr_sessionUseAltSpeed(tr, b);
            gtr_action_set_toggled(tr_quark_get_string(key, NULL), b);
            break;
        }

    case TR_KEY_alt_speed_time_begin:
        tr_sessionSetAltSpeedBegin(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_alt_speed_time_end:
        tr_sessionSetAltSpeedEnd(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_alt_speed_time_enabled:
        tr_sessionUseAltSpeedTime(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_alt_speed_time_day:
        tr_sessionSetAltSpeedDay(tr, gtr_pref_int_get(key));
        break;

    case TR_KEY_peer_port_random_on_start:
        tr_sessionSetPeerPortRandomOnStart(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_incomplete_dir:
        tr_sessionSetIncompleteDir(tr, gtr_pref_string_get(key));
        break;

    case TR_KEY_incomplete_dir_enabled:
        tr_sessionSetIncompleteDirEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_script_torrent_done_enabled:
        tr_sessionSetTorrentDoneScriptEnabled(tr, gtr_pref_flag_get(key));
        break;

    case TR_KEY_script_torrent_done_filename:
        tr_sessionSetTorrentDoneScript(tr, gtr_pref_string_get(key));
        break;

    case TR_KEY_start_added_torrents:
        tr_sessionSetPaused(tr, !gtr_pref_flag_get(key));
        break;

    case TR_KEY_trash_original_torrent_files:
        tr_sessionSetDeleteSource(tr, gtr_pref_flag_get(key));
        break;

    default:
        break;
    }
}

static gboolean update_model_once(gpointer gdata)
{
    struct cbdata* data = gdata;

    /* update the torrent data in the model */
    gtr_core_update(data->core);

    /* refresh the main window's statusbar and toolbar buttons */
    if (data->wind != NULL)
    {
        gtr_window_refresh(data->wind);
    }

    /* update the actions */
    refresh_actions(data);

    /* update the status tray icon */
    if (data->icon != NULL)
    {
        gtr_icon_refresh(data->icon);
    }

    data->update_model_soon_tag = 0;
    return G_SOURCE_REMOVE;
}

static void update_model_soon(gpointer gdata)
{
    struct cbdata* data = gdata;

    if (data->update_model_soon_tag == 0)
    {
        data->update_model_soon_tag = gdk_threads_add_idle(update_model_once, data);
    }
}

static gboolean update_model_loop(gpointer gdata)
{
    gboolean const done = global_sigcount != 0;

    if (!done)
    {
        update_model_once(gdata);
    }

    return !done;
}

static void show_about_dialog(GtkWindow* parent)
{
    char const* uri = "https://transmissionbt.com/";
    char const* authors[] =
    {
        "Charles Kerr (Backend; GTK+)",
        "Mitchell Livingston (Backend; OS X)",
        "Mike Gelfand",
        NULL
    };

    gtk_show_about_dialog(parent,
        "authors", authors,
        "comments", _("A fast and easy BitTorrent client"),
        "copyright", _("Copyright (c) The Transmission Project"),
        "logo-icon-name", MY_CONFIG_NAME,
        "name", g_get_application_name(),
        /* Translators: translate "translator-credits" as your name
           to have it appear in the credits in the "About"
           dialog */
        "translator-credits", _("translator-credits"),
        "version", LONG_VERSION_STRING,
        "website", uri,
        "website-label", uri,
#ifdef SHOW_LICENSE
        "license", LICENSE,
        "wrap-license", TRUE,
#endif
        NULL);
}

static void append_id_to_benc_list(GtkTreeModel* m, GtkTreePath* path UNUSED, GtkTreeIter* iter, gpointer list)
{
    tr_torrent* tor = NULL;
    gtk_tree_model_get(m, iter, MC_TORRENT, &tor, -1);
    tr_variantListAddInt(list, tr_torrentId(tor));
}

static gboolean call_rpc_for_selected_torrents(struct cbdata* data, char const* method)
{
    tr_variant top;
    tr_variant* args;
    tr_variant* ids;
    gboolean invoked = FALSE;
    GtkTreeSelection* s = data->sel;
    tr_session* session = gtr_core_session(data->core);

    tr_variantInitDict(&top, 2);
    tr_variantDictAddStr(&top, TR_KEY_method, method);
    args = tr_variantDictAddDict(&top, TR_KEY_arguments, 1);
    ids = tr_variantDictAddList(args, TR_KEY_ids, 0);
    gtk_tree_selection_selected_foreach(s, append_id_to_benc_list, ids);

    if (tr_variantListSize(ids) != 0)
    {
        tr_rpc_request_exec_json(session, &top, NULL, NULL);
        invoked = TRUE;
    }

    tr_variantFree(&top);
    return invoked;
}

static void open_folder_foreach(GtkTreeModel* model, GtkTreePath* path UNUSED, GtkTreeIter* iter, gpointer core)
{
    int id;
    gtk_tree_model_get(model, iter, MC_TORRENT_ID, &id, -1);
    gtr_core_open_folder(core, id);
}

static gboolean on_message_window_closed(void)
{
    gtr_action_set_toggled("toggle-message-log", FALSE);
    return FALSE;
}

static void accumulate_selected_torrents(GtkTreeModel* model, GtkTreePath* path UNUSED, GtkTreeIter* iter, gpointer gdata)
{
    int id;
    GSList** data = gdata;

    gtk_tree_model_get(model, iter, MC_TORRENT_ID, &id, -1);
    *data = g_slist_append(*data, GINT_TO_POINTER(id));
}

static void remove_selected(struct cbdata* data, gboolean delete_files)
{
    GSList* l = NULL;

    gtk_tree_selection_selected_foreach(data->sel, accumulate_selected_torrents, &l);

    if (l != NULL)
    {
        gtr_confirm_remove(data->wind, data->core, l, delete_files);
    }
}

static void start_all_torrents(struct cbdata* data)
{
    tr_session* session = gtr_core_session(data->core);
    tr_variant request;

    tr_variantInitDict(&request, 1);
    tr_variantDictAddStr(&request, TR_KEY_method, "torrent-start");
    tr_rpc_request_exec_json(session, &request, NULL, NULL);
    tr_variantFree(&request);
}

static void pause_all_torrents(struct cbdata* data)
{
    tr_session* session = gtr_core_session(data->core);
    tr_variant request;

    tr_variantInitDict(&request, 1);
    tr_variantDictAddStr(&request, TR_KEY_method, "torrent-stop");
    tr_rpc_request_exec_json(session, &request, NULL, NULL);
    tr_variantFree(&request);
}

static tr_torrent* get_first_selected_torrent(struct cbdata* data)
{
    tr_torrent* tor = NULL;
    GtkTreeModel* m;
    GList* l = gtk_tree_selection_get_selected_rows(data->sel, &m);

    if (l != NULL)
    {
        GtkTreePath* p = l->data;
        GtkTreeIter i;

        if (gtk_tree_model_get_iter(m, &i, p))
        {
            gtk_tree_model_get(m, &i, MC_TORRENT, &tor, -1);
        }
    }

    g_list_foreach(l, (GFunc)(GCallback)gtk_tree_path_free, NULL);
    g_list_free(l);
    return tor;
}

static void copy_magnet_link_to_clipboard(GtkWidget* w, tr_torrent* tor)
{
    char* magnet = tr_torrentGetMagnetLink(tor);
    GdkDisplay* display = gtk_widget_get_display(w);
    GdkAtom selection;
    GtkClipboard* clipboard;

    /* this is The Right Thing for copy/paste... */
    selection = GDK_SELECTION_CLIPBOARD;
    clipboard = gtk_clipboard_get_for_display(display, selection);
    gtk_clipboard_set_text(clipboard, magnet, -1);

    /* ...but people using plain ol' X need this instead */
    selection = GDK_SELECTION_PRIMARY;
    clipboard = gtk_clipboard_get_for_display(display, selection);
    gtk_clipboard_set_text(clipboard, magnet, -1);

    /* cleanup */
    tr_free(magnet);
}

void gtr_actions_handler(char const* action_name, gpointer user_data)
{
    gboolean changed = FALSE;
    struct cbdata* data = user_data;

    if (g_strcmp0(action_name, "open-torrent-from-url") == 0)
    {
        GtkWidget* w = gtr_torrent_open_from_url_dialog_new(data->wind, data->core);
        gtk_widget_show(w);
    }
    else if (g_strcmp0(action_name, "open-torrent-menu") == 0 || g_strcmp0(action_name, "open-torrent-toolbar") == 0)
    {
        GtkWidget* w = gtr_torrent_open_from_file_dialog_new(data->wind, data->core);
        gtk_widget_show(w);
    }
    else if (g_strcmp0(action_name, "show-stats") == 0)
    {
        GtkWidget* dialog = gtr_stats_dialog_new(data->wind, data->core);
        gtk_widget_show(dialog);
    }
    else if (g_strcmp0(action_name, "donate") == 0)
    {
        gtr_open_uri("https://transmissionbt.com/donate/");
    }
    else if (g_strcmp0(action_name, "pause-all-torrents") == 0)
    {
        pause_all_torrents(data);
    }
    else if (g_strcmp0(action_name, "start-all-torrents") == 0)
    {
        start_all_torrents(data);
    }
    else if (g_strcmp0(action_name, "copy-magnet-link-to-clipboard") == 0)
    {
        tr_torrent* tor = get_first_selected_torrent(data);

        if (tor != NULL)
        {
            copy_magnet_link_to_clipboard(GTK_WIDGET(data->wind), tor);
        }
    }
    else if (g_strcmp0(action_name, "relocate-torrent") == 0)
    {
        GSList* ids = get_selected_torrent_ids(data);

        if (ids != NULL)
        {
            GtkWindow* parent = data->wind;
            GtkWidget* w = gtr_relocate_dialog_new(parent, data->core, ids);
            gtk_widget_show(w);
        }
    }
    else if (g_strcmp0(action_name, "torrent-start") == 0 || g_strcmp0(action_name, "torrent-start-now") == 0 ||
        g_strcmp0(action_name, "torrent-stop") == 0 || g_strcmp0(action_name, "torrent-reannounce") == 0 ||
        g_strcmp0(action_name, "torrent-verify") == 0 || g_strcmp0(action_name, "queue-move-top") == 0 ||
        g_strcmp0(action_name, "queue-move-up") == 0 || g_strcmp0(action_name, "queue-move-down") == 0 ||
        g_strcmp0(action_name, "queue-move-bottom") == 0)
    {
        changed |= call_rpc_for_selected_torrents(data, action_name);
    }
    else if (g_strcmp0(action_name, "open-torrent-folder") == 0)
    {
        gtk_tree_selection_selected_foreach(data->sel, open_folder_foreach, data->core);
    }
    else if (g_strcmp0(action_name, "show-torrent-properties") == 0)
    {
        show_details_dialog_for_selected_torrents(data);
    }
    else if (g_strcmp0(action_name, "new-torrent") == 0)
    {
        GtkWidget* w = gtr_torrent_creation_dialog_new(data->wind, data->core);
        gtk_widget_show(w);
    }
    else if (g_strcmp0(action_name, "remove-torrent") == 0)
    {
        remove_selected(data, FALSE);
    }
    else if (g_strcmp0(action_name, "delete-torrent") == 0)
    {
        remove_selected(data, TRUE);
    }
    else if (g_strcmp0(action_name, "quit") == 0)
    {
        on_app_exit(data);
    }
    else if (g_strcmp0(action_name, "select-all") == 0)
    {
        gtk_tree_selection_select_all(data->sel);
    }
    else if (g_strcmp0(action_name, "deselect-all") == 0)
    {
        gtk_tree_selection_unselect_all(data->sel);
    }
    else if (g_strcmp0(action_name, "edit-preferences") == 0)
    {
        if (data->prefs == NULL)
        {
            data->prefs = gtr_prefs_dialog_new(data->wind, G_OBJECT(data->core));
            g_signal_connect(data->prefs, "destroy", G_CALLBACK(gtk_widget_destroyed), &data->prefs);
        }

        gtr_window_present(GTK_WINDOW(data->prefs));
    }
    else if (g_strcmp0(action_name, "toggle-message-log") == 0)
    {
        if (data->msgwin == NULL)
        {
            GtkWidget* win = gtr_message_log_window_new(data->wind, data->core);
            g_signal_connect(win, "destroy", G_CALLBACK(on_message_window_closed), NULL);
            data->msgwin = win;
        }
        else
        {
            gtr_action_set_toggled("toggle-message-log", FALSE);
            gtk_widget_destroy(data->msgwin);
            data->msgwin = NULL;
        }
    }
    else if (g_strcmp0(action_name, "show-about-dialog") == 0)
    {
        show_about_dialog(data->wind);
    }
    else if (g_strcmp0(action_name, "help") == 0)
    {
        gtr_open_uri(gtr_get_help_uri());
    }
    else if (g_strcmp0(action_name, "toggle-main-window") == 0)
    {
        toggleMainWindow(data);
    }
    else if (g_strcmp0(action_name, "present-main-window") == 0)
    {
        presentMainWindow(data);
    }
    else
    {
        g_error("Unhandled action: %s", action_name);
    }

    if (changed)
    {
        update_model_soon(data);
    }
}
