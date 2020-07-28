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

#include <math.h> /* pow() */
#include <string.h> /* strlen */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <event2/buffer.h>

#include <libtransmission/transmission.h>
#include <libtransmission/log.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h> /* tr_free */
#include <libtransmission/variant.h>

#include "actions.h"
#include "conf.h"
#include "notify.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

/***
****
***/

enum
{
    ADD_ERROR_SIGNAL,
    ADD_PROMPT_SIGNAL,
    BLOCKLIST_SIGNAL,
    BUSY_SIGNAL,
    PORT_SIGNAL,
    PREFS_SIGNAL,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void core_maybe_inhibit_hibernation(TrCore* core);

typedef struct TrCorePrivate
{
    GFileMonitor* monitor;
    gulong monitor_tag;
    GFile* monitor_dir;
    GSList* monitor_files;
    gulong monitor_idle_tag;

    gboolean adding_from_watch_dir;
    gboolean inhibit_allowed;
    gboolean have_inhibit_cookie;
    gboolean dbus_error;
    guint inhibit_cookie;
    gint busy_count;
    GtkTreeModel* raw_model;
    GtkTreeModel* sorted_model;
    tr_session* session;
    GStringChunk* string_chunk;
}
TrCorePrivate;

static int core_is_disposed(TrCore const* core)
{
    return core == NULL || core->priv->sorted_model == NULL;
}

G_DEFINE_TYPE_WITH_CODE(TrCore, tr_core, G_TYPE_OBJECT, G_ADD_PRIVATE(TrCore));

static void core_dispose(GObject* o)
{
    TrCore* core = TR_CORE(o);

    if (core->priv->sorted_model != NULL)
    {
        g_object_unref(core->priv->sorted_model);
        core->priv->sorted_model = NULL;
        core->priv->raw_model = NULL;
    }

    G_OBJECT_CLASS(tr_core_parent_class)->dispose(o);
}

static void core_finalize(GObject* o)
{
    TrCore* core = TR_CORE(o);

    g_string_chunk_free(core->priv->string_chunk);

    G_OBJECT_CLASS(tr_core_parent_class)->finalize(o);
}

static void tr_core_class_init(TrCoreClass* core_class)
{
    GObjectClass* gobject_class;
    GType core_type = G_TYPE_FROM_CLASS(core_class);

    gobject_class = G_OBJECT_CLASS(core_class);
    gobject_class->dispose = core_dispose;
    gobject_class->finalize = core_finalize;

    signals[ADD_ERROR_SIGNAL] = g_signal_new("add-error", core_type, G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(TrCoreClass, add_error),
        NULL, NULL, g_cclosure_marshal_VOID__UINT_POINTER, G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

    signals[ADD_PROMPT_SIGNAL] = g_signal_new("add-prompt", core_type, G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(TrCoreClass,
        add_prompt), NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

    signals[BUSY_SIGNAL] = g_signal_new("busy", core_type, G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET(TrCoreClass, busy), NULL, NULL,
        g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[BLOCKLIST_SIGNAL] = g_signal_new("blocklist-updated", core_type, G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET(TrCoreClass,
        blocklist_updated), NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

    signals[PORT_SIGNAL] = g_signal_new("port-tested", core_type, G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(TrCoreClass, port_tested),
        NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[PREFS_SIGNAL] = g_signal_new("prefs-changed", core_type, G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(TrCoreClass,
        prefs_changed), NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
}

static void tr_core_init(TrCore* core)
{
    GtkListStore* store;
    struct TrCorePrivate* p;

    /* column types for the model used to store torrent information */
    /* keep this in sync with the enum near the bottom of tr_core.h */
    GType types[] =
    {
        G_TYPE_POINTER, /* collated name */
        G_TYPE_POINTER, /* tr_torrent* */
        G_TYPE_INT, /* torrent id */
        G_TYPE_DOUBLE, /* tr_stat.pieceUploadSpeed_KBps */
        G_TYPE_DOUBLE, /* tr_stat.pieceDownloadSpeed_KBps */
        G_TYPE_INT, /* tr_stat.peersGettingFromUs */
        G_TYPE_INT, /* tr_stat.peersSendingToUs + webseedsSendingToUs */
        G_TYPE_DOUBLE, /* tr_stat.recheckProgress */
        G_TYPE_BOOLEAN, /* filter.c:ACTIVITY_FILTER_ACTIVE */
        G_TYPE_INT, /* tr_stat.activity */
        G_TYPE_UCHAR, /* tr_stat.finished */
        G_TYPE_CHAR, /* tr_priority_t */
        G_TYPE_INT, /* tr_stat.queuePosition */
        G_TYPE_UINT, /* build_torrent_trackers_hash () */
        G_TYPE_INT, /* MC_ERROR */
        G_TYPE_INT /* MC_ACTIVE_PEER_COUNT */
    };

#if GLIB_CHECK_VERSION(2, 58, 0)
    p = core->priv = tr_core_get_instance_private(core);
#else
    p = core->priv = G_TYPE_INSTANCE_GET_PRIVATE(core, TR_CORE_TYPE, struct TrCorePrivate);
#endif

    /* create the model used to store torrent data */
    g_assert(G_N_ELEMENTS(types) == MC_ROW_COUNT);
    store = gtk_list_store_newv(MC_ROW_COUNT, types);

    p->raw_model = GTK_TREE_MODEL(store);
    p->sorted_model = gtk_tree_model_sort_new_with_model(p->raw_model);
    p->string_chunk = g_string_chunk_new(2048);
    g_object_unref(p->raw_model);
}

/***
****  EMIT SIGNALS
***/

static inline void core_emit_blocklist_udpated(TrCore* core, int ruleCount)
{
    g_signal_emit(core, signals[BLOCKLIST_SIGNAL], 0, ruleCount);
}

static inline void core_emit_port_tested(TrCore* core, gboolean is_open)
{
    g_signal_emit(core, signals[PORT_SIGNAL], 0, is_open);
}

static inline void core_emit_err(TrCore* core, enum tr_core_err type, char const* msg)
{
    g_signal_emit(core, signals[ADD_ERROR_SIGNAL], 0, type, msg);
}

static inline void core_emit_busy(TrCore* core, gboolean is_busy)
{
    g_signal_emit(core, signals[BUSY_SIGNAL], 0, is_busy);
}

void gtr_core_pref_changed(TrCore* core, tr_quark const key)
{
    g_signal_emit(core, signals[PREFS_SIGNAL], 0, key);
}

/***
****
***/

static GtkTreeModel* core_raw_model(TrCore* core)
{
    return core_is_disposed(core) ? NULL : core->priv->raw_model;
}

GtkTreeModel* gtr_core_model(TrCore* core)
{
    return core_is_disposed(core) ? NULL : core->priv->sorted_model;
}

tr_session* gtr_core_session(TrCore* core)
{
    return core_is_disposed(core) ? NULL : core->priv->session;
}

/***
****  BUSY
***/

static bool core_is_busy(TrCore* core)
{
    return core->priv->busy_count > 0;
}

static void core_add_to_busy(TrCore* core, int addMe)
{
    bool const wasBusy = core_is_busy(core);

    core->priv->busy_count += addMe;

    if (wasBusy != core_is_busy(core))
    {
        core_emit_busy(core, core_is_busy(core));
    }
}

static void core_inc_busy(TrCore* core)
{
    core_add_to_busy(core, 1);
}

static void core_dec_busy(TrCore* core)
{
    core_add_to_busy(core, -1);
}

/***
****
****  SORTING THE MODEL
****
***/

static gboolean is_valid_eta(int t)
{
    return t != TR_ETA_NOT_AVAIL && t != TR_ETA_UNKNOWN;
}

static int compare_eta(int a, int b)
{
    int ret;

    gboolean const a_valid = is_valid_eta(a);
    gboolean const b_valid = is_valid_eta(b);

    if (!a_valid && !b_valid)
    {
        ret = 0;
    }
    else if (!a_valid)
    {
        ret = -1;
    }
    else if (!b_valid)
    {
        ret = 1;
    }
    else
    {
        ret = a < b ? 1 : -1;
    }

    return ret;
}

static int compare_double(double a, double b)
{
    int ret;

    if (a < b)
    {
        ret = -1;
    }
    else if (a > b)
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static int compare_uint64(uint64_t a, uint64_t b)
{
    int ret;

    if (a < b)
    {
        ret = -1;
    }
    else if (a > b)
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static int compare_int(int a, int b)
{
    int ret;

    if (a < b)
    {
        ret = -1;
    }
    else if (a > b)
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static int compare_ratio(double a, double b)
{
    int ret;

    if ((int)a == TR_RATIO_INF && (int)b == TR_RATIO_INF)
    {
        ret = 0;
    }
    else if ((int)a == TR_RATIO_INF)
    {
        ret = 1;
    }
    else if ((int)b == TR_RATIO_INF)
    {
        ret = -1;
    }
    else
    {
        ret = compare_double(a, b);
    }

    return ret;
}

static int compare_time(time_t a, time_t b)
{
    int ret;

    if (a < b)
    {
        ret = -1;
    }
    else if (a > b)
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static int compare_by_name(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer user_data UNUSED)
{
    char const* ca;
    char const* cb;
    gtk_tree_model_get(m, a, MC_NAME_COLLATED, &ca, -1);
    gtk_tree_model_get(m, b, MC_NAME_COLLATED, &cb, -1);
    return g_strcmp0(ca, cb);
}

static int compare_by_queue(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer user_data UNUSED)
{
    tr_torrent* ta;
    tr_torrent* tb;
    tr_stat const* sa;
    tr_stat const* sb;

    gtk_tree_model_get(m, a, MC_TORRENT, &ta, -1);
    sa = tr_torrentStatCached(ta);
    gtk_tree_model_get(m, b, MC_TORRENT, &tb, -1);
    sb = tr_torrentStatCached(tb);

    return sb->queuePosition - sa->queuePosition;
}

static int compare_by_ratio(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer user_data)
{
    int ret = 0;
    tr_torrent* ta;
    tr_torrent* tb;
    tr_stat const* sa;
    tr_stat const* sb;

    gtk_tree_model_get(m, a, MC_TORRENT, &ta, -1);
    sa = tr_torrentStatCached(ta);
    gtk_tree_model_get(m, b, MC_TORRENT, &tb, -1);
    sb = tr_torrentStatCached(tb);

    if (ret == 0)
    {
        ret = compare_ratio(sa->ratio, sb->ratio);
    }

    if (ret == 0)
    {
        ret = compare_by_queue(m, a, b, user_data);
    }

    return ret;
}

static int compare_by_activity(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer user_data)
{
    int ret = 0;
    tr_torrent* ta;
    tr_torrent* tb;
    double aUp;
    double aDown;
    double bUp;
    double bDown;

    gtk_tree_model_get(m, a, MC_SPEED_UP, &aUp, MC_SPEED_DOWN, &aDown, MC_TORRENT, &ta, -1);
    gtk_tree_model_get(m, b, MC_SPEED_UP, &bUp, MC_SPEED_DOWN, &bDown, MC_TORRENT, &tb, -1);

    ret = compare_double(aUp + aDown, bUp + bDown);

    if (ret == 0)
    {
        tr_stat const* const sa = tr_torrentStatCached(ta);
        tr_stat const* const sb = tr_torrentStatCached(tb);
        ret = compare_uint64(sa->peersSendingToUs + sa->peersGettingFromUs, sb->peersSendingToUs + sb->peersGettingFromUs);
    }

    if (ret == 0)
    {
        ret = compare_by_queue(m, a, b, user_data);
    }

    return ret;
}

static int compare_by_age(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer u)
{
    int ret = 0;
    tr_torrent* ta;
    tr_torrent* tb;

    gtk_tree_model_get(m, a, MC_TORRENT, &ta, -1);
    gtk_tree_model_get(m, b, MC_TORRENT, &tb, -1);

    if (ret == 0)
    {
        ret = compare_time(tr_torrentStatCached(ta)->addedDate, tr_torrentStatCached(tb)->addedDate);
    }

    if (ret == 0)
    {
        ret = compare_by_name(m, a, b, u);
    }

    return ret;
}

static int compare_by_size(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer u)
{
    int ret = 0;
    tr_torrent* t;
    tr_info const* ia;
    tr_info const* ib;

    gtk_tree_model_get(m, a, MC_TORRENT, &t, -1);
    ia = tr_torrentInfo(t);
    gtk_tree_model_get(m, b, MC_TORRENT, &t, -1);
    ib = tr_torrentInfo(t);

    if (ret == 0)
    {
        ret = compare_uint64(ia->totalSize, ib->totalSize);
    }

    if (ret == 0)
    {
        ret = compare_by_name(m, a, b, u);
    }

    return ret;
}

static int compare_by_progress(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer u)
{
    int ret = 0;
    tr_torrent* t;
    tr_stat const* sa;
    tr_stat const* sb;

    gtk_tree_model_get(m, a, MC_TORRENT, &t, -1);
    sa = tr_torrentStatCached(t);
    gtk_tree_model_get(m, b, MC_TORRENT, &t, -1);
    sb = tr_torrentStatCached(t);

    if (ret == 0)
    {
        ret = compare_double(sa->percentComplete, sb->percentComplete);
    }

    if (ret == 0)
    {
        ret = compare_double(sa->seedRatioPercentDone, sb->seedRatioPercentDone);
    }

    if (ret == 0)
    {
        ret = compare_by_ratio(m, a, b, u);
    }

    return ret;
}

static int compare_by_eta(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer u)
{
    int ret = 0;
    tr_torrent* ta;
    tr_torrent* tb;

    gtk_tree_model_get(m, a, MC_TORRENT, &ta, -1);
    gtk_tree_model_get(m, b, MC_TORRENT, &tb, -1);

    if (ret == 0)
    {
        ret = compare_eta(tr_torrentStatCached(ta)->eta, tr_torrentStatCached(tb)->eta);
    }

    if (ret == 0)
    {
        ret = compare_by_name(m, a, b, u);
    }

    return ret;
}

static int compare_by_state(GtkTreeModel* m, GtkTreeIter* a, GtkTreeIter* b, gpointer u)
{
    int ret = 0;
    int sa;
    int sb;
    tr_torrent* ta;
    tr_torrent* tb;

    gtk_tree_model_get(m, a, MC_ACTIVITY, &sa, MC_TORRENT, &ta, -1);
    gtk_tree_model_get(m, b, MC_ACTIVITY, &sb, MC_TORRENT, &tb, -1);

    if (ret == 0)
    {
        ret = compare_int(sa, sb);
    }

    if (ret == 0)
    {
        ret = compare_by_queue(m, a, b, u);
    }

    return ret;
}

static void core_set_sort_mode(TrCore* core, char const* mode, gboolean is_reversed)
{
    int const col = MC_TORRENT;
    GtkTreeIterCompareFunc sort_func;
    GtkSortType type = is_reversed ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
    GtkTreeSortable* sortable = GTK_TREE_SORTABLE(gtr_core_model(core));

    if (g_strcmp0(mode, "sort-by-activity") == 0)
    {
        sort_func = compare_by_activity;
    }
    else if (g_strcmp0(mode, "sort-by-age") == 0)
    {
        sort_func = compare_by_age;
    }
    else if (g_strcmp0(mode, "sort-by-progress") == 0)
    {
        sort_func = compare_by_progress;
    }
    else if (g_strcmp0(mode, "sort-by-queue") == 0)
    {
        sort_func = compare_by_queue;
    }
    else if (g_strcmp0(mode, "sort-by-time-left") == 0)
    {
        sort_func = compare_by_eta;
    }
    else if (g_strcmp0(mode, "sort-by-ratio") == 0)
    {
        sort_func = compare_by_ratio;
    }
    else if (g_strcmp0(mode, "sort-by-state") == 0)
    {
        sort_func = compare_by_state;
    }
    else if (g_strcmp0(mode, "sort-by-size") == 0)
    {
        sort_func = compare_by_size;
    }
    else
    {
        sort_func = compare_by_name;
        type = is_reversed ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    }

    gtk_tree_sortable_set_sort_func(sortable, col, sort_func, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, col, type);
}

/***
****
****  WATCHDIR
****
***/

static time_t get_file_mtime(GFile* file)
{
    GFileInfo* info;
    time_t mtime = 0;

    info = g_file_query_info(file, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, NULL);

    if (info != NULL)
    {
        mtime = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        g_object_unref(G_OBJECT(info));
    }

    return mtime;
}

static void rename_torrent_and_unref_file(GFile* file)
{
    GFileInfo* info;

    info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_EDIT_NAME, 0, NULL, NULL);

    if (info != NULL)
    {
        GError* error = NULL;
        char const* old_name;
        char* new_name;
        GFile* new_file;

        old_name = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_STANDARD_EDIT_NAME);
        new_name = g_strdup_printf("%s.added", old_name);
        new_file = g_file_set_display_name(file, new_name, NULL, &error);

        if (error != NULL)
        {
            g_message("Unable to rename \"%s\" as \"%s\": %s", old_name, new_name, error->message);
            g_error_free(error);
        }

        if (new_file != NULL)
        {
            g_object_unref(G_OBJECT(new_file));
        }

        g_free(new_name);
        g_object_unref(G_OBJECT(info));
    }

    g_object_unref(G_OBJECT(file));
}

static gboolean core_watchdir_idle(gpointer gcore)
{
    GSList* changing = NULL;
    GSList* unchanging = NULL;
    TrCore* core = TR_CORE(gcore);
    time_t const now = tr_time();
    struct TrCorePrivate* p = core->priv;

    /* separate the files into two lists: changing and unchanging */
    for (GSList* l = p->monitor_files; l != NULL; l = l->next)
    {
        GFile* file = l->data;
        time_t const mtime = get_file_mtime(file);

        if (mtime + 2 >= now)
        {
            changing = g_slist_prepend(changing, file);
        }
        else
        {
            unchanging = g_slist_prepend(unchanging, file);
        }
    }

    /* add the files that have stopped changing */
    if (unchanging != NULL)
    {
        gboolean const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents);
        gboolean const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);

        core->priv->adding_from_watch_dir = TRUE;
        gtr_core_add_files(core, unchanging, do_start, do_prompt, TRUE);
        g_slist_foreach(unchanging, (GFunc)(GCallback)rename_torrent_and_unref_file, NULL);
        g_slist_free(unchanging);
        core->priv->adding_from_watch_dir = FALSE;
    }

    /* keep monitoring the ones that are still changing */
    g_slist_free(p->monitor_files);
    p->monitor_files = changing;

    /* if monitor_files is nonempty, keep checking every second */
    if (core->priv->monitor_files)
    {
        return G_SOURCE_CONTINUE;
    }

    core->priv->monitor_idle_tag = 0;
    return G_SOURCE_REMOVE;
}

/* If this file is a torrent, add it to our list */
static void core_watchdir_monitor_file(TrCore* core, GFile* file)
{
    char* filename = g_file_get_path(file);
    gboolean const is_torrent = g_str_has_suffix(filename, ".torrent");

    if (is_torrent)
    {
        struct TrCorePrivate* p = core->priv;
        bool found = false;

        /* if we're not already watching this file, start watching it now */
        for (GSList* l = p->monitor_files; !found && l != NULL; l = l->next)
        {
            found = g_file_equal(file, l->data);
        }

        if (!found)
        {
            g_object_ref(file);
            p->monitor_files = g_slist_prepend(p->monitor_files, file);

            if (p->monitor_idle_tag == 0)
            {
                p->monitor_idle_tag = gdk_threads_add_timeout_seconds(1, core_watchdir_idle, core);
            }
        }
    }

    g_free(filename);
}

/* GFileMonitor noticed a file was created */
static void on_file_changed_in_watchdir(GFileMonitor* monitor UNUSED, GFile* file, GFile* other_type UNUSED,
    GFileMonitorEvent event_type, gpointer core)
{
    if (event_type == G_FILE_MONITOR_EVENT_CREATED)
    {
        core_watchdir_monitor_file(core, file);
    }
}

/* walk through the pre-existing files in the watchdir */
static void core_watchdir_scan(TrCore* core)
{
    char const* dirname = gtr_pref_string_get(TR_KEY_watch_dir);
    GDir* dir = g_dir_open(dirname, 0, NULL);

    if (dir != NULL)
    {
        char const* name;

        while ((name = g_dir_read_name(dir)) != NULL)
        {
            char* filename = g_build_filename(dirname, name, NULL);
            GFile* file = g_file_new_for_path(filename);
            core_watchdir_monitor_file(core, file);
            g_object_unref(file);
            g_free(filename);
        }

        g_dir_close(dir);
    }
}

static void core_watchdir_update(TrCore* core)
{
    gboolean const is_enabled = gtr_pref_flag_get(TR_KEY_watch_dir_enabled);
    GFile* dir = g_file_new_for_path(gtr_pref_string_get(TR_KEY_watch_dir));
    struct TrCorePrivate* p = core->priv;

    if (p->monitor != NULL && (!is_enabled || !g_file_equal(dir, p->monitor_dir)))
    {
        g_signal_handler_disconnect(p->monitor, p->monitor_tag);
        g_file_monitor_cancel(p->monitor);
        g_object_unref(p->monitor);
        g_object_unref(p->monitor_dir);

        p->monitor_dir = NULL;
        p->monitor = NULL;
        p->monitor_tag = 0;
    }

    if (is_enabled && p->monitor == NULL)
    {
        GFileMonitor* m = g_file_monitor_directory(dir, 0, NULL, NULL);
        core_watchdir_scan(core);

        g_object_ref(dir);
        p->monitor = m;
        p->monitor_dir = dir;
        p->monitor_tag = g_signal_connect(m, "changed", G_CALLBACK(on_file_changed_in_watchdir), core);
    }

    g_object_unref(dir);
}

/***
****
***/

static void on_pref_changed(TrCore* core, tr_quark const key, gpointer data UNUSED)
{
    switch (key)
    {
    case TR_KEY_sort_mode:
    case TR_KEY_sort_reversed:
        {
            char const* mode = gtr_pref_string_get(TR_KEY_sort_mode);
            gboolean const is_reversed = gtr_pref_flag_get(TR_KEY_sort_reversed);
            core_set_sort_mode(core, mode, is_reversed);
            break;
        }

    case TR_KEY_peer_limit_global:
        tr_sessionSetPeerLimit(gtr_core_session(core), gtr_pref_int_get(key));
        break;

    case TR_KEY_peer_limit_per_torrent:
        tr_sessionSetPeerLimitPerTorrent(gtr_core_session(core), gtr_pref_int_get(key));
        break;

    case TR_KEY_inhibit_desktop_hibernation:
        core_maybe_inhibit_hibernation(core);
        break;

    case TR_KEY_watch_dir:
    case TR_KEY_watch_dir_enabled:
        core_watchdir_update(core);
        break;

    default:
        break;
    }
}

/**
***
**/

TrCore* gtr_core_new(tr_session* session)
{
    TrCore* core = TR_CORE(g_object_new(TR_CORE_TYPE, NULL));

    core->priv->session = session;

    /* init from prefs & listen to pref changes */
    on_pref_changed(core, TR_KEY_sort_mode, NULL);
    on_pref_changed(core, TR_KEY_sort_reversed, NULL);
    on_pref_changed(core, TR_KEY_watch_dir_enabled, NULL);
    on_pref_changed(core, TR_KEY_peer_limit_global, NULL);
    on_pref_changed(core, TR_KEY_inhibit_desktop_hibernation, NULL);
    g_signal_connect(core, "prefs-changed", G_CALLBACK(on_pref_changed), NULL);

    return core;
}

tr_session* gtr_core_close(TrCore* core)
{
    tr_session* session = gtr_core_session(core);

    if (session != NULL)
    {
        core->priv->session = NULL;
        gtr_pref_save(session);
    }

    return session;
}

/***
****  COMPLETENESS CALLBACK
***/

struct notify_callback_data
{
    TrCore* core;
    int torrent_id;
};

static gboolean on_torrent_completeness_changed_idle(gpointer gdata)
{
    struct notify_callback_data* data = gdata;
    gtr_notify_torrent_completed(data->core, data->torrent_id);
    g_object_unref(G_OBJECT(data->core));
    g_free(data);
    return G_SOURCE_REMOVE;
}

/* this is called in the libtransmission thread, *NOT* the GTK+ thread,
   so delegate to the GTK+ thread before calling notify's dbus code... */
static void on_torrent_completeness_changed(tr_torrent* tor, tr_completeness completeness, bool was_running, void* gcore)
{
    if (was_running && completeness != TR_LEECH && tr_torrentStat(tor)->sizeWhenDone != 0)
    {
        struct notify_callback_data* data = g_new(struct notify_callback_data, 1);
        data->core = gcore;
        data->torrent_id = tr_torrentId(tor);
        g_object_ref(G_OBJECT(data->core));
        gdk_threads_add_idle(on_torrent_completeness_changed_idle, data);
    }
}

/***
****  METADATA CALLBACK
***/

static char const* get_collated_name(TrCore* core, tr_torrent const* tor)
{
    char buf[2048];
    char const* name = tr_torrentName(tor);
    char* down = g_utf8_strdown(name ? name : "", -1);
    tr_info const* inf = tr_torrentInfo(tor);
    g_snprintf(buf, sizeof(buf), "%s\t%s", down, inf->hashString);
    g_free(down);
    return g_string_chunk_insert_const(core->priv->string_chunk, buf);
}

struct metadata_callback_data
{
    TrCore* core;
    int torrent_id;
};

static gboolean find_row_from_torrent_id(GtkTreeModel* model, int id, GtkTreeIter* setme)
{
    GtkTreeIter iter;
    gboolean match = FALSE;

    if (gtk_tree_model_iter_children(model, &iter, NULL))
    {
        do
        {
            int row_id;
            gtk_tree_model_get(model, &iter, MC_TORRENT_ID, &row_id, -1);
            match = id == row_id;
        }
        while (!match && gtk_tree_model_iter_next(model, &iter));
    }

    if (match)
    {
        *setme = iter;
    }

    return match;
}

static gboolean on_torrent_metadata_changed_idle(gpointer gdata)
{
    struct notify_callback_data* data = gdata;
    tr_session* session = gtr_core_session(data->core);
    tr_torrent* tor = tr_torrentFindFromId(session, data->torrent_id);

    /* update the torrent's collated name */
    if (tor != NULL)
    {
        GtkTreeIter iter;
        GtkTreeModel* model = core_raw_model(data->core);

        if (find_row_from_torrent_id(model, data->torrent_id, &iter))
        {
            char const* collated = get_collated_name(data->core, tor);
            GtkListStore* store = GTK_LIST_STORE(model);
            gtk_list_store_set(store, &iter, MC_NAME_COLLATED, collated, -1);
        }
    }

    /* cleanup */
    g_object_unref(G_OBJECT(data->core));
    g_free(data);
    return G_SOURCE_REMOVE;
}

/* this is called in the libtransmission thread, *NOT* the GTK+ thread,
   so delegate to the GTK+ thread before changing our list store... */
static void on_torrent_metadata_changed(tr_torrent* tor, void* gcore)
{
    struct notify_callback_data* data = g_new(struct notify_callback_data, 1);
    data->core = gcore;
    data->torrent_id = tr_torrentId(tor);
    g_object_ref(G_OBJECT(data->core));
    gdk_threads_add_idle(on_torrent_metadata_changed_idle, data);
}

/***
****
****  ADDING TORRENTS
****
***/

static unsigned int build_torrent_trackers_hash(tr_torrent* tor)
{
    uint64_t hash = 0;
    tr_info const* const inf = tr_torrentInfo(tor);

    for (unsigned int i = 0; i < inf->trackerCount; ++i)
    {
        for (char const* pch = inf->trackers[i].announce; *pch != '\0'; ++pch)
        {
            hash = (hash << 4) ^ (hash >> 28) ^ *pch;
        }
    }

    return hash;
}

static gboolean is_torrent_active(tr_stat const* st)
{
    return st->peersSendingToUs > 0 || st->peersGettingFromUs > 0 || st->activity == TR_STATUS_CHECK;
}

void gtr_core_add_torrent(TrCore* core, tr_torrent* tor, gboolean do_notify)
{
    if (tor != NULL)
    {
        GtkTreeIter unused;
        tr_stat const* st = tr_torrentStat(tor);
        char const* collated = get_collated_name(core, tor);
        unsigned int const trackers_hash = build_torrent_trackers_hash(tor);
        GtkListStore* store = GTK_LIST_STORE(core_raw_model(core));

        gtk_list_store_insert_with_values(store, &unused, 0,
            MC_NAME_COLLATED, collated,
            MC_TORRENT, tor,
            MC_TORRENT_ID, tr_torrentId(tor),
            MC_SPEED_UP, st->pieceUploadSpeed_KBps,
            MC_SPEED_DOWN, st->pieceDownloadSpeed_KBps,
            MC_ACTIVE_PEERS_UP, st->peersGettingFromUs,
            MC_ACTIVE_PEERS_DOWN, st->peersSendingToUs + st->webseedsSendingToUs,
            MC_RECHECK_PROGRESS, st->recheckProgress,
            MC_ACTIVE, is_torrent_active(st),
            MC_ACTIVITY, st->activity,
            MC_FINISHED, st->finished,
            MC_PRIORITY, tr_torrentGetPriority(tor),
            MC_QUEUE_POSITION, st->queuePosition,
            MC_TRACKERS, trackers_hash,
            -1);

        if (do_notify)
        {
            gtr_notify_torrent_added(tr_torrentName(tor));
        }

        tr_torrentSetMetadataCallback(tor, on_torrent_metadata_changed, core);
        tr_torrentSetCompletenessCallback(tor, on_torrent_completeness_changed, core);
    }
}

static tr_torrent* core_create_new_torrent(TrCore* core, tr_ctor* ctor)
{
    tr_torrent* tor;
    bool do_trash = false;
    tr_session* session = gtr_core_session(core);

    /* let the gtk client handle the removal, since libT
     * doesn't have any concept of the glib trash API */
    tr_ctorGetDeleteSource(ctor, &do_trash);
    tr_ctorSetDeleteSource(ctor, FALSE);
    tor = tr_torrentNew(ctor, NULL, NULL);

    if (tor != NULL && do_trash)
    {
        char const* config = tr_sessionGetConfigDir(session);
        char const* source = tr_ctorGetSourceFile(ctor);

        if (source != NULL)
        {
            /* #1294: don't delete the .torrent file if it's our internal copy */
            bool const is_internal = strstr(source, config) == source;

            if (!is_internal)
            {
                gtr_file_trash_or_remove(source, NULL);
            }
        }
    }

    return tor;
}

static int core_add_ctor(TrCore* core, tr_ctor* ctor, gboolean do_prompt, gboolean do_notify)
{
    tr_info inf;
    int err = tr_torrentParse(ctor, &inf);

    switch (err)
    {
    case TR_PARSE_ERR:
        break;

    case TR_PARSE_DUPLICATE:
        /* don't complain about .torrent files in the watch directory
         * that have already been added... that gets annoying and we
         * don't want to be nagging users to clean up their watch dirs */
        if (tr_ctorGetSourceFile(ctor) == NULL || !core->priv->adding_from_watch_dir)
        {
            core_emit_err(core, err, inf.name);
        }

        tr_metainfoFree(&inf);
        tr_ctorFree(ctor);
        break;

    default:
        if (do_prompt)
        {
            g_signal_emit(core, signals[ADD_PROMPT_SIGNAL], 0, ctor);
        }
        else
        {
            gtr_core_add_torrent(core, core_create_new_torrent(core, ctor), do_notify);
            tr_ctorFree(ctor);
        }

        tr_metainfoFree(&inf);
        break;
    }

    return err;
}

static void core_apply_defaults(tr_ctor* ctor)
{
    if (!tr_ctorGetPaused(ctor, TR_FORCE, NULL))
    {
        tr_ctorSetPaused(ctor, TR_FORCE, !gtr_pref_flag_get(TR_KEY_start_added_torrents));
    }

    if (!tr_ctorGetDeleteSource(ctor, NULL))
    {
        tr_ctorSetDeleteSource(ctor, gtr_pref_flag_get(TR_KEY_trash_original_torrent_files));
    }

    if (!tr_ctorGetPeerLimit(ctor, TR_FORCE, NULL))
    {
        tr_ctorSetPeerLimit(ctor, TR_FORCE, gtr_pref_int_get(TR_KEY_peer_limit_per_torrent));
    }

    if (!tr_ctorGetDownloadDir(ctor, TR_FORCE, NULL))
    {
        tr_ctorSetDownloadDir(ctor, TR_FORCE, gtr_pref_string_get(TR_KEY_download_dir));
    }
}

void gtr_core_add_ctor(TrCore* core, tr_ctor* ctor)
{
    gboolean const do_notify = FALSE;
    gboolean const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);
    core_apply_defaults(ctor);
    core_add_ctor(core, ctor, do_prompt, do_notify);
}

/***
****
***/

struct add_from_url_data
{
    TrCore* core;
    tr_ctor* ctor;
    bool do_prompt;
    bool do_notify;
};

static void add_file_async_callback(GObject* file, GAsyncResult* result, gpointer gdata)
{
    gsize length;
    char* contents;
    GError* error = NULL;
    struct add_from_url_data* data = gdata;

    if (!g_file_load_contents_finish(G_FILE(file), result, &contents, &length, NULL, &error))
    {
        g_message(_("Couldn't read \"%s\": %s"), g_file_get_parse_name(G_FILE(file)), error->message);
        g_error_free(error);
    }
    else if (tr_ctorSetMetainfo(data->ctor, (uint8_t const*)contents, length) == 0)
    {
        core_add_ctor(data->core, data->ctor, data->do_prompt, data->do_notify);
    }
    else
    {
        tr_ctorFree(data->ctor);
    }

    core_dec_busy(data->core);
    g_free(data);
}

static bool add_file(TrCore* core, GFile* file, gboolean do_start, gboolean do_prompt, gboolean do_notify)
{
    bool handled = false;
    tr_session* session = gtr_core_session(core);

    if (session != NULL)
    {
        tr_ctor* ctor;
        bool tried = false;
        bool loaded = false;

        ctor = tr_ctorNew(session);
        core_apply_defaults(ctor);
        tr_ctorSetPaused(ctor, TR_FORCE, !do_start);

        /* local files... */
        if (!tried)
        {
            char* str = g_file_get_path(file);

            if ((tried = (str != NULL) && g_file_test(str, G_FILE_TEST_EXISTS)))
            {
                loaded = !tr_ctorSetMetainfoFromFile(ctor, str);
            }

            g_free(str);
        }

        /* magnet links... */
        if (!tried && g_file_has_uri_scheme(file, "magnet"))
        {
            /* GFile mangles the original string with /// so we have to un-mangle */
            char* str = g_file_get_parse_name(file);
            char* magnet = g_strdup_printf("magnet:%s", strchr(str, '?'));
            tried = true;
            loaded = !tr_ctorSetMetainfoFromMagnetLink(ctor, magnet);
            g_free(magnet);
            g_free(str);
        }

        /* hashcodes that we can turn into magnet links... */
        if (!tried)
        {
            char* str = g_file_get_basename(file);

            if (gtr_is_hex_hashcode(str))
            {
                char* magnet = g_strdup_printf("magnet:?xt=urn:btih:%s", str);
                tried = true;
                loaded = !tr_ctorSetMetainfoFromMagnetLink(ctor, magnet);
                g_free(magnet);
            }

            g_free(str);
        }

        /* if we were able to load the metainfo, add the torrent */
        if (loaded)
        {
            handled = true;
            core_add_ctor(core, ctor, do_prompt, do_notify);
        }
        else if (g_file_has_uri_scheme(file, "http") || g_file_has_uri_scheme(file, "https") ||
            g_file_has_uri_scheme(file, "ftp"))
        {
            struct add_from_url_data* data;

            data = g_new0(struct add_from_url_data, 1);
            data->core = core;
            data->ctor = ctor;
            data->do_prompt = do_prompt;
            data->do_notify = do_notify;

            handled = true;
            core_inc_busy(core);
            g_file_load_contents_async(file, NULL, add_file_async_callback, data);
        }
        else
        {
            tr_ctorFree(ctor);
            g_message(_("Skipping unknown torrent \"%s\""), g_file_get_parse_name(file));
        }
    }

    return handled;
}

bool gtr_core_add_from_url(TrCore* core, char const* uri)
{
    bool handled;
    bool const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents);
    bool const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);
    bool const do_notify = false;

    GFile* file = g_file_new_for_uri(uri);
    handled = add_file(core, file, do_start, do_prompt, do_notify);
    g_object_unref(file);
    gtr_core_torrents_added(core);

    return handled;
}

void gtr_core_add_files(TrCore* core, GSList* files, gboolean do_start, gboolean do_prompt, gboolean do_notify)
{
    for (GSList* l = files; l != NULL; l = l->next)
    {
        add_file(core, l->data, do_start, do_prompt, do_notify);
    }

    gtr_core_torrents_added(core);
}

void gtr_core_torrents_added(TrCore* self)
{
    gtr_core_update(self);
    core_emit_err(self, TR_CORE_ERR_NO_MORE_TORRENTS, NULL);
}

void gtr_core_torrent_changed(TrCore* self, int id)
{
    GtkTreeIter iter;
    GtkTreeModel* model = core_raw_model(self);

    if (find_row_from_torrent_id(model, id, &iter))
    {
        GtkTreePath* path = gtk_tree_model_get_path(model, &iter);
        gtk_tree_model_row_changed(model, path, &iter);
    }
}

void gtr_core_remove_torrent(TrCore* core, int id, gboolean delete_local_data)
{
    tr_torrent* tor = gtr_core_find_torrent(core, id);

    if (tor != NULL)
    {
        /* remove from the gui */
        GtkTreeIter iter;
        GtkTreeModel* model = core_raw_model(core);

        if (find_row_from_torrent_id(model, id, &iter))
        {
            gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
        }

        /* remove the torrent */
        tr_torrentRemove(tor, delete_local_data, gtr_file_trash_or_remove);
    }
}

void gtr_core_load(TrCore* self, gboolean forcePaused)
{
    tr_ctor* ctor;
    tr_torrent** torrents;
    int count = 0;

    ctor = tr_ctorNew(gtr_core_session(self));

    if (forcePaused)
    {
        tr_ctorSetPaused(ctor, TR_FORCE, TRUE);
    }

    tr_ctorSetPeerLimit(ctor, TR_FALLBACK, gtr_pref_int_get(TR_KEY_peer_limit_per_torrent));

    torrents = tr_sessionLoadTorrents(gtr_core_session(self), ctor, &count);

    for (int i = 0; i < count; ++i)
    {
        gtr_core_add_torrent(self, torrents[i], FALSE);
    }

    tr_free(torrents);
    tr_ctorFree(ctor);
}

void gtr_core_clear(TrCore* self)
{
    gtk_list_store_clear(GTK_LIST_STORE(core_raw_model(self)));
}

/***
****
***/

static int gtr_compare_double(double const a, double const b, int decimal_places)
{
    int ret;
    int64_t const ia = (int64_t)(a * pow(10, decimal_places));
    int64_t const ib = (int64_t)(b * pow(10, decimal_places));

    if (ia < ib)
    {
        ret = -1;
    }
    else if (ia > ib)
    {
        ret = 1;
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static void update_foreach(GtkTreeModel* model, GtkTreeIter* iter)
{
    int oldActivity;
    int newActivity;
    int oldActivePeerCount;
    int newActivePeerCount;
    int oldError;
    int newError;
    bool oldFinished;
    bool newFinished;
    int oldQueuePosition;
    int newQueuePosition;
    int oldDownloadPeerCount;
    int newDownloadPeerCount;
    int oldUploadPeerCount;
    int newUploadPeerCount;
    tr_priority_t oldPriority;
    tr_priority_t newPriority;
    unsigned int oldTrackers;
    unsigned int newTrackers;
    double oldUpSpeed;
    double newUpSpeed;
    double oldDownSpeed;
    double newDownSpeed;
    double oldRecheckProgress;
    double newRecheckProgress;
    gboolean oldActive;
    gboolean newActive;
    tr_stat const* st;
    tr_torrent* tor;

    /* get the old states */
    gtk_tree_model_get(model, iter,
        MC_TORRENT, &tor,
        MC_ACTIVE, &oldActive,
        MC_ACTIVE_PEER_COUNT, &oldActivePeerCount,
        MC_ACTIVE_PEERS_UP, &oldUploadPeerCount,
        MC_ACTIVE_PEERS_DOWN, &oldDownloadPeerCount,
        MC_ERROR, &oldError,
        MC_ACTIVITY, &oldActivity,
        MC_FINISHED, &oldFinished,
        MC_PRIORITY, &oldPriority,
        MC_QUEUE_POSITION, &oldQueuePosition,
        MC_TRACKERS, &oldTrackers,
        MC_SPEED_UP, &oldUpSpeed,
        MC_RECHECK_PROGRESS, &oldRecheckProgress,
        MC_SPEED_DOWN, &oldDownSpeed,
        -1);

    /* get the new states */
    st = tr_torrentStat(tor);
    newActive = is_torrent_active(st);
    newActivity = st->activity;
    newFinished = st->finished;
    newPriority = tr_torrentGetPriority(tor);
    newQueuePosition = st->queuePosition;
    newTrackers = build_torrent_trackers_hash(tor);
    newUpSpeed = st->pieceUploadSpeed_KBps;
    newDownSpeed = st->pieceDownloadSpeed_KBps;
    newRecheckProgress = st->recheckProgress;
    newActivePeerCount = st->peersSendingToUs + st->peersGettingFromUs + st->webseedsSendingToUs;
    newDownloadPeerCount = st->peersSendingToUs;
    newUploadPeerCount = st->peersGettingFromUs + st->webseedsSendingToUs;
    newError = st->error;

    /* updating the model triggers off resort/refresh,
       so don't do it unless something's actually changed... */
    if (newActive != oldActive || newActivity != oldActivity || newFinished != oldFinished || newPriority != oldPriority ||
        newQueuePosition != oldQueuePosition || newError != oldError || newActivePeerCount != oldActivePeerCount ||
        newDownloadPeerCount != oldDownloadPeerCount || newUploadPeerCount != oldUploadPeerCount ||
        newTrackers != oldTrackers || gtr_compare_double(newUpSpeed, oldUpSpeed, 2) != 0 ||
        gtr_compare_double(newDownSpeed, oldDownSpeed, 2) != 0 ||
        gtr_compare_double(newRecheckProgress, oldRecheckProgress, 2) != 0)
    {
        gtk_list_store_set(GTK_LIST_STORE(model), iter,
            MC_ACTIVE, newActive,
            MC_ACTIVE_PEER_COUNT, newActivePeerCount,
            MC_ACTIVE_PEERS_UP, newUploadPeerCount,
            MC_ACTIVE_PEERS_DOWN, newDownloadPeerCount,
            MC_ERROR, newError,
            MC_ACTIVITY, newActivity,
            MC_FINISHED, newFinished,
            MC_PRIORITY, newPriority,
            MC_QUEUE_POSITION, newQueuePosition,
            MC_TRACKERS, newTrackers,
            MC_SPEED_UP, newUpSpeed,
            MC_SPEED_DOWN, newDownSpeed,
            MC_RECHECK_PROGRESS, newRecheckProgress,
            -1);
    }
}

void gtr_core_update(TrCore* core)
{
    GtkTreeIter iter;
    GtkTreeModel* model;

    /* update the model */
    model = core_raw_model(core);

    if (gtk_tree_model_iter_nth_child(model, &iter, NULL, 0))
    {
        do
        {
            update_foreach(model, &iter);
        }
        while (gtk_tree_model_iter_next(model, &iter));
    }

    /* update hibernation */
    core_maybe_inhibit_hibernation(core);
}

/**
***  Hibernate
**/

#define SESSION_MANAGER_SERVICE_NAME "org.gnome.SessionManager"
#define SESSION_MANAGER_INTERFACE "org.gnome.SessionManager"
#define SESSION_MANAGER_OBJECT_PATH "/org/gnome/SessionManager"

static gboolean gtr_inhibit_hibernation(guint* cookie)
{
    gboolean success;
    GVariant* response;
    GDBusConnection* connection;
    GError* err = NULL;
    char const* application = "Transmission BitTorrent Client";
    char const* reason = "BitTorrent Activity";
    int const toplevel_xid = 0;
    int const flags = 4; /* Inhibit suspending the session or computer */

    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);

    response = g_dbus_connection_call_sync(connection, SESSION_MANAGER_SERVICE_NAME, SESSION_MANAGER_OBJECT_PATH,
        SESSION_MANAGER_INTERFACE, "Inhibit", g_variant_new("(susu)", application, toplevel_xid, reason, flags), NULL,
        G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &err);

    if (response != NULL)
    {
        *cookie = g_variant_get_uint32(g_variant_get_child_value(response, 0));
    }

    success = response != NULL && err == NULL;

    /* logging */
    if (success)
    {
        tr_logAddInfo("%s", _("Inhibiting desktop hibernation"));
    }
    else
    {
        tr_logAddError(_("Couldn't inhibit desktop hibernation: %s"), err->message);
        g_error_free(err);
    }

    /* cleanup */
    if (response != NULL)
    {
        g_variant_unref(response);
    }

    if (connection != NULL)
    {
        g_object_unref(connection);
    }

    return success;
}

static void gtr_uninhibit_hibernation(guint inhibit_cookie)
{
    GVariant* response;
    GDBusConnection* connection;
    GError* err = NULL;

    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);

    response = g_dbus_connection_call_sync(connection, SESSION_MANAGER_SERVICE_NAME, SESSION_MANAGER_OBJECT_PATH,
        SESSION_MANAGER_INTERFACE, "Uninhibit", g_variant_new("(u)", inhibit_cookie), NULL, G_DBUS_CALL_FLAGS_NONE, 1000, NULL,
        &err);

    /* logging */
    if (err == NULL)
    {
        tr_logAddInfo("%s", _("Allowing desktop hibernation"));
    }
    else
    {
        g_warning("Couldn't uninhibit desktop hibernation: %s.", err->message);
        g_error_free(err);
    }

    /* cleanup */
    g_variant_unref(response);
    g_object_unref(connection);
}

static void gtr_core_set_hibernation_allowed(TrCore* core, gboolean allowed)
{
    g_return_if_fail(core);
    g_return_if_fail(core->priv);

    core->priv->inhibit_allowed = allowed != 0;

    if (allowed && core->priv->have_inhibit_cookie)
    {
        gtr_uninhibit_hibernation(core->priv->inhibit_cookie);
        core->priv->have_inhibit_cookie = FALSE;
    }

    if (!allowed && !core->priv->have_inhibit_cookie && !core->priv->dbus_error)
    {
        if (gtr_inhibit_hibernation(&core->priv->inhibit_cookie))
        {
            core->priv->have_inhibit_cookie = TRUE;
        }
        else
        {
            core->priv->dbus_error = TRUE;
        }
    }
}

static void core_maybe_inhibit_hibernation(TrCore* core)
{
    /* hibernation is allowed if EITHER
     * (a) the "inhibit" pref is turned off OR
     * (b) there aren't any active torrents */
    gboolean const hibernation_allowed = !gtr_pref_flag_get(TR_KEY_inhibit_desktop_hibernation) ||
        !gtr_core_get_active_torrent_count(core);
    gtr_core_set_hibernation_allowed(core, hibernation_allowed);
}

/**
***  Prefs
**/

static void core_commit_prefs_change(TrCore* core, tr_quark const key)
{
    gtr_core_pref_changed(core, key);
    gtr_pref_save(gtr_core_session(core));
}

void gtr_core_set_pref(TrCore* self, tr_quark const key, char const* newval)
{
    if (g_strcmp0(newval, gtr_pref_string_get(key)) != 0)
    {
        gtr_pref_string_set(key, newval);
        core_commit_prefs_change(self, key);
    }
}

void gtr_core_set_pref_bool(TrCore* self, tr_quark const key, gboolean newval)
{
    if (newval != gtr_pref_flag_get(key))
    {
        gtr_pref_flag_set(key, newval);
        core_commit_prefs_change(self, key);
    }
}

void gtr_core_set_pref_int(TrCore* self, tr_quark const key, int newval)
{
    if (newval != gtr_pref_int_get(key))
    {
        gtr_pref_int_set(key, newval);
        core_commit_prefs_change(self, key);
    }
}

void gtr_core_set_pref_double(TrCore* self, tr_quark const key, double newval)
{
    if (gtr_compare_double(newval, gtr_pref_double_get(key), 4))
    {
        gtr_pref_double_set(key, newval);
        core_commit_prefs_change(self, key);
    }
}

/***
****
****  RPC Interface
****
***/

/* #define DEBUG_RPC */

static int nextTag = 1;

typedef void (* server_response_func)(TrCore* core, tr_variant* response, gpointer user_data);

struct pending_request_data
{
    TrCore* core;
    server_response_func response_func;
    gpointer response_func_user_data;
};

static GHashTable* pendingRequests = NULL;

static gboolean core_read_rpc_response_idle(void* vresponse)
{
    int64_t intVal;
    tr_variant* response = vresponse;

    if (tr_variantDictFindInt(response, TR_KEY_tag, &intVal))
    {
        int const tag = (int)intVal;
        struct pending_request_data* data = g_hash_table_lookup(pendingRequests, &tag);

        if (data != NULL)
        {
            if (data->response_func != NULL)
            {
                (*data->response_func)(data->core, response, data->response_func_user_data);
            }

            g_hash_table_remove(pendingRequests, &tag);
        }
    }

    tr_variantFree(response);
    tr_free(response);
    return G_SOURCE_REMOVE;
}

static void core_read_rpc_response(tr_session* session UNUSED, tr_variant* response, void* unused UNUSED)
{
    tr_variant* response_copy = tr_new(tr_variant, 1);

    *response_copy = *response;
    tr_variantInitBool(response, false);

    gdk_threads_add_idle(core_read_rpc_response_idle, response_copy);
}

static void core_send_rpc_request(TrCore* core, tr_variant const* request, int tag, server_response_func response_func,
    void* response_func_user_data)
{
    tr_session* session = gtr_core_session(core);

    if (pendingRequests == NULL)
    {
        pendingRequests = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, g_free);
    }

    if (session == NULL)
    {
        g_error("GTK+ client doesn't support connections to remote servers yet.");
    }
    else
    {
        /* remember this request */
        struct pending_request_data* data;
        data = g_new0(struct pending_request_data, 1);
        data->core = core;
        data->response_func = response_func;
        data->response_func_user_data = response_func_user_data;
        g_hash_table_insert(pendingRequests, g_memdup(&tag, sizeof(int)), data);

        /* make the request */
#ifdef DEBUG_RPC
        {
            struct evbuffer* buf = tr_variantToBuf(request, TR_VARIANT_FMT_JSON_LEAN);
            size_t const buf_len = evbuffer_get_length(buf);
            g_message("request: [%*.*s]", (int)buf_len, (int)buf_len, evbuffer_pullup(buf, -1));
            evbuffer_free(buf);
        }
#endif

        tr_rpc_request_exec_json(session, request, core_read_rpc_response, GINT_TO_POINTER(tag));
    }
}

/***
****  Sending a test-port request via RPC
***/

static void on_port_test_response(TrCore* core, tr_variant* response, gpointer u UNUSED)
{
    tr_variant* args;
    bool is_open;

    if (!tr_variantDictFindDict(response, TR_KEY_arguments, &args) ||
        !tr_variantDictFindBool(args, TR_KEY_port_is_open, &is_open))
    {
        is_open = false;
    }

    core_emit_port_tested(core, is_open);
}

void gtr_core_port_test(TrCore* core)
{
    int const tag = nextTag;
    ++nextTag;

    tr_variant request;
    tr_variantInitDict(&request, 2);
    tr_variantDictAddStr(&request, TR_KEY_method, "port-test");
    tr_variantDictAddInt(&request, TR_KEY_tag, tag);
    core_send_rpc_request(core, &request, tag, on_port_test_response, NULL);
    tr_variantFree(&request);
}

/***
****  Updating a blocklist via RPC
***/

static void on_blocklist_response(TrCore* core, tr_variant* response, gpointer data UNUSED)
{
    tr_variant* args;
    int64_t ruleCount;

    if (!tr_variantDictFindDict(response, TR_KEY_arguments, &args) ||
        !tr_variantDictFindInt(args, TR_KEY_blocklist_size, &ruleCount))
    {
        ruleCount = -1;
    }

    if (ruleCount > 0)
    {
        gtr_pref_int_set(TR_KEY_blocklist_date, tr_time());
    }

    core_emit_blocklist_udpated(core, ruleCount);
}

void gtr_core_blocklist_update(TrCore* core)
{
    int const tag = nextTag;
    ++nextTag;

    tr_variant request;
    tr_variantInitDict(&request, 2);
    tr_variantDictAddStr(&request, TR_KEY_method, "blocklist-update");
    tr_variantDictAddInt(&request, TR_KEY_tag, tag);
    core_send_rpc_request(core, &request, tag, on_blocklist_response, NULL);
    tr_variantFree(&request);
}

/***
****
***/

void gtr_core_exec(TrCore* core, tr_variant const* top)
{
    int const tag = nextTag;
    ++nextTag;

    core_send_rpc_request(core, top, tag, NULL, NULL);
}

/***
****
***/

size_t gtr_core_get_torrent_count(TrCore* core)
{
    return gtk_tree_model_iter_n_children(core_raw_model(core), NULL);
}

size_t gtr_core_get_active_torrent_count(TrCore* core)
{
    GtkTreeIter iter;
    size_t activeCount = 0;
    GtkTreeModel* model = core_raw_model(core);

    if (gtk_tree_model_iter_nth_child(model, &iter, NULL, 0))
    {
        do
        {
            int activity;
            gtk_tree_model_get(model, &iter, MC_ACTIVITY, &activity, -1);

            if (activity != TR_STATUS_STOPPED)
            {
                ++activeCount;
            }
        }
        while (gtk_tree_model_iter_next(model, &iter));
    }

    return activeCount;
}

tr_torrent* gtr_core_find_torrent(TrCore* core, int id)
{
    tr_session* session;
    tr_torrent* tor = NULL;

    if ((session = gtr_core_session(core)) != NULL)
    {
        tor = tr_torrentFindFromId(session, id);
    }

    return tor;
}

void gtr_core_open_folder(TrCore* core, int torrent_id)
{
    tr_torrent const* tor = gtr_core_find_torrent(core, torrent_id);

    if (tor != NULL)
    {
        gboolean const single = tr_torrentInfo(tor)->fileCount == 1;
        char const* currentDir = tr_torrentGetCurrentDir(tor);

        if (single)
        {
            gtr_open_file(currentDir);
        }
        else
        {
            char* path = g_build_filename(currentDir, tr_torrentName(tor), NULL);
            gtr_open_file(path);
            g_free(path);
        }
    }
}
