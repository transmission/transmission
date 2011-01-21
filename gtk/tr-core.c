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

#include <string.h> /* strcmp, strlen */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#ifdef HAVE_GIO
 #include <gio/gio.h>
#endif
#ifdef HAVE_DBUS_GLIB
 #include <dbus/dbus-glib.h>
#endif

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
#ifdef HAVE_DBUS_GLIB
 #include "tr-core-dbus.h"
#endif
#include "tr-prefs.h"
#include "tr-torrent.h"
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

static void maybeInhibitHibernation( TrCore * core );

static gboolean our_instance_adds_remote_torrents = FALSE;

struct TrCorePrivate
{
#ifdef HAVE_GIO
    GFileMonitor *  monitor;
    gulong          monitor_tag;
    char *          monitor_path;
    GSList *        monitor_files;
    guint           monitor_idle_tag;
#endif
    gboolean        adding_from_watch_dir;
    gboolean        inhibit_allowed;
    gboolean        have_inhibit_cookie;
    gboolean        dbus_error;
    guint           inhibit_cookie;
    gint            busy_count;
    GtkTreeModel *  model;
    tr_session *    session;
};

static int
isDisposed( const TrCore * core )
{
    return !core || !core->priv;
}

static void
tr_core_dispose( GObject * obj )
{
    TrCore * core = TR_CORE( obj );

    if( !isDisposed( core ) )
    {
        GObjectClass * parent;

        core->priv = NULL;

        parent = g_type_class_peek( g_type_parent( TR_CORE_TYPE ) );
        parent->dispose( obj );
    }
}

static void
tr_core_class_init( gpointer              g_class,
                    gpointer g_class_data UNUSED )
{
    GObjectClass * gobject_class;

    g_type_class_add_private( g_class, sizeof( struct TrCorePrivate ) );

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->dispose = tr_core_dispose;

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
        "busy",                             /* signal name */
        G_TYPE_FROM_CLASS( g_class ),       /* applies to TrCore */
        G_SIGNAL_RUN_FIRST,                 /* when to invoke */
        G_STRUCT_OFFSET(TrCoreClass, busy), /* class_offset */
        NULL, NULL,                         /* accumulator */
        g_cclosure_marshal_VOID__BOOLEAN    /* marshaler */,
        G_TYPE_NONE,                        /* return type */
        1, G_TYPE_BOOLEAN );                /* signal arguments */

    core_signals[BLOCKLIST_SIGNAL] = g_signal_new(
        "blocklist-updated",                          /* signal name */
        G_TYPE_FROM_CLASS( g_class ),                     /* applies to TrCore */
        G_SIGNAL_RUN_FIRST,                               /* when to invoke */
        G_STRUCT_OFFSET(TrCoreClass, blocklist_updated),  /* class_offset */
        NULL, NULL,                                       /* accumulator */
        g_cclosure_marshal_VOID__INT,                     /* marshaler */
        G_TYPE_NONE,                                      /* return type */
        1, G_TYPE_INT );                                  /* signal arguments */

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

#ifdef HAVE_DBUS_GLIB
    {
        DBusGConnection * bus = dbus_g_bus_get( DBUS_BUS_SESSION, NULL );
        DBusGProxy *      bus_proxy = NULL;
        if( bus )
            bus_proxy =
                dbus_g_proxy_new_for_name( bus, "org.freedesktop.DBus",
                                           "/org/freedesktop/DBus",
                                           "org.freedesktop.DBus" );
        if( bus_proxy )
        {
            int result = 0;
            dbus_g_proxy_call( bus_proxy, "RequestName", NULL,
                               G_TYPE_STRING,
                               "com.transmissionbt.Transmission",
                               G_TYPE_UINT, 0,
                               G_TYPE_INVALID,
                               G_TYPE_UINT, &result,
                               G_TYPE_INVALID );
            if( ( our_instance_adds_remote_torrents = result == 1 ) )
                dbus_g_object_type_install_info(
                    TR_CORE_TYPE,
                    &
                    dbus_glib_tr_core_object_info );
        }
    }
#endif
}

/***
****
***/

static tr_bool
coreIsBusy( TrCore * core )
{
    return core->priv->busy_count > 0;
}

static void
emitBusy( TrCore * core )
{
    g_signal_emit( core, core_signals[BUSY_SIGNAL], 0, coreIsBusy( core ) );
}

static void
coreAddToBusy( TrCore * core, int addMe )
{
    const tr_bool wasBusy = coreIsBusy( core );

    core->priv->busy_count += addMe;

    if( wasBusy != coreIsBusy( core ) )
        emitBusy( core );
}

static void coreIncBusy( TrCore * core ) { coreAddToBusy( core, 1 ); }
static void coreDecBusy( TrCore * core ) { coreAddToBusy( core, -1 ); }

/***
****  SORTING
***/

static gboolean
isValidETA( int t )
{
    return ( t != TR_ETA_NOT_AVAIL ) && ( t != TR_ETA_UNKNOWN );
}

static int
compareETA( int a, int b )
{
    const gboolean a_valid = isValidETA( a );
    const gboolean b_valid = isValidETA( b );

    if( !a_valid && !b_valid ) return 0;
    if( !a_valid ) return -1;
    if( !b_valid ) return 1;
    return a < b ? 1 : -1;
}

