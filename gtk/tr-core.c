/******************************************************************************
 * $Id$
 *
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
#include <string.h> /* strcmp, strlen */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <event2/buffer.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/json.h>
#include <libtransmission/utils.h> /* tr_free */
#include <libtransmission/web.h>

#include "conf.h"
#include "notify.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"
#include "actions.h"

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

static guint core_signals[LAST_SIGNAL] = { 0 };

static void core_maybe_inhibit_hibernation( TrCore * core );

struct TrCorePrivate
{
    GFileMonitor * monitor;
    gulong         monitor_tag;
    char         * monitor_dir;
    GSList       * monitor_files;
    guint          monitor_idle_tag;

    gboolean       adding_from_watch_dir;
    gboolean       inhibit_allowed;
    gboolean       have_inhibit_cookie;
    gboolean       dbus_error;
    guint          inhibit_cookie;
    guint          dbus_session_owner_id;
    guint          dbus_display_owner_id;
    gint           busy_count;
    GtkTreeModel * raw_model;
    GtkTreeModel * sorted_model;
    tr_session   * session;
    GStringChunk * string_chunk;
};

static int
core_is_disposed( const TrCore * core )
{
    return !core || !core->priv->sorted_model;
}

static void
core_dispose( GObject * o )
{
    TrCore * core = TR_CORE( o );
    GObjectClass * parent = g_type_class_peek( g_type_parent( TR_CORE_TYPE ) );

    if( core->priv->sorted_model != NULL )
    {
        g_object_unref( core->priv->sorted_model );
        core->priv->sorted_model = NULL;
        core->priv->raw_model = NULL;
    }

    parent->dispose( o );
}

static void
core_finalize( GObject * o )
{
    TrCore * core = TR_CORE( o );
    GObjectClass * parent = g_type_class_peek( g_type_parent( TR_CORE_TYPE ) );

    g_string_chunk_free( core->priv->string_chunk );

    parent->finalize( o );
}

static void
gtr_core_class_init( gpointer g_class, gpointer g_class_data UNUSED )
{
    GObjectClass * gobject_class;

    g_type_class_add_private( g_class, sizeof( struct TrCorePrivate ) );

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->dispose = core_dispose;
    gobject_class->finalize = core_finalize;

    core_signals[ADD_ERROR_SIGNAL] = g_signal_new(
        "add-error",
        G_TYPE_FROM_CLASS( g_class ),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(TrCoreClass, add_error),
        NULL, NULL,
        g_cclosure_marshal_VOID__UINT_POINTER,
        G_TYPE_NONE,
        2, G_TYPE_UINT, G_TYPE_POINTER );

    core_signals[ADD_PROMPT_SIGNAL] = g_signal_new(
        "add-prompt",
        G_TYPE_FROM_CLASS( g_class ),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(TrCoreClass, add_prompt),
        NULL, NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE,
        1, G_TYPE_POINTER );

    core_signals[BUSY_SIGNAL] = g_signal_new(
        "busy",
        G_TYPE_FROM_CLASS( g_class ),
        G_SIGNAL_RUN_FIRST,
        G_STRUCT_OFFSET(TrCoreClass, busy),
        NULL, NULL,
        g_cclosure_marshal_VOID__BOOLEAN,
        G_TYPE_NONE,
        1, G_TYPE_BOOLEAN );

    core_signals[BLOCKLIST_SIGNAL] = g_signal_new(
        "blocklist-updated",
        G_TYPE_FROM_CLASS( g_class ),
        G_SIGNAL_RUN_FIRST,
        G_STRUCT_OFFSET(TrCoreClass, blocklist_updated),
        NULL, NULL,
        g_cclosure_marshal_VOID__INT,
        G_TYPE_NONE,
        1, G_TYPE_INT );

    core_signals[PORT_SIGNAL] = g_signal_new(
        "port-tested",
        G_TYPE_FROM_CLASS( g_class ),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(TrCoreClass, port_tested),
        NULL, NULL,
        g_cclosure_marshal_VOID__BOOLEAN,
        G_TYPE_NONE,
        1, G_TYPE_BOOLEAN );

    core_signals[PREFS_SIGNAL] = g_signal_new(
        "prefs-changed",
        G_TYPE_FROM_CLASS( g_class ),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(TrCoreClass, prefs_changed),
        NULL, NULL,
        g_cclosure_marshal_VOID__STRING,
        G_TYPE_NONE,
        1, G_TYPE_STRING );
}

static void
handle_dbus_method( GDBusConnection       * connection UNUSED,
                    const gchar           * sender UNUSED,
                    const gchar           * object_path,
                    const gchar           * interface_name,
                    const gchar           * method_name,
                    GVariant              * parameters,
                    GDBusMethodInvocation * invocation,
                    gpointer                core )
{
    gboolean handled = false;

    if( !g_strcmp0( interface_name, TR_DBUS_SESSION_INTERFACE ) )
    {
        if( !g_strcmp0( method_name, "TorrentAdd" ) )
        {
            GVariant * args = g_variant_get_child_value( parameters, 0 );
            GVariant * filename_variant = g_variant_lookup_value ( args, "filename", G_VARIANT_TYPE_STRING );
            char * filename = g_variant_dup_string( filename_variant, NULL );
            GSList * files = g_slist_append( NULL, filename );
            gtr_core_add_list_defaults( TR_CORE( core ), files, TRUE );
            g_dbus_method_invocation_return_value( invocation, g_variant_new( "(b)", true ) );
            handled = true;
        }
    }
    else if( !g_strcmp0( interface_name, TR_DBUS_DISPLAY_INTERFACE ) )
    {
        if( !g_strcmp0( method_name, "PresentWindow" ) )
        {
            gtr_action_activate( "present-main-window" );
            g_dbus_method_invocation_return_value( invocation, NULL );
            handled = true;
        }
    }

    if( !handled )
        g_warning( "Unhandled method call:\n\tObject Path: %s\n\tInterface Name: %s\n\tMethod Name: %s",
                   object_path, interface_name, method_name );
};

static void
on_session_registered_in_dbus( GDBusConnection *connection, const gchar *name UNUSED, gpointer core )
{
    GError * err = NULL;
    GDBusNodeInfo * node_info;
    GDBusInterfaceVTable vtable;
    const char * interface_xml = "<node>"
                                 "  <interface name='" TR_DBUS_SESSION_INTERFACE "'>"
                                 "    <method name='TorrentAdd'>"
                                 "      <arg type='a{sv}' name='args' direction='in'/>"
                                 "      <arg type='b' name='response' direction='out'/>"
                                 "    </method>"
                                 "  </interface>"
                                 "</node>";

    node_info = g_dbus_node_info_new_for_xml( interface_xml, &err );

    vtable.method_call = handle_dbus_method;
    vtable.get_property = NULL;
    vtable.set_property = NULL;

    g_dbus_connection_register_object ( connection,
                                        TR_DBUS_SESSION_OBJECT_PATH,
                                        node_info->interfaces[0],
                                        &vtable,
                                        core,
                                        NULL,
                                        &err );

    if( err != NULL ) {
        g_warning( "%s:%d Error registering object: %s", __FILE__, __LINE__, err->message );
        g_error_free( err );
    }
}