static int
compareDouble( double a, double b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compareUint64( uint64_t a, uint64_t b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compareInt_( int a, int b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compareRatio( double a, double b )
{
    if( (int)a == TR_RATIO_INF && (int)b == TR_RATIO_INF ) return 0;
    if( (int)a == TR_RATIO_INF ) return 1;
    if( (int)b == TR_RATIO_INF ) return -1;
    return compareDouble( a, b );
}

static int
compareTime( time_t a, time_t b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

static int
compareByName( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data UNUSED )
{
    int ret = 0;

    if( !ret ) {
        char *ca, *cb;
        gtk_tree_model_get( m, a, MC_NAME_COLLATED, &ca, -1 );
        gtk_tree_model_get( m, b, MC_NAME_COLLATED, &cb, -1 );
        ret = gtr_strcmp0( ca, cb );
        g_free( cb );
        g_free( ca );
    }

    if( !ret ) {
        tr_torrent * t;
        const tr_info *ia, *ib;
        gtk_tree_model_get( m, a, MC_TORRENT_RAW, &t, -1 );
        ia = tr_torrentInfo( t );
        gtk_tree_model_get( m, b, MC_TORRENT_RAW, &t, -1 );
        ib = tr_torrentInfo( t );
        ret = memcmp( ia->hash, ib->hash, SHA_DIGEST_LENGTH );
    }

    return ret;
}

static int
compareByRatio( GtkTreeModel* m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data )
{
    int ret = 0;
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;

    gtk_tree_model_get( m, a, MC_TORRENT_RAW, &ta, -1 );
    sa = tr_torrentStatCached( ta );
    gtk_tree_model_get( m, b, MC_TORRENT_RAW, &tb, -1 );
    sb = tr_torrentStatCached( tb );

    if( !ret ) ret = compareRatio( sa->ratio, sb->ratio );
    if( !ret ) ret = compareByName( m, a, b, user_data );
    return ret;
}

static int
compareByActivity( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data )
{
    int ret = 0;
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;
    double aUp, aDown, bUp, bDown;

    gtk_tree_model_get( m, a, MC_SPEED_UP, &aUp,
                              MC_SPEED_DOWN, &aDown,
                              MC_TORRENT_RAW, &ta,
                              -1 );
    gtk_tree_model_get( m, b, MC_SPEED_UP, &bUp,
                              MC_SPEED_DOWN, &bDown,
                              MC_TORRENT_RAW, &tb,
                              -1 );
    sa = tr_torrentStatCached( ta );
    sb = tr_torrentStatCached( tb );

    if( !ret ) ret = compareDouble( aUp+aDown, bUp+bDown );
    if( !ret ) ret = compareUint64( sa->uploadedEver, sb->uploadedEver );
    if( !ret ) ret = compareByName( m, a, b, user_data );
    return ret;
}

static int
compareByAge( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data )
{
    int ret = 0;
    tr_torrent *ta, *tb;

    gtk_tree_model_get( m, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( m, b, MC_TORRENT_RAW, &tb, -1 );

    if( !ret ) ret = compareTime( tr_torrentStatCached( ta )->addedDate, tr_torrentStatCached( tb )->addedDate );
    if( !ret ) ret = compareByName( m, a, b, user_data );
    return ret;
}

static int
compareBySize( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data )
{
    int ret = 0;
    tr_torrent *t;
    const tr_info *ia, *ib;

    gtk_tree_model_get( m, a, MC_TORRENT_RAW, &t, -1 );
    ia = tr_torrentInfo( t );
    gtk_tree_model_get( m, b, MC_TORRENT_RAW, &t, -1 );
    ib = tr_torrentInfo( t );

    if( !ret ) ret = compareUint64( ia->totalSize, ib->totalSize );
    if( !ret ) ret = compareByName( m, a, b, user_data );
    return ret;
}

static int
compareByProgress( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data )
{
    int ret = 0;
    tr_torrent * t;
    const tr_stat *sa, *sb;

    gtk_tree_model_get( m, a, MC_TORRENT_RAW, &t, -1 );
    sa = tr_torrentStatCached( t );
    gtk_tree_model_get( m, b, MC_TORRENT_RAW, &t, -1 );
    sb = tr_torrentStatCached( t );

    if( !ret ) ret = compareDouble( sa->percentComplete, sb->percentComplete );
    if( !ret ) ret = compareDouble( sa->seedRatioPercentDone, sb->seedRatioPercentDone );
    if( !ret ) ret = compareByRatio( m, a, b, user_data );
    return ret;
}

static int
compareByETA( GtkTreeModel * m, GtkTreeIter  * a, GtkTreeIter  * b, gpointer user_data )
{
    int ret = 0;
    tr_torrent *ta, *tb;

    gtk_tree_model_get( m, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( m, b, MC_TORRENT_RAW, &tb, -1 );

    if( !ret ) ret = compareETA( tr_torrentStatCached( ta )->eta, tr_torrentStatCached( tb )->eta );
    if( !ret ) ret = compareByName( m, a, b, user_data );
    return ret;
}

static int
compareByState( GtkTreeModel * m, GtkTreeIter * a, GtkTreeIter * b, gpointer user_data )
{
    int ret = 0;
    int sa, sb;

    gtk_tree_model_get( m, a, MC_ACTIVITY, &sa, -1 );
    gtk_tree_model_get( m, b, MC_ACTIVITY, &sb, -1 );

    if( !ret ) ret = compareInt_( sa, sb );
    if( !ret ) ret = compareByProgress( m, a, b, user_data );
    return ret;
}

static void
setSort( TrCore *     core,
         const char * mode,
         gboolean     isReversed  )
{
    const int              col = MC_TORRENT_RAW;
    GtkTreeIterCompareFunc sort_func;
    GtkSortType            type =
        isReversed ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
    GtkTreeSortable *      sortable =
        GTK_TREE_SORTABLE( tr_core_model( core ) );

    if( !strcmp( mode, "sort-by-activity" ) )
        sort_func = compareByActivity;
    else if( !strcmp( mode, "sort-by-age" ) )
        sort_func = compareByAge;
    else if( !strcmp( mode, "sort-by-progress" ) )
        sort_func = compareByProgress;
    else if( !strcmp( mode, "sort-by-time-left" ) )
        sort_func = compareByETA;
    else if( !strcmp( mode, "sort-by-ratio" ) )
        sort_func = compareByRatio;
    else if( !strcmp( mode, "sort-by-state" ) )
        sort_func = compareByState;
    else if( !strcmp( mode, "sort-by-size" ) )
        sort_func = compareBySize;
    else {
        sort_func = compareByName;
        type = isReversed ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    }

    gtk_tree_sortable_set_sort_func( sortable, col, sort_func, NULL, NULL );
    gtk_tree_sortable_set_sort_column_id( sortable, col, type );
}

static void
tr_core_apply_defaults( tr_ctor * ctor )
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
    {
        const char * path = gtr_pref_string_get( TR_PREFS_KEY_DOWNLOAD_DIR );
        tr_ctorSetDownloadDir( ctor, TR_FORCE, path );
    }
}

static char *
torrentTrackerString( tr_torrent * tor )
{
    int i;
    GString * str = g_string_new( NULL );
    const tr_info * inf = tr_torrentInfo( tor );

    for( i = 0; i < inf->trackerCount; ++i )
    {
        const tr_tracker_info * t = &inf->trackers[i];
        g_string_append( str, t->announce );
    }

    return g_string_free( str, FALSE );
}

static gboolean
isTorrentActive( const tr_stat * st )
{
    return ( st->peersSendingToUs > 0 )
        || ( st->peersGettingFromUs > 0 )
        || ( st->activity == TR_STATUS_CHECK );
}

#ifdef HAVE_GIO

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

static void
watchdir_file_update_mtime( struct watchdir_file * file )
{
    GFile * gfile = g_file_new_for_path( file->filename );
    GFileInfo * info = g_file_query_info( gfile, G_FILE_ATTRIBUTE_TIME_MODIFIED, 0, NULL, NULL );

    file->mtime = g_file_info_get_attribute_uint64( info, G_FILE_ATTRIBUTE_TIME_MODIFIED );

    g_object_unref( G_OBJECT( info ) );
    g_object_unref( G_OBJECT( gfile ) );
}

static struct watchdir_file*
watchdir_file_new( const char * filename )
{
    struct watchdir_file * f;

    f = g_new( struct watchdir_file, 1 );
    f->filename = g_strdup( filename );
    watchdir_file_update_mtime( f );

    return f;
}

static void
watchdir_file_free( struct watchdir_file * f )
{
    g_free( f->filename );
    g_free( f );
}

static gboolean
watchFolderIdle( gpointer gcore )
{
    GSList * l;
    GSList * addme = NULL;
    GSList * monitor_files = NULL;
    TrCore * core = TR_CORE( gcore );
    const time_t now = time( NULL );
    struct TrCorePrivate * p = core->priv;

    /* of the monitor_files, make a list of those that haven't
     * changed lately, since they should be ready to add */
    for( l=p->monitor_files; l!=NULL; l=l->next ) {
        struct watchdir_file * f = l->data;
        watchdir_file_update_mtime( f );
        if( f->mtime + 2 >= now )
            monitor_files = g_slist_prepend( monitor_files, f );
        else {
            addme = g_slist_prepend( addme, g_strdup( f->filename ) );
            watchdir_file_free( f );
        }
    }

    /* add the torrents from that list */
    core->priv->adding_from_watch_dir = TRUE;
    tr_core_add_list_defaults( core, addme, TRUE );
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
maybeAddTorrent( TrCore * core, const char * filename )
{
    const gboolean isTorrent = g_str_has_suffix( filename, ".torrent" );

    if( isTorrent )
    {
        struct TrCorePrivate * p = core->priv;

        if( !g_slist_find_custom( p->monitor_files, filename, (GCompareFunc)compare_watchdir_file_to_filename ) )
            p->monitor_files = g_slist_append( p->monitor_files, watchdir_file_new( filename ) );

        if( !p->monitor_idle_tag )
            p->monitor_idle_tag = gtr_timeout_add_seconds( 1, watchFolderIdle, core );
    }
}

static void
watchFolderChanged( GFileMonitor       * monitor    UNUSED,
                    GFile *                         file,
                    GFile              * other_type UNUSED,
                    GFileMonitorEvent               event_type,
                    gpointer                        core )
{
    if( event_type == G_FILE_MONITOR_EVENT_CREATED )
    {
        char * filename = g_file_get_path( file );
        maybeAddTorrent( core, filename );
        g_free( filename );
    }
}

static void
scanWatchDir( TrCore * core )
{
    const gboolean isEnabled = gtr_pref_flag_get( PREF_KEY_DIR_WATCH_ENABLED );

    if( isEnabled )
    {
        const char * dirname = gtr_pref_string_get( PREF_KEY_DIR_WATCH );
        GDir * dir = g_dir_open( dirname, 0, NULL );

        if( dir != NULL )
        {
            const char * basename;
            while(( basename = g_dir_read_name( dir )))
            {
                char * filename = g_build_filename( dirname, basename, NULL );
                maybeAddTorrent( core, filename );
                g_free( filename );
            }

            g_dir_close( dir );
        }
    }
}

static void
updateWatchDir( TrCore * core )
{
    const char *           filename = gtr_pref_string_get( PREF_KEY_DIR_WATCH );
    const gboolean         isEnabled = gtr_pref_flag_get(
        PREF_KEY_DIR_WATCH_ENABLED );
    struct TrCorePrivate * p = TR_CORE( core )->priv;

    if( p->monitor && ( !isEnabled || gtr_strcmp0( filename, p->monitor_path ) ) )
    {
        g_signal_handler_disconnect( p->monitor, p->monitor_tag );
        g_free( p->monitor_path );
        g_file_monitor_cancel( p->monitor );
        g_object_unref( G_OBJECT( p->monitor ) );
        p->monitor_path = NULL;
        p->monitor = NULL;
        p->monitor_tag = 0;
    }

    if( isEnabled && !p->monitor )
    {
        GFile *        file = g_file_new_for_path( filename );
        GFileMonitor * m = g_file_monitor_directory( file, 0, NULL, NULL );
        scanWatchDir( core );
        p->monitor = m;
        p->monitor_path = g_strdup( filename );
        p->monitor_tag = g_signal_connect( m, "changed",
                                           G_CALLBACK(
                                               watchFolderChanged ), core );
    }
}

#endif

static void
prefsChanged( TrCore *      core,
              const char *  key,
              gpointer data UNUSED )
{
    if( !strcmp( key, PREF_KEY_SORT_MODE )
      || !strcmp( key, PREF_KEY_SORT_REVERSED ) )
    {
        const char * mode = gtr_pref_string_get( PREF_KEY_SORT_MODE );
        gboolean     isReversed = gtr_pref_flag_get( PREF_KEY_SORT_REVERSED );
        setSort( core, mode, isReversed );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_LIMIT_GLOBAL ) )
    {
        const uint16_t val = gtr_pref_int_get( key );
        tr_sessionSetPeerLimit( tr_core_session( core ), val );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_LIMIT_TORRENT ) )
    {
        const uint16_t val = gtr_pref_int_get( key );
        tr_sessionSetPeerLimitPerTorrent( tr_core_session( core ), val );
    }
    else if( !strcmp( key, PREF_KEY_INHIBIT_HIBERNATION ) )
    {
        maybeInhibitHibernation( core );
    }
#ifdef HAVE_GIO
    else if( !strcmp( key, PREF_KEY_DIR_WATCH )
           || !strcmp( key, PREF_KEY_DIR_WATCH_ENABLED ) )
    {
        updateWatchDir( core );
    }
#endif
}

static void
tr_core_init( GTypeInstance *  instance,
              gpointer g_class UNUSED )
{
    GtkListStore * store;
    struct TrCorePrivate * p;
    TrCore * self = (TrCore *) instance;

    /* column types for the model used to store torrent information */
    /* keep this in sync with the enum near the bottom of tr_core.h */
    GType types[] = { G_TYPE_STRING,    /* name */
                      G_TYPE_STRING,    /* collated name */
                      TR_TORRENT_TYPE,  /* TrTorrent object */
                      G_TYPE_POINTER,   /* tr_torrent* */
                      G_TYPE_DOUBLE,    /* tr_stat.pieceUploadSpeed_KBps */
                      G_TYPE_DOUBLE,    /* tr_stat.pieceDownloadSpeed_KBps */
                      G_TYPE_BOOLEAN,   /* filter.c:ACTIVITY_FILTER_ACTIVE */
                      G_TYPE_INT,       /* tr_stat.activity */
                      G_TYPE_UCHAR,     /* tr_stat.finished */
                      G_TYPE_CHAR,      /* tr_priority_t */
                      G_TYPE_STRING };  /* concatenated trackers string */

    p = self->priv = G_TYPE_INSTANCE_GET_PRIVATE( self,
                                                  TR_CORE_TYPE,
                                                  struct TrCorePrivate );

    /* create the model used to store torrent data */
    g_assert( G_N_ELEMENTS( types ) == MC_ROW_COUNT );
    store = gtk_list_store_newv( MC_ROW_COUNT, types );

    p->model    = GTK_TREE_MODEL( store );

#ifdef HAVE_DBUS_GLIB
    if( our_instance_adds_remote_torrents )
    {
        DBusGConnection * bus = dbus_g_bus_get( DBUS_BUS_SESSION, NULL );
        if( bus )
            dbus_g_connection_register_g_object(
                 bus,
                "/com/transmissionbt/Transmission",
                G_OBJECT( self ) );
    }
#endif
}

GType
tr_core_get_type( void )
{
    static GType type = 0;

    if( !type )
    {
        static const GTypeInfo info =
        {
            sizeof( TrCoreClass ),
            NULL,                 /* base_init */
            NULL,                 /* base_finalize */
            tr_core_class_init,   /* class_init */
            NULL,                 /* class_finalize */
            NULL,                 /* class_data */
            sizeof( TrCore ),
            0,                    /* n_preallocs */
            tr_core_init,         /* instance_init */
            NULL,
        };
        type = g_type_register_static( G_TYPE_OBJECT, "TrCore", &info, 0 );
    }

    return type;
}

/**
***
**/

TrCore *
tr_core_new( tr_session * session )
{
    TrCore * core = TR_CORE( g_object_new( TR_CORE_TYPE, NULL ) );

    core->priv->session  = session;

    /* init from prefs & listen to pref changes */
    prefsChanged( core, PREF_KEY_SORT_MODE, NULL );
    prefsChanged( core, PREF_KEY_SORT_REVERSED, NULL );
    prefsChanged( core, PREF_KEY_DIR_WATCH_ENABLED, NULL );
    prefsChanged( core, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, NULL );
    prefsChanged( core, PREF_KEY_INHIBIT_HIBERNATION, NULL );
    g_signal_connect( core, "prefs-changed", G_CALLBACK( prefsChanged ), NULL );

    return core;
}

void
tr_core_close( TrCore * core )
{
    tr_session * session = tr_core_session( core );

    if( session )
    {
        core->priv->session = NULL;
        gtr_pref_save( session );
        tr_sessionClose( session );
    }
}

GtkTreeModel *
tr_core_model( TrCore * core )
{
    return isDisposed( core ) ? NULL : core->priv->model;
}

tr_session *
tr_core_session( TrCore * core )
{
    return isDisposed( core ) ? NULL : core->priv->session;
}

void
tr_core_add_torrent( TrCore     * self,
                     TrTorrent  * gtor,
                     gboolean     doNotify )
{
    const tr_info * inf = tr_torrent_info( gtor );
    const tr_stat * st = tr_torrent_stat( gtor );
    tr_torrent * tor = tr_torrent_handle( gtor );
    char *  collated = g_utf8_strdown( inf->name ? inf->name : "", -1 );
    char *  trackers = torrentTrackerString( tor );
    GtkListStore *  store = GTK_LIST_STORE( tr_core_model( self ) );
    GtkTreeIter  unused;

    gtk_list_store_insert_with_values( store, &unused, 0,
                                       MC_NAME,          inf->name,
                                       MC_NAME_COLLATED, collated,
                                       MC_TORRENT,       gtor,
                                       MC_TORRENT_RAW,   tor,
                                       MC_SPEED_UP,      st->pieceUploadSpeed_KBps,
                                       MC_SPEED_DOWN,    st->pieceDownloadSpeed_KBps,
                                       MC_ACTIVE,        isTorrentActive( st ),
                                       MC_ACTIVITY,      st->activity,
                                       MC_FINISHED,      st->finished,
                                       MC_PRIORITY,      tr_torrentGetPriority( tor ),
                                       MC_TRACKERS,      trackers,
                                       -1 );

    if( doNotify )
        gtr_notify_added( inf->name );

    /* cleanup */
    g_object_unref( G_OBJECT( gtor ) );
    g_free( collated );
    g_free( trackers );
}

void
tr_core_load( TrCore * self, gboolean forcePaused )
{
    int i;
    tr_ctor * ctor;
    tr_torrent ** torrents;
    int count = 0;

    ctor = tr_ctorNew( tr_core_session( self ) );
    if( forcePaused )
        tr_ctorSetPaused( ctor, TR_FORCE, TRUE );
    tr_ctorSetPeerLimit( ctor, TR_FALLBACK,
                         gtr_pref_int_get( TR_PREFS_KEY_PEER_LIMIT_TORRENT ) );

    torrents = tr_sessionLoadTorrents ( tr_core_session( self ), ctor, &count );
    for( i=0; i<count; ++i )
        tr_core_add_torrent( self, tr_torrent_new_preexisting( torrents[i] ), FALSE );

    tr_free( torrents );
    tr_ctorFree( ctor );
}

/***
****
***/

static void
emitBlocklistUpdated( TrCore * core, int ruleCount )
{
    g_signal_emit( core, core_signals[BLOCKLIST_SIGNAL], 0, ruleCount );
}

static void
emitPortTested( TrCore * core, gboolean isOpen )
{
    g_signal_emit( core, core_signals[PORT_SIGNAL], 0, isOpen );
}

static void
tr_core_errsig( TrCore * core, enum tr_core_err type, const char * msg )
{
    g_signal_emit( core, core_signals[ADD_ERROR_SIGNAL], 0, type, msg );
}

static int
add_ctor( TrCore * core, tr_ctor * ctor, gboolean doPrompt, gboolean doNotify )
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
                tr_core_errsig( core, err, inf.name );
            tr_metainfoFree( &inf );
            break;

        default:
            if( doPrompt )
                g_signal_emit( core, core_signals[ADD_PROMPT_SIGNAL], 0, ctor );
            else {
                tr_session * session = tr_core_session( core );
                TrTorrent * gtor = tr_torrent_new_ctor( session, ctor, &err );
                if( !err )
                    tr_core_add_torrent( core, gtor, doNotify );
            }
            tr_metainfoFree( &inf );
            break;
    }

    return err;
}

void
tr_core_add_ctor( TrCore * core, tr_ctor * ctor )
{
    const gboolean doPrompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );
    const gboolean doNotify = FALSE;
    tr_core_apply_defaults( ctor );
    add_ctor( core, ctor, doPrompt, doNotify );
}

/* invoked remotely via dbus. */
gboolean
tr_core_add_metainfo( TrCore      * core,
                      const char  * payload,
                      gboolean    * setme_handled,
                      GError     ** gerr UNUSED )
{
    tr_session * session = tr_core_session( core );

    if( !session )
    {
        *setme_handled = FALSE;
    }
    else if( gtr_is_supported_url( payload ) || gtr_is_magnet_link( payload ) )
    {
        tr_core_add_from_url( core, payload );
        *setme_handled = TRUE;
    }
    else /* base64-encoded metainfo */
    {
        int file_length;
        tr_ctor * ctor;
        char * file_contents;
        gboolean do_prompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );

        ctor = tr_ctorNew( session );
        tr_core_apply_defaults( ctor );

        file_contents = tr_base64_decode( payload, -1, &file_length );
        tr_ctorSetMetainfo( ctor, (const uint8_t*)file_contents, file_length );
        add_ctor( core, ctor, do_prompt, TRUE );

        tr_free( file_contents );
        tr_core_torrents_added( core );
        *setme_handled = TRUE;
    }

    return TRUE;
}

/***
****
***/

struct url_dialog_data
{
    TrCore * core;
    tr_ctor * ctor;
    char * url;
    long response_code;
};

static gboolean
onURLDoneIdle( gpointer vdata )
{
    struct url_dialog_data * data = vdata;

    if( data->response_code != 200 )
    {
        gtr_http_failure_dialog( NULL, data->url, data->response_code );
    }
    else
    {
        const gboolean doPrompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );
        const gboolean doNotify = FALSE;
        const int err = add_ctor( data->core, data->ctor, doPrompt, doNotify );

        if( err == TR_PARSE_ERR )
            tr_core_errsig( data->core, TR_PARSE_ERR, data->url );

        tr_core_torrents_added( data->core );
    }

    /* cleanup */
    coreDecBusy( data->core );
    g_free( data->url );
    g_free( data );
    return FALSE;
}

static void
onURLDone( tr_session   * session,
           long           response_code,
           const void   * response,
           size_t         response_byte_count,
           void         * vdata )
{
    struct url_dialog_data * data = vdata;

    data->response_code = response_code;
    data->ctor = tr_ctorNew( session );
    tr_core_apply_defaults( data->ctor );
    tr_ctorSetMetainfo( data->ctor, response, response_byte_count );

    gtr_idle_add( onURLDoneIdle, data );
}