static void
on_display_registered_in_dbus( GDBusConnection *connection, const gchar *name UNUSED, gpointer core )
{
    GError * err = NULL;
    const char * interface_xml = "<node>"
                                 "  <interface name='" TR_DBUS_DISPLAY_INTERFACE "'>"
                                 "    <method name='PresentWindow'>"
                                 "    </method>"
                                 "  </interface>"
                                 "</node>";
    GDBusInterfaceVTable vtable = { .method_call=handle_dbus_method };
    GDBusNodeInfo * node_info = g_dbus_node_info_new_for_xml( interface_xml, &err );

    g_dbus_connection_register_object ( connection,
                                        TR_DBUS_DISPLAY_OBJECT_PATH,
                                        node_info->interfaces[0],
                                        &vtable,
                                        core,
                                        NULL,
                                        &err );

    if( err != NULL ) {
        g_warning( "%s:%d Error registering object: %s", __FILE__, __LINE__, err->message );
        g_error_free( err );
    }
}

static void
core_init( GTypeInstance * instance, gpointer g_class UNUSED )
{
    GtkListStore * store;
    struct TrCorePrivate * p;
    TrCore * self = (TrCore *) instance;

    /* column types for the model used to store torrent information */
    /* keep this in sync with the enum near the bottom of tr_core.h */
    GType types[] = { G_TYPE_POINTER,   /* collated name */
                      G_TYPE_POINTER,   /* tr_torrent* */
                      G_TYPE_INT,       /* torrent id */
                      G_TYPE_DOUBLE,    /* tr_stat.pieceUploadSpeed_KBps */
                      G_TYPE_DOUBLE,    /* tr_stat.pieceDownloadSpeed_KBps */
                      G_TYPE_DOUBLE,    /* tr_stat.recheckProgress */
                      G_TYPE_BOOLEAN,   /* filter.c:ACTIVITY_FILTER_ACTIVE */
                      G_TYPE_INT,       /* tr_stat.activity */
                      G_TYPE_UCHAR,     /* tr_stat.finished */
                      G_TYPE_CHAR,      /* tr_priority_t */
                      G_TYPE_INT,       /* tr_stat.queuePosition */
                      G_TYPE_UINT,      /* build_torrent_trackers_hash() */
                      G_TYPE_INT,       /* MC_ERROR */
                      G_TYPE_INT };     /* MC_ACTIVE_PEER_COUNT */

    p = self->priv = G_TYPE_INSTANCE_GET_PRIVATE( self,
                                                  TR_CORE_TYPE,
                                                  struct TrCorePrivate );

    /* create the model used to store torrent data */
    g_assert( G_N_ELEMENTS( types ) == MC_ROW_COUNT );
    store = gtk_list_store_newv( MC_ROW_COUNT, types );

    p->raw_model = GTK_TREE_MODEL( store );
    p->sorted_model = gtk_tree_model_sort_new_with_model( p->raw_model );
    p->string_chunk = g_string_chunk_new( 2048 );
    g_object_unref( p->raw_model );

    p->dbus_session_owner_id = g_bus_own_name( G_BUS_TYPE_SESSION,
                                               TR_DBUS_SESSION_SERVICE_NAME,
                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                               NULL,
                                               on_session_registered_in_dbus,
                                               NULL,
                                               self,
                                               NULL );

    p->dbus_display_owner_id = g_bus_own_name( G_BUS_TYPE_SESSION,
                                               TR_DBUS_DISPLAY_SERVICE_NAME,
                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                               NULL,
                                               on_display_registered_in_dbus,
                                               NULL,
                                               self,
                                               NULL );
}

GType
gtr_core_get_type( void )
{
    static GType type = 0;

    if( !type )
    {
        static const GTypeInfo info =
        {
            sizeof( TrCoreClass ),
            NULL,                 /* base_init */
            NULL,                 /* base_finalize */
            gtr_core_class_init,  /* class_init */
            NULL,                 /* class_finalize */
            NULL,                 /* class_data */
            sizeof( TrCore ),
            0,                    /* n_preallocs */
            core_init,            /* instance_init */
            NULL,
        };
        type = g_type_register_static( G_TYPE_OBJECT, "TrCore", &info, 0 );
    }

    return type;
}

/***
****  EMIT SIGNALS
***/

static inline void
core_emit_blocklist_udpated( TrCore * core, int ruleCount )
{
    g_signal_emit( core, core_signals[BLOCKLIST_SIGNAL], 0, ruleCount );
}

static inline void
core_emit_port_tested( TrCore * core, gboolean is_open )
{
    g_signal_emit( core, core_signals[PORT_SIGNAL], 0, is_open );
}

static inline void
core_emit_err( TrCore * core, enum tr_core_err type, const char * msg )
{
    g_signal_emit( core, core_signals[ADD_ERROR_SIGNAL], 0, type, msg );
}

static inline void
core_emit_busy( TrCore * core, gboolean is_busy )
{
    g_signal_emit( core, core_signals[BUSY_SIGNAL], 0, is_busy );
}

void
gtr_core_pref_changed( TrCore * core, const char * key )
{
    g_signal_emit( core, core_signals[PREFS_SIGNAL], 0, key );
}

/***
****
***/

static GtkTreeModel *
core_raw_model( TrCore * core )
{
    return core_is_disposed( core ) ? NULL : core->priv->raw_model;
}

GtkTreeModel *
gtr_core_model( TrCore * core )
{
    return core_is_disposed( core ) ? NULL : core->priv->sorted_model;
}

tr_session *
gtr_core_session( TrCore * core )
{
    return core_is_disposed( core ) ? NULL : core->priv->session;
}

/***
****  BUSY
***/

static bool
core_is_busy( TrCore * core )
{
    return core->priv->busy_count > 0;
}

static void
core_add_to_busy( TrCore * core, int addMe )
{
    const bool wasBusy = core_is_busy( core );

    core->priv->busy_count += addMe;

    if( wasBusy != core_is_busy( core ) )
        core_emit_busy( core, core_is_busy( core ) );
}

static void core_inc_busy( TrCore * core ) { core_add_to_busy( core, 1 ); }
static void core_dec_busy( TrCore * core ) { core_add_to_busy( core, -1 ); }

/***
****
****  SORTING THE MODEL
****
***/

static gboolean
is_valid_eta( int t )
{
    return ( t != TR_ETA_NOT_AVAIL ) && ( t != TR_ETA_UNKNOWN );
}

static int
compare_eta( int a, int b )
{
    const gboolean a_valid = is_valid_eta( a );
    const gboolean b_valid = is_valid_eta( b );

    if( !a_valid && !b_valid ) return 0;
    if( !a_valid ) return -1;
    if( !b_valid ) return 1;
    return a < b ? 1 : -1;
}