void
tr_core_add_from_url( TrCore * core, const char * url )
{
    tr_session * session = tr_core_session( core );
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
            tr_core_add_ctor( core, ctor );
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
        coreIncBusy( data->core );
        tr_webRun( session, url, NULL, onURLDone, data );
    }
}

/***
****
***/

static void
add_filename( TrCore      * core,
              const char  * filename,
              gboolean      doStart,
              gboolean      doPrompt,
              gboolean      doNotify )
{
    tr_session * session = tr_core_session( core );

    if( session == NULL )
        return;

    if( gtr_is_supported_url( filename ) || gtr_is_magnet_link( filename ) )
    {
        tr_core_add_from_url( core, filename );
    }
    else if( g_file_test( filename, G_FILE_TEST_EXISTS ) )
    {
        int err;

        tr_ctor * ctor = tr_ctorNew( session );
        tr_ctorSetMetainfoFromFile( ctor, filename );
        tr_core_apply_defaults( ctor );
        tr_ctorSetPaused( ctor, TR_FORCE, !doStart );

        err = add_ctor( core, ctor, doPrompt, doNotify );
        if( err == TR_PARSE_ERR )
            tr_core_errsig( core, TR_PARSE_ERR, filename );
    }
    else if( gtr_is_hex_hashcode( filename ) )
    {
        tr_core_add_from_url( core, filename );
    }
}

gboolean
tr_core_present_window( TrCore      * core UNUSED,
                        gboolean *         success,
                        GError     ** err  UNUSED )
{
    /* Setting the toggle-main-window GtkCheckMenuItem to
       make sure its state is correctly set */
    gtr_action_set_toggled( "toggle-main-window", TRUE);

    *success = TRUE;
    return TRUE;
}

void
tr_core_add_list( TrCore       * core,
                  GSList       * torrentFiles,
                  gboolean       doStart,
                  gboolean       doPrompt,
                  gboolean       doNotify )
{
    GSList * l;

    for( l=torrentFiles; l!=NULL; l=l->next )
    {
        char * filename = l->data;
        add_filename( core, filename, doStart, doPrompt, doNotify );
        g_free( filename );
    }

    tr_core_torrents_added( core );

    g_slist_free( torrentFiles );
}

void
tr_core_add_list_defaults( TrCore * core, GSList * torrentFiles, gboolean doNotify )
{
    const gboolean doStart = gtr_pref_flag_get( TR_PREFS_KEY_START );
    const gboolean doPrompt = gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT );

    tr_core_add_list( core, torrentFiles, doStart, doPrompt, doNotify );
}

void
tr_core_torrents_added( TrCore * self )
{
    tr_core_update( self );
    tr_core_errsig( self, TR_CORE_ERR_NO_MORE_TORRENTS, NULL );
}