static int
compare_double( double a, double b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compare_uint64( uint64_t a, uint64_t b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compare_int( int a, int b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compare_ratio( double a, double b )
{
    if( (int)a == TR_RATIO_INF && (int)b == TR_RATIO_INF ) return 0;
    if( (int)a == TR_RATIO_INF ) return 1;
    if( (int)b == TR_RATIO_INF ) return -1;
    return compare_double( a, b );
}

static int
compare_time( time_t a, time_t b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compare_by_name( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data UNUSED )
{
    const char *ca, *cb;
    gtk_tree_model_get( m, a, MC_NAME_COLLATED, &ca, -1 );
    gtk_tree_model_get( m, b, MC_NAME_COLLATED, &cb, -1 );
    return tr_strcmp0( ca, cb );
}

static int
compare_by_queue( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data UNUSED )
{
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;

    gtk_tree_model_get( m, a, MC_TORRENT, &ta, -1 );
    sa = tr_torrentStatCached( ta );
    gtk_tree_model_get( m, b, MC_TORRENT, &tb, -1 );
    sb = tr_torrentStatCached( tb );

    return sb->queuePosition - sa->queuePosition;
}

static int
compare_by_ratio( GtkTreeModel* m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data )
{
    int ret = 0;
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;

    gtk_tree_model_get( m, a, MC_TORRENT, &ta, -1 );
    sa = tr_torrentStatCached( ta );
    gtk_tree_model_get( m, b, MC_TORRENT, &tb, -1 );
    sb = tr_torrentStatCached( tb );

    if( !ret ) ret = compare_ratio( sa->ratio, sb->ratio );
    if( !ret ) ret = compare_by_queue( m, a, b, user_data );
    return ret;
}

static int
compare_by_activity( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data )
{
    int ret = 0;
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;
    double aUp, aDown, bUp, bDown;

    gtk_tree_model_get( m, a, MC_SPEED_UP, &aUp,
                              MC_SPEED_DOWN, &aDown,
                              MC_TORRENT, &ta,
                              -1 );
    gtk_tree_model_get( m, b, MC_SPEED_UP, &bUp,
                              MC_SPEED_DOWN, &bDown,
                              MC_TORRENT, &tb,
                              -1 );
    sa = tr_torrentStatCached( ta );
    sb = tr_torrentStatCached( tb );

    if( !ret ) ret = compare_double( aUp+aDown, bUp+bDown );
    if( !ret ) ret = compare_uint64( sa->uploadedEver, sb->uploadedEver );
    if( !ret ) ret = compare_by_queue( m, a, b, user_data );
    return ret;
}

static int
compare_by_age( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer u )
{
    int ret = 0;
    tr_torrent *ta, *tb;

    gtk_tree_model_get( m, a, MC_TORRENT, &ta, -1 );
    gtk_tree_model_get( m, b, MC_TORRENT, &tb, -1 );

    if( !ret ) ret = compare_time( tr_torrentStatCached( ta )->addedDate,
                                   tr_torrentStatCached( tb )->addedDate );
    if( !ret ) ret = compare_by_name( m, a, b, u );
    return ret;
}

static int
compare_by_size( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer u )
{
    int ret = 0;
    tr_torrent *t;
    const tr_info *ia, *ib;

    gtk_tree_model_get( m, a, MC_TORRENT, &t, -1 );
    ia = tr_torrentInfo( t );
    gtk_tree_model_get( m, b, MC_TORRENT, &t, -1 );
    ib = tr_torrentInfo( t );

    if( !ret ) ret = compare_uint64( ia->totalSize, ib->totalSize );
    if( !ret ) ret = compare_by_name( m, a, b, u );
    return ret;
}

static int
compare_by_progress( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer u )
{
    int ret = 0;
    tr_torrent * t;
    const tr_stat *sa, *sb;

    gtk_tree_model_get( m, a, MC_TORRENT, &t, -1 );
    sa = tr_torrentStatCached( t );
    gtk_tree_model_get( m, b, MC_TORRENT, &t, -1 );
    sb = tr_torrentStatCached( t );

    if( !ret ) ret = compare_double( sa->percentComplete, sb->percentComplete );
    if( !ret ) ret = compare_double( sa->seedRatioPercentDone, sb->seedRatioPercentDone );
    if( !ret ) ret = compare_by_ratio( m, a, b, u );
    return ret;
}

static int
compare_by_eta( GtkTreeModel * m, GtkTreeIter  * a, GtkTreeIter  * b, gpointer u )
{
    int ret = 0;
    tr_torrent *ta, *tb;

    gtk_tree_model_get( m, a, MC_TORRENT, &ta, -1 );
    gtk_tree_model_get( m, b, MC_TORRENT, &tb, -1 );

    if( !ret ) ret = compare_eta( tr_torrentStatCached( ta )->eta,
                                  tr_torrentStatCached( tb )->eta );
    if( !ret ) ret = compare_by_name( m, a, b, u );
    return ret;
}

static int
compare_by_state( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer u )
{
    int ret = 0;
    int sa, sb;
    tr_torrent *ta, *tb;

    gtk_tree_model_get( m, a, MC_ACTIVITY, &sa, MC_TORRENT, &ta, -1 );
    gtk_tree_model_get( m, b, MC_ACTIVITY, &sb, MC_TORRENT, &tb, -1 );

    if( !ret ) ret = compare_int( sa, sb );
    if( !ret ) ret = compare_by_queue( m, a, b, u );
    return ret;
}

static void
core_set_sort_mode( TrCore * core, const char * mode, gboolean is_reversed )
{
    const int col = MC_TORRENT;
    GtkTreeIterCompareFunc sort_func;
    GtkSortType type = is_reversed ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
    GtkTreeSortable * sortable = GTK_TREE_SORTABLE( gtr_core_model( core ) );

    if( !strcmp( mode, "sort-by-activity" ) )
        sort_func = compare_by_activity;
    else if( !strcmp( mode, "sort-by-age" ) )
        sort_func = compare_by_age;
    else if( !strcmp( mode, "sort-by-progress" ) )
        sort_func = compare_by_progress;
    else if( !strcmp( mode, "sort-by-queue" ) )
        sort_func = compare_by_queue;
    else if( !strcmp( mode, "sort-by-time-left" ) )
        sort_func = compare_by_eta;
    else if( !strcmp( mode, "sort-by-ratio" ) )
        sort_func = compare_by_ratio;
    else if( !strcmp( mode, "sort-by-state" ) )
        sort_func = compare_by_state;
    else if( !strcmp( mode, "sort-by-size" ) )
        sort_func = compare_by_size;
    else {
        sort_func = compare_by_name;
        type = is_reversed ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    }

    gtk_tree_sortable_set_sort_func( sortable, col, sort_func, NULL, NULL );
    gtk_tree_sortable_set_sort_column_id( sortable, col, type );
}

/***
****
****  WATCHDIR
****
***/

struct watchdir_file
{
    char * filename;
    time_t mtime;
};

static int
compare_watchdir_file_to_filename( const void * a, const void * filename )
{
    return strcmp( ((const struct watchdir_file*)a)->filename, filename );
}

static time_t
get_file_mtime( const char * filename )
{
    time_t mtime;
    GFile * gfile = g_file_new_for_path( filename );
    GFileInfo * info = g_file_query_info( gfile, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, NULL );
    mtime = g_file_info_get_attribute_uint64( info, G_FILE_ATTRIBUTE_TIME_MODIFIED );
    g_object_unref( G_OBJECT( info ) );
    g_object_unref( G_OBJECT( gfile ) );
    return mtime;
}

static struct watchdir_file*
watchdir_file_new( const char * filename )
{
    struct watchdir_file * f;
    f = g_new( struct watchdir_file, 1 );
    f->filename = g_strdup( filename );
    f->mtime = get_file_mtime( filename );
    return f;
}

static void
watchdir_file_free( struct watchdir_file * f )
{
    g_free( f->filename );
    g_free( f );
}

static gboolean
core_watchdir_idle( gpointer gcore )
{
    GSList * l;
    GSList * addme = NULL;
    GSList * monitor_files = NULL;
    TrCore * core = TR_CORE( gcore );
    const time_t now = tr_time( );
    struct TrCorePrivate * p = core->priv;

    /* of the monitor_files, make a list of those that haven't
     * changed lately, since they should be ready to add */
    for( l=p->monitor_files; l!=NULL; l=l->next ) {
        struct watchdir_file * f = l->data;
        f->mtime = get_file_mtime( f->filename );
        if( f->mtime + 2 >= now )
            monitor_files = g_slist_prepend( monitor_files, f );
        else {
            addme = g_slist_prepend( addme, g_strdup( f->filename ) );
            watchdir_file_free( f );
        }
    }

    /* add the torrents from that list */
    core->priv->adding_from_watch_dir = TRUE;
    gtr_core_add_list_defaults( core, addme, TRUE );
    core->priv->adding_from_watch_dir = FALSE;

    /* update the monitor_files list */
    g_slist_free( p->monitor_files );
    p->monitor_files = monitor_files;

    /* if monitor_files is nonempty, keep checking every second */
    if( core->priv->monitor_files )
        return TRUE;
    core->priv->monitor_idle_tag = 0;
    return FALSE;

}

static void
core_watchdir_monitor_file( TrCore * core, const char * filename )
{
    const gboolean isTorrent = g_str_has_suffix( filename, ".torrent" );

    if( isTorrent )
    {
        struct TrCorePrivate * p = core->priv;

        if( !g_slist_find_custom( p->monitor_files, filename, (GCompareFunc)compare_watchdir_file_to_filename ) )
            p->monitor_files = g_slist_append( p->monitor_files, watchdir_file_new( filename ) );

        if( !p->monitor_idle_tag )
            p->monitor_idle_tag = gdk_threads_add_timeout_seconds( 1, core_watchdir_idle, core );
    }
}

static void
on_file_changed_in_watchdir( GFileMonitor       * monitor UNUSED,
                             GFile              * file,
                             GFile              * other_type UNUSED,
                             GFileMonitorEvent    event_type,
                             gpointer             core )
{
    if( event_type == G_FILE_MONITOR_EVENT_CREATED )
    {
        char * filename = g_file_get_path( file );
        core_watchdir_monitor_file( core, filename );
        g_free( filename );
    }
}

static void
core_watchdir_scan( TrCore * core )
{
    const gboolean is_enabled = gtr_pref_flag_get( PREF_KEY_DIR_WATCH_ENABLED );

    if( is_enabled )
    {
        const char * dirname = gtr_pref_string_get( PREF_KEY_DIR_WATCH );
        GDir * dir = g_dir_open( dirname, 0, NULL );

        if( dir != NULL )
        {
            const char * basename;
            while(( basename = g_dir_read_name( dir )))
            {
                char * filename = g_build_filename( dirname, basename, NULL );
                core_watchdir_monitor_file( core, filename );
                g_free( filename );
            }

            g_dir_close( dir );
        }
    }
}

static void
core_watchdir_update( TrCore * core )
{
    const char * dir = gtr_pref_string_get( PREF_KEY_DIR_WATCH );
    const gboolean is_enabled = gtr_pref_flag_get( PREF_KEY_DIR_WATCH_ENABLED );
    struct TrCorePrivate * p = TR_CORE( core )->priv;

    if( p->monitor && ( !is_enabled || tr_strcmp0( dir, p->monitor_dir ) ) )
    {
        g_signal_handler_disconnect( p->monitor, p->monitor_tag );
        g_free( p->monitor_dir );
        g_file_monitor_cancel( p->monitor );
        g_object_unref( G_OBJECT( p->monitor ) );
        p->monitor_dir = NULL;
        p->monitor = NULL;
        p->monitor_tag = 0;
    }

    if( is_enabled && !p->monitor )
    {
        GFile * file = g_file_new_for_path( dir );
        GFileMonitor * m = g_file_monitor_directory( file, 0, NULL, NULL );
        core_watchdir_scan( core );
        p->monitor = m;
        p->monitor_dir = g_strdup( dir );
        p->monitor_tag = g_signal_connect( m, "changed",
                                           G_CALLBACK( on_file_changed_in_watchdir ), core );
    }
}

/***
****
***/

static void
on_pref_changed( TrCore * core, const char * key, gpointer data UNUSED )
{
    if( !strcmp( key, PREF_KEY_SORT_MODE )
      || !strcmp( key, PREF_KEY_SORT_REVERSED ) )
    {
        const char * mode = gtr_pref_string_get( PREF_KEY_SORT_MODE );
        gboolean is_reversed = gtr_pref_flag_get( PREF_KEY_SORT_REVERSED );
        core_set_sort_mode( core, mode, is_reversed );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_LIMIT_GLOBAL ) )
    {
        tr_sessionSetPeerLimit( gtr_core_session( core ),
                                gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_LIMIT_TORRENT ) )
    {
        tr_sessionSetPeerLimitPerTorrent( gtr_core_session( core ),
                                          gtr_pref_int_get( key ) );
    }
    else if( !strcmp( key, PREF_KEY_INHIBIT_HIBERNATION ) )
    {
        core_maybe_inhibit_hibernation( core );
    }
    else if( !strcmp( key, PREF_KEY_DIR_WATCH )
           || !strcmp( key, PREF_KEY_DIR_WATCH_ENABLED ) )
    {
        core_watchdir_update( core );
    }
}

/**
***
**/

TrCore *
gtr_core_new( tr_session * session )
{
    TrCore * core = TR_CORE( g_object_new( TR_CORE_TYPE, NULL ) );

    core->priv->session = session;

    /* init from prefs & listen to pref changes */
    on_pref_changed( core, PREF_KEY_SORT_MODE, NULL );
    on_pref_changed( core, PREF_KEY_SORT_REVERSED, NULL );
    on_pref_changed( core, PREF_KEY_DIR_WATCH_ENABLED, NULL );
    on_pref_changed( core, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, NULL );
    on_pref_changed( core, PREF_KEY_INHIBIT_HIBERNATION, NULL );
    g_signal_connect( core, "prefs-changed", G_CALLBACK( on_pref_changed ), NULL );

    return core;
}

void
gtr_core_close( TrCore * core )
{
    tr_session * session = gtr_core_session( core );

    if( session )
    {
        core->priv->session = NULL;
        gtr_pref_save( session );
        tr_sessionClose( session );
    }

    g_bus_unown_name( core->priv->dbus_display_owner_id );
    g_bus_unown_name( core->priv->dbus_session_owner_id );
}

/***
****  COMPLETENESS CALLBACK
***/

struct notify_callback_data
{
    TrCore * core;
    int torrent_id;
};

static gboolean
on_torrent_completeness_changed_idle( gpointer gdata )
{
    struct notify_callback_data * data = gdata;
    gtr_notify_torrent_completed( data->core, data->torrent_id );
    g_object_unref( G_OBJECT( data->core ) );
    g_free( data );
    return FALSE;
}

/* this is called in the libtransmission thread, *NOT* the GTK+ thread,
   so delegate to the GTK+ thread before calling notify's dbus code... */
static void
on_torrent_completeness_changed( tr_torrent       * tor,
                                 tr_completeness    completeness,
                                 bool               was_running,
                                 void             * gcore )
{
    if( was_running && ( completeness != TR_LEECH ) && ( tr_torrentStat( tor )->sizeWhenDone != 0 ) )
    {
        struct notify_callback_data * data = g_new( struct notify_callback_data, 1 );
        data->core = gcore;
        data->torrent_id = tr_torrentId( tor );
        g_object_ref( G_OBJECT( data->core ) );
        gdk_threads_add_idle( on_torrent_completeness_changed_idle, data );
    }
}

/***
****  METADATA CALLBACK
***/

static const char*
get_collated_name( TrCore * core, const tr_torrent * tor )
{
    char buf[2048];
    const char * name = tr_torrentName( tor );
    char * down = g_utf8_strdown( name ? name : "", -1 );
    const tr_info * inf = tr_torrentInfo( tor );
    g_snprintf( buf, sizeof( buf ), "%s\t%s", down, inf->hashString );
    g_free( down );
    return g_string_chunk_insert_const( core->priv->string_chunk, buf );
}

struct metadata_callback_data
{
    TrCore * core;
    int torrent_id;
};

static gboolean
find_row_from_torrent_id( GtkTreeModel * model, int id, GtkTreeIter * setme )
{
    GtkTreeIter iter;
    gboolean match = FALSE;

    if( gtk_tree_model_iter_children( model, &iter, NULL ) ) do
    {
        int row_id;
        gtk_tree_model_get( model, &iter, MC_TORRENT_ID, &row_id, -1 );
        match = id == row_id;
    }
    while( !match && gtk_tree_model_iter_next( model, &iter ) );

    if( match )
        *setme = iter;

    return match;
}

static gboolean
on_torrent_metadata_changed_idle( gpointer gdata )
{
    struct notify_callback_data * data = gdata;
    tr_session * session = gtr_core_session( data->core );
    tr_torrent * tor = tr_torrentFindFromId( session, data->torrent_id );

    /* update the torrent's collated name */
    if( tor != NULL ) {
        GtkTreeIter iter;
        GtkTreeModel * model = core_raw_model( data->core );
        if( find_row_from_torrent_id( model, data->torrent_id, &iter ) ) {
            const char * collated = get_collated_name( data->core, tor );
            GtkListStore * store = GTK_LIST_STORE( model );
            gtk_list_store_set( store, &iter, MC_NAME_COLLATED, collated, -1 );
        }
    }

    /* cleanup */
    g_object_unref( G_OBJECT( data->core ) );
    g_free( data );
    return FALSE;
}

/* this is called in the libtransmission thread, *NOT* the GTK+ thread,
   so delegate to the GTK+ thread before changing our list store... */
static void
on_torrent_metadata_changed( tr_torrent * tor, void * gcore )
{
    struct notify_callback_data * data = g_new( struct notify_callback_data, 1 );
    data->core = gcore;
    data->torrent_id = tr_torrentId( tor );
    g_object_ref( G_OBJECT( data->core ) );
    gdk_threads_add_idle( on_torrent_metadata_changed_idle, data );
}

/***
****
****  ADDING TORRENTS
****
***/

static unsigned int
build_torrent_trackers_hash( tr_torrent * tor )
{
    int i;
    const char * pch;
    uint64_t hash = 0;
    const tr_info * const inf = tr_torrentInfo( tor );

    for( i=0; i<inf->trackerCount; ++i )
        for( pch=inf->trackers[i].announce; *pch; ++pch )
            hash = (hash<<4) ^ (hash>>28) ^ *pch;

    return hash;
}

static gboolean
is_torrent_active( const tr_stat * st )
{
    return ( st->peersSendingToUs > 0 )
        || ( st->peersGettingFromUs > 0 )
        || ( st->activity == TR_STATUS_CHECK );
}

void
gtr_core_add_torrent( TrCore * core, tr_torrent * tor, gboolean do_notify )
{
    if( tor != NULL )
    {
        GtkTreeIter unused;
        const tr_stat * st = tr_torrentStat( tor );
        const char * collated = get_collated_name( core, tor );
        const unsigned int trackers_hash = build_torrent_trackers_hash( tor );
        GtkListStore * store = GTK_LIST_STORE( core_raw_model( core ) );

        gtk_list_store_insert_with_values( store, &unused, 0,
            MC_NAME_COLLATED,     collated,
            MC_TORRENT,           tor,
            MC_TORRENT_ID,        tr_torrentId( tor ),
            MC_SPEED_UP,          st->pieceUploadSpeed_KBps,
            MC_SPEED_DOWN,        st->pieceDownloadSpeed_KBps,
            MC_RECHECK_PROGRESS,  st->recheckProgress,
            MC_ACTIVE,            is_torrent_active( st ),
            MC_ACTIVITY,          st->activity,
            MC_FINISHED,          st->finished,
            MC_PRIORITY,          tr_torrentGetPriority( tor ),
            MC_QUEUE_POSITION,    st->queuePosition,
            MC_TRACKERS,          trackers_hash,
            -1 );

        if( do_notify )
            gtr_notify_torrent_added( tr_torrentName( tor ) );

        tr_torrentSetMetadataCallback( tor, on_torrent_metadata_changed, core );
        tr_torrentSetCompletenessCallback( tor, on_torrent_completeness_changed, core );
    }
}

static tr_torrent *
core_create_new_torrent( TrCore * core, tr_ctor * ctor )
{
    int errcode = 0;
    tr_torrent * tor;
    bool do_trash = false;
    tr_session * session = gtr_core_session( core );

    /* let the gtk client handle the removal, since libT
     * doesn't have any concept of the glib trash API */
    tr_ctorGetDeleteSource( ctor, &do_trash );
    tr_ctorSetDeleteSource( ctor, FALSE );
    tor = tr_torrentNew( ctor, &errcode );

    if( tor && do_trash )
    {
        const char * config = tr_sessionGetConfigDir( session );
        const char * source = tr_ctorGetSourceFile( ctor );
        const int is_internal = source && ( strstr( source, config ) == source );

        /* #1294: don't delete the .torrent file if it's our internal copy */
        if( !is_internal )
            gtr_file_trash_or_remove( source );
    }

    return tor;
}

static int
core_add_ctor( TrCore * core, tr_ctor * ctor,
               gboolean do_prompt, gboolean do_notify )
{
    tr_info inf;
    int err = tr_torrentParse( ctor, &inf );

    switch( err )
    {
        case TR_PARSE_ERR:
            break;

        case TR_PARSE_DUPLICATE:
            /* don't complain about .torrent files in the watch directory
             * that have already been added... that gets annoying and we
             * don't want to be nagging users to clean up their watch dirs */
            if( !tr_ctorGetSourceFile(ctor) || !core->priv->adding_from_watch_dir )
                core_emit_err( core, err, inf.name );
            tr_metainfoFree( &inf );
            tr_ctorFree( ctor );
            break;

        default:
            if( do_prompt )
                g_signal_emit( core, core_signals[ADD_PROMPT_SIGNAL], 0, ctor );
            else {
                gtr_core_add_torrent( core, core_create_new_torrent( core, ctor ), do_notify );
                tr_ctorFree( ctor );
            }
            tr_metainfoFree( &inf );
            break;
    }

    return err;
}

static void
core_apply_defaults( tr_ctor * ctor )
{
    if( tr_ctorGetPaused( ctor, TR_FORCE, NULL ) )
        tr_ctorSetPaused( ctor, TR_FORCE, !gtr_pref_flag_get( TR_PREFS_KEY_START ) );

    if( tr_ctorGetDeleteSource( ctor, NULL ) )
        tr_ctorSetDeleteSource( ctor,
                               gtr_pref_flag_get( TR_PREFS_KEY_TRASH_ORIGINAL ) );

    if( tr_ctorGetPeerLimit( ctor, TR_FORCE, NULL ) )
        tr_ctorSetPeerLimit( ctor, TR_FORCE,
                             gtr_pref_int_get( TR_PREFS_KEY_PEER_LIMIT_TORRENT ) );

    if( tr_ctorGetDownloadDir( ctor, TR_FORCE, NULL ) )
        tr_ctorSetDownloadDir( ctor, TR_FORCE,
                               gtr_pref_string_get( TR_PREFS_KEY_DOWNLOAD_DIR ) );
}

void
gtr_core_add_ctor( TrCore * core, tr_ctor * ctor )
{
    const gboolean do_notify = FALSE;
    const gboolean do_prompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );
    core_apply_defaults( ctor );
    core_add_ctor( core, ctor, do_prompt, do_notify );
}

/***
****
***/

struct url_dialog_data
{
    TrCore * core;
    tr_ctor * ctor;
    char * url;

    bool did_connect;
    bool did_timeout;
    long response_code;
};

static gboolean
on_url_done_idle( gpointer vdata )
{
    struct url_dialog_data * data = vdata;

    if( data->response_code != 200 )
    {
        gtr_http_failure_dialog( NULL, data->url, data->response_code );
    }
    else
    {
        const gboolean do_prompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );
        const gboolean do_notify = FALSE;
        const int err = core_add_ctor( data->core, data->ctor, do_prompt, do_notify );

        if( err == TR_PARSE_ERR )
            core_emit_err( data->core, TR_PARSE_ERR, data->url );

        gtr_core_torrents_added( data->core );
    }

    /* cleanup */
    core_dec_busy( data->core );
    g_free( data->url );
    g_free( data );
    return FALSE;
}

static void
on_url_done( tr_session   * session,
             bool           did_connect,
             bool           did_timeout,
             long           response_code,
             const void   * response,
             size_t         response_byte_count,
             void         * vdata )
{
    struct url_dialog_data * data = vdata;

    data->did_connect = did_connect;
    data->did_timeout = did_timeout;
    data->response_code = response_code;
    data->ctor = tr_ctorNew( session );
    core_apply_defaults( data->ctor );
    tr_ctorSetMetainfo( data->ctor, response, response_byte_count );

    gdk_threads_add_idle( on_url_done_idle, data );
}

void
gtr_core_add_from_url( TrCore * core, const char * url )
{
    tr_session * session = gtr_core_session( core );
    const gboolean is_magnet_link = gtr_is_magnet_link( url );

    if( is_magnet_link || gtr_is_hex_hashcode( url ) )
    {
        int err;
        char * tmp = NULL;
        tr_ctor * ctor = tr_ctorNew( session );

        if( gtr_is_hex_hashcode( url ) )
            url = tmp = g_strdup_printf( "magnet:?xt=urn:btih:%s", url );

        err = tr_ctorSetMetainfoFromMagnetLink( ctor, url );

        if( !err )
            gtr_core_add_ctor( core, ctor );
        else {
            gtr_unrecognized_url_dialog( NULL, url );
            tr_ctorFree( ctor );
        }

        g_free( tmp );
    }
    else
    {
        struct url_dialog_data * data = g_new( struct url_dialog_data, 1 );
        data->core = core;
        data->url = g_strdup( url );
        core_inc_busy( data->core );
        tr_webRun( session, url, NULL, NULL, on_url_done, data );
    }
}

/***
****
***/

static void
add_filename( TrCore      * core,
              const char  * filename,
              gboolean      do_start,
              gboolean      do_prompt,
              gboolean      do_notify )
{
    tr_session * session = gtr_core_session( core );

    if( session == NULL )
        return;

    if( gtr_is_supported_url( filename ) || gtr_is_magnet_link( filename ) )
    {
        gtr_core_add_from_url( core, filename );
    }
    else if( g_file_test( filename, G_FILE_TEST_EXISTS ) )
    {
        int err;

        tr_ctor * ctor = tr_ctorNew( session );
        tr_ctorSetMetainfoFromFile( ctor, filename );
        core_apply_defaults( ctor );
        tr_ctorSetPaused( ctor, TR_FORCE, !do_start );

        err = core_add_ctor( core, ctor, do_prompt, do_notify );
        if( err == TR_PARSE_ERR )
            core_emit_err( core, TR_PARSE_ERR, filename );
    }
    else if( gtr_is_hex_hashcode( filename ) )
    {
        gtr_core_add_from_url( core, filename );
    }
}

void
gtr_core_add_list( TrCore    * core,
                   GSList    * files,
                   gboolean    do_start,
                   gboolean    do_prompt,
                   gboolean    do_notify )
{
    GSList * l;

    for( l=files; l!=NULL; l=l->next )
    {
        char * filename = l->data;
        add_filename( core, filename, do_start, do_prompt, do_notify );
        g_free( filename );
    }

    gtr_core_torrents_added( core );

    g_slist_free( files );
}

void
gtr_core_add_list_defaults( TrCore * core, GSList * files, gboolean do_notify )
{
    const gboolean do_start = gtr_pref_flag_get( TR_PREFS_KEY_START );
    const gboolean do_prompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );

    gtr_core_add_list( core, files, do_start, do_prompt, do_notify );
}