static gboolean
findTorrentInModel( TrCore *      core,
                    int           id,
                    GtkTreeIter * setme )
{
    int            match = 0;
    GtkTreeIter    iter;
    GtkTreeModel * model = tr_core_model( core );

    if( gtk_tree_model_iter_children( model, &iter, NULL ) ) do
        {
            tr_torrent * tor;
            gtk_tree_model_get( model, &iter, MC_TORRENT_RAW, &tor, -1 );
            match = tr_torrentId( tor ) == id;
        }
        while( !match && gtk_tree_model_iter_next( model, &iter ) );

    if( match )
        *setme = iter;

    return match;
}

void
tr_core_remove_torrent( TrCore * core, TrTorrent * gtor, gboolean deleteFiles )
{
    const tr_torrent * tor = tr_torrent_handle( gtor );

    if( tor != NULL )
        tr_core_remove_torrent_from_id( core, tr_torrentId( tor ), deleteFiles );
}

void
tr_core_remove_torrent_from_id( TrCore * core, int id, gboolean deleteFiles )
{
    GtkTreeIter iter;

    if( findTorrentInModel( core, id, &iter ) )
    {
        TrTorrent * gtor = NULL;
        tr_torrent * tor = NULL;
        GtkTreeModel * model = tr_core_model( core );

        gtk_tree_model_get( model, &iter, MC_TORRENT, &gtor,
                                          MC_TORRENT_RAW, &tor,
                                          -1 );

        /* remove from the gui */
        gtk_list_store_remove( GTK_LIST_STORE( model ), &iter );

        /* remove the torrent */
        tr_torrent_set_delete_local_data_flag( gtor, deleteFiles );
        tr_torrent_set_remove_flag( gtor, TRUE );
        gtr_warn_if_fail( G_OBJECT( gtor )->ref_count == 1 );
        g_object_unref( G_OBJECT( gtor ) ); /* remove the last refcount */
    }
}

/***
****
***/