void
gtr_core_torrents_added( TrCore * self )
{
    gtr_core_update( self );
    core_emit_err( self, TR_CORE_ERR_NO_MORE_TORRENTS, NULL );
}

void
gtr_core_remove_torrent( TrCore * core, int id, gboolean delete_local_data )
{
    tr_torrent * tor = gtr_core_find_torrent( core, id );

    if( tor != NULL )
    {
        /* remove from the gui */
        GtkTreeIter iter;
        GtkTreeModel * model = core_raw_model( core );
        if( find_row_from_torrent_id( model, id, &iter ) )
            gtk_list_store_remove( GTK_LIST_STORE( model ), &iter );

        /* remove the torrent */
        tr_torrentRemove( tor, delete_local_data, gtr_file_trash_or_remove );
    }
}

void
gtr_core_load( TrCore * self, gboolean forcePaused )
{
    int i;
    tr_ctor * ctor;
    tr_torrent ** torrents;
    int count = 0;

    ctor = tr_ctorNew( gtr_core_session( self ) );
    if( forcePaused )
        tr_ctorSetPaused( ctor, TR_FORCE, TRUE );
    tr_ctorSetPeerLimit( ctor, TR_FALLBACK,
                         gtr_pref_int_get( TR_PREFS_KEY_PEER_LIMIT_TORRENT ) );

    torrents = tr_sessionLoadTorrents ( gtr_core_session( self ), ctor, &count );
    for( i=0; i<count; ++i )
        gtr_core_add_torrent( self, torrents[i], FALSE );

    tr_free( torrents );
    tr_ctorFree( ctor );
}

void
gtr_core_clear( TrCore * self )
{
    gtk_list_store_clear( GTK_LIST_STORE( core_raw_model( self ) ) );
}

/***
****
***/

static int
gtr_compare_double( const double a, const double b, int decimal_places )
{
    const int64_t ia = (int64_t)(a * pow( 10, decimal_places ) );
    const int64_t ib = (int64_t)(b * pow( 10, decimal_places ) );
    if( ia < ib ) return -1;
    if( ia > ib ) return  1;
    return 0;
}

static void
update_foreach( GtkTreeModel * model, GtkTreeIter * iter )
{
    int oldActivity, newActivity;
    int oldActivePeerCount, newActivePeerCount;
    int oldError, newError;
    bool oldFinished, newFinished;
    int oldQueuePosition, newQueuePosition;
    tr_priority_t oldPriority, newPriority;
    unsigned int oldTrackers, newTrackers;
    double oldUpSpeed, newUpSpeed;
    double oldDownSpeed, newDownSpeed;
    double oldRecheckProgress, newRecheckProgress;
    gboolean oldActive, newActive;
    const tr_stat * st;
    tr_torrent * tor;

    /* get the old states */
    gtk_tree_model_get( model, iter,
                        MC_TORRENT,  &tor,
                        MC_ACTIVE, &oldActive,
                        MC_ACTIVE_PEER_COUNT, &oldActivePeerCount,
                        MC_ERROR, &oldError,
                        MC_ACTIVITY, &oldActivity,
                        MC_FINISHED, &oldFinished,
                        MC_PRIORITY, &oldPriority,
                        MC_QUEUE_POSITION, &oldQueuePosition,
                        MC_TRACKERS, &oldTrackers,
                        MC_SPEED_UP, &oldUpSpeed,
                        MC_RECHECK_PROGRESS, &oldRecheckProgress,
                        MC_SPEED_DOWN, &oldDownSpeed,
                        -1 );

    /* get the new states */
    st = tr_torrentStat( tor );
    newActive = is_torrent_active( st );
    newActivity = st->activity;
    newFinished = st->finished;
    newPriority = tr_torrentGetPriority( tor );
    newQueuePosition = st->queuePosition;
    newTrackers = build_torrent_trackers_hash( tor );
    newUpSpeed = st->pieceUploadSpeed_KBps;
    newDownSpeed = st->pieceDownloadSpeed_KBps;
    newRecheckProgress = st->recheckProgress;
    newActivePeerCount = st->peersSendingToUs + st->peersGettingFromUs + st->webseedsSendingToUs;
    newError = st->error;

    /* updating the model triggers off resort/refresh,
       so don't do it unless something's actually changed... */
    if( ( newActive != oldActive )
        || ( newActivity  != oldActivity )
        || ( newFinished != oldFinished )
        || ( newPriority != oldPriority )
        || ( newQueuePosition != oldQueuePosition )
        || ( newError != oldError )
        || ( newActivePeerCount != oldActivePeerCount )
        || ( newTrackers != oldTrackers )
        || gtr_compare_double( newUpSpeed, oldUpSpeed, 2 )
        || gtr_compare_double( newDownSpeed, oldDownSpeed, 2 )
        || gtr_compare_double( newRecheckProgress, oldRecheckProgress, 2 ) )
    {
        gtk_list_store_set( GTK_LIST_STORE( model ), iter,
                            MC_ACTIVE, newActive,
                            MC_ACTIVE_PEER_COUNT, newActivePeerCount,
                            MC_ERROR, newError,
                            MC_ACTIVITY, newActivity,
                            MC_FINISHED, newFinished,
                            MC_PRIORITY, newPriority,
                            MC_QUEUE_POSITION, newQueuePosition,
                            MC_TRACKERS, newTrackers,
                            MC_SPEED_UP, newUpSpeed,
                            MC_SPEED_DOWN, newDownSpeed,
                            MC_RECHECK_PROGRESS, newRecheckProgress,
                            -1 );
    }
}