static gboolean
update_foreach( GtkTreeModel * model,
                GtkTreePath  * path UNUSED,
                GtkTreeIter  * iter,
                gpointer       data UNUSED )
{
    int oldActivity, newActivity;
    tr_bool oldFinished, newFinished;
    tr_priority_t oldPriority, newPriority;
    char * oldCollatedName, * newCollatedName;
    char * oldTrackers, * newTrackers;
    double oldUpSpeed, newUpSpeed;
    double oldDownSpeed, newDownSpeed;
    gboolean oldActive, newActive;
    const tr_stat * st;
    const tr_info * inf;
    TrTorrent * gtor;
    tr_torrent * tor;

    /* get the old states */
    gtk_tree_model_get( model, iter,
                        MC_TORRENT, &gtor,
                        MC_NAME_COLLATED, &oldCollatedName,
                        MC_ACTIVE, &oldActive,
                        MC_ACTIVITY, &oldActivity,
                        MC_FINISHED, &oldFinished,
                        MC_PRIORITY, &oldPriority,
                        MC_TRACKERS, &oldTrackers,
                        MC_SPEED_UP, &oldUpSpeed,
                        MC_SPEED_DOWN, &oldDownSpeed,
                        -1 );

    /* get the new states */
    tor = tr_torrent_handle( gtor );
    st = tr_torrentStat( tor );
    newActive = isTorrentActive( st );
    newActivity = st->activity;
    newFinished = st->finished;
    newPriority = tr_torrentGetPriority( tor );
    newTrackers = torrentTrackerString( tor );
    newUpSpeed = st->pieceUploadSpeed_KBps;
    newDownSpeed = st->pieceDownloadSpeed_KBps;
    inf = tr_torrent_info( gtor );
    newCollatedName = g_utf8_strdown( inf->name ? inf->name : "", -1 );

    /* updating the model triggers off resort/refresh,
       so don't do it unless something's actually changed... */
    if( ( newActive != oldActive )
        || ( newActivity  != oldActivity )
        || ( newFinished != oldFinished )
        || ( newPriority != oldPriority )
        || gtr_strcmp0( oldTrackers, newTrackers )
        || gtr_strcmp0( oldCollatedName, newCollatedName )
        || gtr_compare_double( newUpSpeed, oldUpSpeed, 3 )
        || gtr_compare_double( newDownSpeed, oldDownSpeed, 3 ) )
    {
        gtk_list_store_set( GTK_LIST_STORE( model ), iter,
                            MC_ACTIVE, newActive,
                            MC_ACTIVITY, newActivity,
                            MC_NAME_COLLATED, &newCollatedName,
                            MC_FINISHED, newFinished,
                            MC_PRIORITY, newPriority,
                            MC_TRACKERS, newTrackers,
                            MC_SPEED_UP, newUpSpeed,
                            MC_SPEED_DOWN, newDownSpeed,
                            -1 );
    }

    /* cleanup */
    g_object_unref( gtor );
    g_free( newCollatedName );
    g_free( oldCollatedName );
    g_free( newTrackers );
    g_free( oldTrackers );
    return FALSE;
}

void
tr_core_update( TrCore * self )
{
    int               column;
    GtkSortType       order;
    GtkTreeSortable * sortable;
    GtkTreeModel *    model = tr_core_model( self );

    /* pause sorting */
    sortable = GTK_TREE_SORTABLE( model );
    gtk_tree_sortable_get_sort_column_id( sortable, &column, &order );
    gtk_tree_sortable_set_sort_column_id(
        sortable, GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order );

    /* refresh the model */
    gtk_tree_model_foreach( model, update_foreach, NULL );

    /* resume sorting */
    gtk_tree_sortable_set_sort_column_id( sortable, column, order );

    /* maybe inhibit hibernation */
    maybeInhibitHibernation( self );
}

/**
***  Hibernate
**/

#ifdef HAVE_DBUS_GLIB

static DBusGProxy*
get_hibernation_inhibit_proxy( void )
{
    DBusGConnection * conn;
    GError * error = NULL;
    const char * name = "org.gnome.SessionManager";
    const char * path = "/org/gnome/SessionManager";
    const char * interface = "org.gnome.SessionManager";

    conn = dbus_g_bus_get( DBUS_BUS_SESSION, &error );
    if( error )
    {
        g_warning ( "DBUS cannot connect : %s", error->message );
        g_error_free ( error );
        return NULL;
    }

    return dbus_g_proxy_new_for_name ( conn, name, path, interface );
}

static gboolean
gtr_inhibit_hibernation( guint * cookie )
{
    gboolean     success = FALSE;
    DBusGProxy * proxy = get_hibernation_inhibit_proxy( );

    if( proxy )
    {
        GError * error = NULL;
        const int toplevel_xid = 0;
        const char * application = _( "Transmission Bittorrent Client" );
        const char * reason = _( "BitTorrent Activity" );
        const int flags = 4; /* Inhibit suspending the session or computer */

        success = dbus_g_proxy_call( proxy, "Inhibit", &error,
                                     G_TYPE_STRING, application,
                                     G_TYPE_UINT, toplevel_xid,
                                     G_TYPE_STRING, reason,
                                     G_TYPE_UINT, flags,
                                     G_TYPE_INVALID, /* sentinel - end of input args */
                                     G_TYPE_UINT, cookie,
                                     G_TYPE_INVALID /* senitnel - end of output args */ );

        if( success )
            tr_inf( "%s", _( "Disallowing desktop hibernation" ) );
        else
        {
            tr_err( _( "Couldn't disable desktop hibernation: %s" ),
                    error->message );
            g_error_free( error );
        }

        g_object_unref( G_OBJECT( proxy ) );
    }

    return success != 0;
}

static void
gtr_uninhibit_hibernation( guint inhibit_cookie )
{
    DBusGProxy * proxy = get_hibernation_inhibit_proxy( );

    if( proxy )
    {
        GError * error = NULL;
        gboolean success = dbus_g_proxy_call( proxy, "Uninhibit", &error,
                                              G_TYPE_UINT, inhibit_cookie,
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID );
        if( success )
            tr_inf( "%s", _( "Allowing desktop hibernation" ) );
        else
        {
            g_warning( "Couldn't uninhibit the system from suspending: %s.",
                       error->message );
            g_error_free( error );
        }

        g_object_unref( G_OBJECT( proxy ) );
    }
}

#endif

static void
tr_core_set_hibernation_allowed( TrCore * core,
                                 gboolean allowed )
{
#ifdef HAVE_DBUS_GLIB
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
#endif
}

static void
maybeInhibitHibernation( TrCore * core )
{
    /* hibernation is allowed if EITHER
     * (a) the "inhibit" pref is turned off OR
     * (b) there aren't any active torrents */
    const gboolean hibernation_allowed = !gtr_pref_flag_get( PREF_KEY_INHIBIT_HIBERNATION )
                                      || !tr_core_get_active_torrent_count( core );
    tr_core_set_hibernation_allowed( core, hibernation_allowed );
}

/**
***  Prefs
**/

static void
commitPrefsChange( TrCore * core, const char * key )
{
    g_signal_emit( core, core_signals[PREFS_SIGNAL], 0, key );
    gtr_pref_save( tr_core_session( core ) );
}