void
gtr_core_update( TrCore * core )
{
    GtkTreeIter iter;
    GtkTreeModel * model;

    /* update the model */
    model = core_raw_model( core );
    if( gtk_tree_model_iter_nth_child( model, &iter, NULL, 0 ) ) do
        update_foreach( model, &iter );
    while( gtk_tree_model_iter_next( model, &iter ) );

    /* update hibernation */
    core_maybe_inhibit_hibernation( core );
}

/**
***  Hibernate
**/

#define SESSION_MANAGER_SERVICE_NAME  "org.gnome.SessionManager"
#define SESSION_MANAGER_INTERFACE     "org.gnome.SessionManager"
#define SESSION_MANAGER_OBJECT_PATH   "/org/gnome/SessionManager"

static gboolean
gtr_inhibit_hibernation( guint * cookie )
{
    gboolean success;
    GVariant * response;
    GDBusConnection * connection;
    GError * err = NULL;
    const char * application = "Transmission BitTorrent Client";
    const char * reason = "BitTorrent Activity";
    const int toplevel_xid = 0;
    const int flags = 4; /* Inhibit suspending the session or computer */

    connection = g_bus_get_sync( G_BUS_TYPE_SESSION, NULL, &err );

    response = g_dbus_connection_call_sync( connection,
                                            SESSION_MANAGER_SERVICE_NAME,
                                            SESSION_MANAGER_OBJECT_PATH,
                                            SESSION_MANAGER_INTERFACE,
                                            "Inhibit",
                                            g_variant_new( "(susu)", application, toplevel_xid, reason, flags ),
                                            NULL, G_DBUS_CALL_FLAGS_NONE,
                                            1000, NULL, &err );

    *cookie = g_variant_get_uint32( g_variant_get_child_value( response, 0 ) );

    success = err == NULL;

    /* logging */
    if( success )
        tr_inf( "%s", _( "Inhibiting desktop hibernation" ) );
    else {
        tr_err( _( "Couldn't inhibit desktop hibernation: %s" ), err->message );
        g_error_free( err );
    }

    /* cleanup */
    g_variant_unref( response );
    g_object_unref( connection );
    return success;
}