void
tr_core_set_pref( TrCore * self, const char * key, const char * newval )
{
    const char * oldval = gtr_pref_string_get( key );

    if( gtr_strcmp0( oldval, newval ) )
    {
        gtr_pref_string_set( key, newval );
        commitPrefsChange( self, key );
    }
}

void
tr_core_set_pref_bool( TrCore *     self,
                       const char * key,
                       gboolean     newval )
{
    const gboolean oldval = gtr_pref_flag_get( key );

    if( oldval != newval )
    {
        gtr_pref_flag_set( key, newval );
        commitPrefsChange( self, key );
    }
}

void
tr_core_set_pref_int( TrCore *     self,
                      const char * key,
                      int          newval )
{
    const int oldval = gtr_pref_int_get( key );

    if( oldval != newval )
    {
        gtr_pref_int_set( key, newval );
        commitPrefsChange( self, key );
    }
}

void
tr_core_set_pref_double( TrCore *     self,
                         const char * key,
                         double       newval )
{
    const double oldval = gtr_pref_double_get( key );

    if( gtr_compare_double( oldval, newval, 4 ) )
    {
        gtr_pref_double_set( key, newval );
        commitPrefsChange( self, key );
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
    server_response_func * responseFunc;
    gpointer responseFuncUserData;
};

static GHashTable * pendingRequests = NULL;

static gboolean
readResponseIdle( void * vresponse )
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
            if( data->responseFunc )
                (*data->responseFunc)(data->core, &top, data->responseFuncUserData );
            g_hash_table_remove( pendingRequests, &tag );
        }
    }

    tr_bencFree( &top );
    evbuffer_free( response );
    return FALSE;
}

static void
readResponse( tr_session  * session UNUSED,
              struct evbuffer * response,
              void        * unused UNUSED )
{
    struct evbuffer * buf = evbuffer_new( );
    evbuffer_add_buffer( buf, response );
    gtr_idle_add( readResponseIdle, buf );
}

static void
sendRequest( TrCore * core, const char * json, int tag,
             server_response_func * responseFunc, void * responseFuncUserData )
{
    tr_session * session = tr_core_session( core );

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
        data->responseFunc = responseFunc;
        data->responseFuncUserData = responseFuncUserData;
        g_hash_table_insert( pendingRequests, g_memdup( &tag, sizeof( int ) ), data );

        /* make the request */
#ifdef DEBUG_RPC
        g_message( "request: [%s]", json );
#endif
        tr_rpc_request_exec_json( session, json, strlen( json ), readResponse, GINT_TO_POINTER(tag) );
    }
}

/***
****  Sending a test-port request via RPC
***/

static void
portTestResponseFunc( TrCore * core, tr_benc * response, gpointer userData UNUSED )
{
    tr_benc * args;
    tr_bool isOpen = FALSE;

    if( tr_bencDictFindDict( response, "arguments", &args ) )
        tr_bencDictFindBool( args, "port-is-open", &isOpen );

    emitPortTested( core, isOpen );
}

void
tr_core_port_test( TrCore * core )
{
    char buf[128];
    const int tag = nextTag++;
    g_snprintf( buf, sizeof( buf ), "{ \"method\": \"port-test\", \"tag\": %d }", tag );
    sendRequest( core, buf, tag, portTestResponseFunc, NULL );
}

/***
****  Updating a blocklist via RPC
***/

static void
blocklistResponseFunc( TrCore * core, tr_benc * response, gpointer userData UNUSED )
{
    tr_benc * args;
    int64_t ruleCount = -1;

    if( tr_bencDictFindDict( response, "arguments", &args ) )
        tr_bencDictFindInt( args, "blocklist-size", &ruleCount );

    if( ruleCount > 0 )
        gtr_pref_int_set( "blocklist-date", time( NULL ) );

    emitBlocklistUpdated( core, ruleCount );
}

void
tr_core_blocklist_update( TrCore * core )
{
    char buf[128];
    const int tag = nextTag++;
    g_snprintf( buf, sizeof( buf ), "{ \"method\": \"blocklist-update\", \"tag\": %d }", tag );
    sendRequest( core, buf, tag, blocklistResponseFunc, NULL );
}

/***
****
***/

void
tr_core_exec_json( TrCore * core, const char * json )
{
    const int tag = nextTag++;
    sendRequest( core, json, tag, NULL, NULL );
}

void
tr_core_exec( TrCore * core, const tr_benc * top )
{
    char * json = tr_bencToStr( top, TR_FMT_JSON_LEAN, NULL );
    tr_core_exec_json( core, json );
    tr_free( json );
}

/***
****
***/

void
tr_core_torrent_changed( TrCore * core, int id )
{
    GtkTreeIter iter;
    GtkTreeModel * model = tr_core_model( core );

    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
    {
        tr_torrent * tor;
        gtk_tree_model_get( model, &iter, MC_TORRENT_RAW, &tor, -1 );
        if( tr_torrentId( tor ) == id )
        {
            GtkTreePath * path = gtk_tree_model_get_path( model, &iter );
            gtk_tree_model_row_changed( model, path, &iter );
            gtk_tree_path_free( path );
            break;
        }
    }
    while( gtk_tree_model_iter_next( model, &iter ) );
}

size_t
tr_core_get_torrent_count( TrCore * core )
{
    return gtk_tree_model_iter_n_children( tr_core_model( core ), NULL );
}

size_t
tr_core_get_active_torrent_count( TrCore * core )
{
    GtkTreeIter iter;
    GtkTreeModel * model = tr_core_model( core );
    size_t activeCount = 0;

    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
    {
        int activity;
        gtk_tree_model_get( model, &iter, MC_ACTIVITY, &activity, -1 );

        if( activity != TR_STATUS_STOPPED )
            ++activeCount;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    return activeCount;
}