static void
gtr_uninhibit_hibernation( guint inhibit_cookie )
{
    GVariant * response;
    GDBusConnection * connection;
    GError * err = NULL;

    connection = g_bus_get_sync( G_BUS_TYPE_SESSION, NULL, &err );

    response = g_dbus_connection_call_sync( connection,
                                            SESSION_MANAGER_SERVICE_NAME,
                                            SESSION_MANAGER_OBJECT_PATH,
                                            SESSION_MANAGER_INTERFACE,
                                            "Uninhibit",
                                            g_variant_new( "(u)", inhibit_cookie ),
                                            NULL, G_DBUS_CALL_FLAGS_NONE,
                                            1000, NULL, &err );

    /* logging */
    if( err == NULL )
        tr_inf( "%s", _( "Allowing desktop hibernation" ) );
    else {
        g_warning( "Couldn't uninhibit desktop hibernation: %s.", err->message );
        g_error_free( err );
    }

    /* cleanup */
    g_variant_unref( response );
    g_object_unref( connection );
}

static void
gtr_core_set_hibernation_allowed( TrCore * core, gboolean allowed )
{
    g_return_if_fail( core );
    g_return_if_fail( core->priv );

    core->priv->inhibit_allowed = allowed != 0;

    if( allowed && core->priv->have_inhibit_cookie )
    {
        gtr_uninhibit_hibernation( core->priv->inhibit_cookie );
        core->priv->have_inhibit_cookie = FALSE;
    }

    if( !allowed
      && !core->priv->have_inhibit_cookie
      && !core->priv->dbus_error )
    {
        if( gtr_inhibit_hibernation( &core->priv->inhibit_cookie ) )
            core->priv->have_inhibit_cookie = TRUE;
        else
            core->priv->dbus_error = TRUE;
    }
}

static void
core_maybe_inhibit_hibernation( TrCore * core )
{
    /* hibernation is allowed if EITHER
     * (a) the "inhibit" pref is turned off OR
     * (b) there aren't any active torrents */
    const gboolean hibernation_allowed = !gtr_pref_flag_get( PREF_KEY_INHIBIT_HIBERNATION )
                                      || !gtr_core_get_active_torrent_count( core );
    gtr_core_set_hibernation_allowed( core, hibernation_allowed );
}

/**
***  Prefs
**/

static void
core_commit_prefs_change( TrCore * core, const char * key )
{
    gtr_core_pref_changed( core, key );
    gtr_pref_save( gtr_core_session( core ) );
}

void
gtr_core_set_pref( TrCore * self, const char * key, const char * newval )
{
    if( tr_strcmp0( newval, gtr_pref_string_get( key ) ) )
    {
        gtr_pref_string_set( key, newval );
        core_commit_prefs_change( self, key );
    }
}

void
gtr_core_set_pref_bool( TrCore * self, const char * key, gboolean newval )
{
    if( newval != gtr_pref_flag_get( key ) )
    {
        gtr_pref_flag_set( key, newval );
        core_commit_prefs_change( self, key );
    }
}

void
gtr_core_set_pref_int( TrCore * self, const char * key, int newval )
{
    if( newval != gtr_pref_int_get( key ) )
    {
        gtr_pref_int_set( key, newval );
        core_commit_prefs_change( self, key );
    }
}

void
gtr_core_set_pref_double( TrCore * self, const char * key, double newval )
{
    if( gtr_compare_double( newval, gtr_pref_double_get( key ), 4 ) )
    {
        gtr_pref_double_set( key, newval );
        core_commit_prefs_change( self, key );
    }
}

/***
****
****  RPC Interface
****
***/

/* #define DEBUG_RPC */

static int nextTag = 1;

typedef void ( server_response_func )( TrCore * core, tr_benc * response, gpointer user_data );

struct pending_request_data
{
    TrCore * core;
    server_response_func * response_func;
    gpointer response_func_user_data;
};

static GHashTable * pendingRequests = NULL;

static gboolean
core_read_rpc_response_idle( void * vresponse )
{
    tr_benc top;
    int64_t intVal;
    struct evbuffer * response = vresponse;

    tr_jsonParse( NULL, evbuffer_pullup( response, -1 ), evbuffer_get_length( response ), &top, NULL );

    if( tr_bencDictFindInt( &top, "tag", &intVal ) )
    {
        const int tag = (int)intVal;
        struct pending_request_data * data = g_hash_table_lookup( pendingRequests, &tag );
        if( data ) {
            if( data->response_func )
                (*data->response_func)(data->core, &top, data->response_func_user_data );
            g_hash_table_remove( pendingRequests, &tag );
        }
    }

    tr_bencFree( &top );
    evbuffer_free( response );
    return FALSE;
}

static void
core_read_rpc_response( tr_session       * session UNUSED,
                        struct evbuffer  * response,
                        void             * unused UNUSED )
{
    struct evbuffer * buf = evbuffer_new( );
    evbuffer_add_buffer( buf, response );
    gdk_threads_add_idle( core_read_rpc_response_idle, buf );
}

static void
core_send_rpc_request( TrCore * core, const char * json, int tag,
                       server_response_func * response_func,
                       void * response_func_user_data )
{
    tr_session * session = gtr_core_session( core );

    if( pendingRequests == NULL )
    {
        pendingRequests = g_hash_table_new_full( g_int_hash, g_int_equal, g_free, g_free );
    }

    if( session == NULL )
    {
        g_error( "GTK+ client doesn't support connections to remote servers yet." );
    }
    else
    {
        /* remember this request */
        struct pending_request_data * data;
        data = g_new0( struct pending_request_data, 1 );
        data->core = core;
        data->response_func = response_func;
        data->response_func_user_data = response_func_user_data;
        g_hash_table_insert( pendingRequests, g_memdup( &tag, sizeof( int ) ), data );

        /* make the request */
#ifdef DEBUG_RPC
        g_message( "request: [%s]", json );
#endif
        tr_rpc_request_exec_json( session, json, strlen( json ), core_read_rpc_response, GINT_TO_POINTER(tag) );
    }
}

/***
****  Sending a test-port request via RPC
***/

static void
on_port_test_response( TrCore * core, tr_benc * response, gpointer u UNUSED )
{
    tr_benc * args;
    bool is_open = FALSE;

    if( tr_bencDictFindDict( response, "arguments", &args ) )
        tr_bencDictFindBool( args, "port-is-open", &is_open );

    core_emit_port_tested( core, is_open );
}

void
gtr_core_port_test( TrCore * core )
{
    char buf[64];
    const int tag = nextTag++;
    g_snprintf( buf, sizeof( buf ), "{ \"method\": \"port-test\", \"tag\": %d }", tag );
    core_send_rpc_request( core, buf, tag, on_port_test_response, NULL );
}

/***
****  Updating a blocklist via RPC
***/

static void
on_blocklist_response( TrCore * core, tr_benc * response, gpointer data UNUSED )
{
    tr_benc * args;
    int64_t ruleCount = -1;

    if( tr_bencDictFindDict( response, "arguments", &args ) )
        tr_bencDictFindInt( args, "blocklist-size", &ruleCount );

    if( ruleCount > 0 )
        gtr_pref_int_set( "blocklist-date", tr_time( ) );

    core_emit_blocklist_udpated( core, ruleCount );
}

void
gtr_core_blocklist_update( TrCore * core )
{
    char buf[64];
    const int tag = nextTag++;
    g_snprintf( buf, sizeof( buf ), "{ \"method\": \"blocklist-update\", \"tag\": %d }", tag );
    core_send_rpc_request( core, buf, tag, on_blocklist_response, NULL );
}

/***
****
***/

void
gtr_core_exec_json( TrCore * core, const char * json )
{
    const int tag = nextTag++;
    core_send_rpc_request( core, json, tag, NULL, NULL );
}

void
gtr_core_exec( TrCore * core, const tr_benc * top )
{
    char * json = tr_bencToStr( top, TR_FMT_JSON_LEAN, NULL );
    gtr_core_exec_json( core, json );
    tr_free( json );
}

/***
****
***/

size_t
gtr_core_get_torrent_count( TrCore * core )
{
    return gtk_tree_model_iter_n_children( core_raw_model( core ), NULL );
}

size_t
gtr_core_get_active_torrent_count( TrCore * core )
{
    GtkTreeIter iter;
    size_t activeCount = 0;
    GtkTreeModel * model = core_raw_model( core );

    if( gtk_tree_model_iter_nth_child( model, &iter, NULL, 0 ) ) do
    {
        int activity;
        gtk_tree_model_get( model, &iter, MC_ACTIVITY, &activity, -1 );

        if( activity != TR_STATUS_STOPPED )
            ++activeCount;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    return activeCount;
}

tr_torrent *
gtr_core_find_torrent( TrCore * core, int id )
{
    tr_session * session;
    tr_torrent * tor = NULL;

    if(( session = gtr_core_session( core )))
        tor = tr_torrentFindFromId( session, id );

    return tor;
}

void
gtr_core_open_folder( TrCore * core, int torrent_id )
{
    const tr_torrent * tor = gtr_core_find_torrent( core, torrent_id );

    if( tor != NULL )
    {
        const gboolean single = tr_torrentInfo( tor )->fileCount == 1;
        const char * currentDir = tr_torrentGetCurrentDir( tor );
        if( single )
            gtr_open_file( currentDir );
        else {
            char * path = g_build_filename( currentDir, tr_torrentName( tor ), NULL );
            gtr_open_file( path );
            g_free( path );
        }
    }
}
