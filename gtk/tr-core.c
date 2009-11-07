/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2008 Transmission authors and contributors
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

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/json.h>
#include <libtransmission/utils.h> /* tr_free */

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

static void     maybeInhibitHibernation( TrCore * core );

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
    TrCoreClass *  cc;

    g_type_class_add_private( g_class, sizeof( struct TrCorePrivate ) );

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->dispose = tr_core_dispose;

    cc = TR_CORE_CLASS( g_class );

    cc->blocklistSignal = g_signal_new( "blocklist-updated",          /* name */
                                        G_TYPE_FROM_CLASS( g_class ), /* applies to TrCore */
                                        G_SIGNAL_RUN_FIRST,           /* when to invoke */
                                        0, NULL, NULL,                /* accumulator */
                                        g_cclosure_marshal_VOID__INT, /* marshaler */
                                        G_TYPE_NONE,                  /* return type */
                                        1, G_TYPE_INT );              /* signal arguments */

    cc->portSignal = g_signal_new( "port-tested",
                                   G_TYPE_FROM_CLASS( g_class ),
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL,
                                   g_cclosure_marshal_VOID__BOOLEAN,
                                   G_TYPE_NONE,
                                   1, G_TYPE_BOOLEAN );

    cc->errsig = g_signal_new( "error",
                               G_TYPE_FROM_CLASS( g_class ),
                               G_SIGNAL_RUN_LAST,
                               0, NULL, NULL,
                               g_cclosure_marshal_VOID__UINT_POINTER,
                               G_TYPE_NONE,
                               2, G_TYPE_UINT, G_TYPE_POINTER );

    cc->promptsig = g_signal_new( "add-torrent-prompt",
                                  G_TYPE_FROM_CLASS( g_class ),
                                  G_SIGNAL_RUN_LAST,
                                  0, NULL, NULL,
                                  g_cclosure_marshal_VOID__POINTER,
                                  G_TYPE_NONE,
                                  1, G_TYPE_POINTER );

    cc->quitsig = g_signal_new( "quit",
                                G_TYPE_FROM_CLASS( g_class ),
                                G_SIGNAL_RUN_LAST,
                                0, NULL, NULL,
                                g_cclosure_marshal_VOID__VOID,
                                G_TYPE_NONE,
                                0 );

    cc->prefsig = g_signal_new( "prefs-changed",
                                G_TYPE_FROM_CLASS( g_class ),
                                G_SIGNAL_RUN_LAST,
                                0, NULL, NULL,
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
compareByRatio( GtkTreeModel  * model,
                GtkTreeIter   * a,
                GtkTreeIter   * b,
                gpointer        user_data UNUSED )
{
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;

    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &tb, -1 );

    sa = tr_torrentStatCached( ta );
    sb = tr_torrentStatCached( tb );

    return compareRatio( sa->ratio, sb->ratio );
}

static int
compareByActivity( GtkTreeModel * model,
                   GtkTreeIter  * a,
                   GtkTreeIter  * b,
                   gpointer       user_data UNUSED )
{
    int i;
    tr_torrent *ta, *tb;
    const tr_stat *sa, *sb;
    double aUp, aDown, bUp, bDown;

    gtk_tree_model_get( model, a, MC_SPEED_UP, &aUp,
                                  MC_SPEED_DOWN, &aDown,
                                  MC_TORRENT_RAW, &ta,
                                  -1 );
    gtk_tree_model_get( model, b, MC_SPEED_UP, &bUp,
                                  MC_SPEED_DOWN, &bDown,
                                  MC_TORRENT_RAW, &tb,
                                  -1 );

    if(( i = compareDouble( aUp+aDown, bUp+bDown )))
        return i;

    sa = tr_torrentStatCached( ta );
    sb = tr_torrentStatCached( tb );
    if( sa->uploadedEver != sb->uploadedEver )
        return sa->uploadedEver < sa->uploadedEver ? -1 : 1;

    return 0;
}

static int
compareByName( GtkTreeModel *             model,
               GtkTreeIter *              a,
               GtkTreeIter *              b,
               gpointer         user_data UNUSED )
{
    int   ret;
    char *ca, *cb;

    gtk_tree_model_get( model, a, MC_NAME_COLLATED, &ca, -1 );
    gtk_tree_model_get( model, b, MC_NAME_COLLATED, &cb, -1 );
    ret = strcmp( ca, cb );
    g_free( cb );
    g_free( ca );
    return ret;
}

static int
compareByAge( GtkTreeModel * model,
              GtkTreeIter  * a,
              GtkTreeIter  * b,
              gpointer       user_data UNUSED )
{
    tr_torrent *ta, *tb;

    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &tb, -1 );
    return compareTime( tr_torrentStatCached( ta )->addedDate,
                        tr_torrentStatCached( tb )->addedDate );
}

static int
compareBySize( GtkTreeModel * model,
               GtkTreeIter  * a,
               GtkTreeIter  * b,
               gpointer       user_data UNUSED )
{
    tr_torrent *t;
    const tr_info *ia, *ib;

    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &t, -1 );
    ia = tr_torrentInfo( t );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &t, -1 );
    ib = tr_torrentInfo( t );

    if( ia->totalSize < ib->totalSize ) return 1;
    if( ia->totalSize > ib->totalSize ) return -1;
    return 0;
}

static int
compareByProgress( GtkTreeModel *             model,
                   GtkTreeIter *              a,
                   GtkTreeIter *              b,
                   gpointer         user_data UNUSED )
{
    int ret;
    tr_torrent * t;
    const tr_stat *sa, *sb;

    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &t, -1 );
    sa = tr_torrentStatCached( t );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &t, -1 );
    sb = tr_torrentStatCached( t );
    ret = compareDouble( sa->percentDone, sb->percentDone );
    if( !ret )
        ret = compareRatio( sa->ratio, sb->ratio );
    return ret;
}

static int
compareByETA( GtkTreeModel * model,
              GtkTreeIter  * a,
              GtkTreeIter  * b,
              gpointer       user_data UNUSED )
{
    tr_torrent *ta, *tb;

    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &tb, -1 );

    return compareETA( tr_torrentStatCached( ta )->eta,
                       tr_torrentStatCached( tb )->eta );
}

static int
compareByState( GtkTreeModel * model,
                GtkTreeIter *  a,
                GtkTreeIter *  b,
                gpointer       user_data )
{
    int sa, sb, ret;

    /* first by state */
    gtk_tree_model_get( model, a, MC_ACTIVITY, &sa, -1 );
    gtk_tree_model_get( model, b, MC_ACTIVITY, &sb, -1 );
    ret = sa - sb;

    /* second by progress */
    if( !ret )
        ret = compareByProgress( model, a, b, user_data );

    return ret;
}

static int
compareByTracker( GtkTreeModel * model,
                  GtkTreeIter  * a,
                  GtkTreeIter  * b,
                  gpointer       user_data UNUSED )
{
    const tr_torrent *ta, *tb;

    gtk_tree_model_get( model, a, MC_TORRENT_RAW, &ta, -1 );
    gtk_tree_model_get( model, b, MC_TORRENT_RAW, &tb, -1 );
    return strcmp( tr_torrentInfo( ta )->trackers[0].announce,
                   tr_torrentInfo( tb )->trackers[0].announce );
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
    else if( !strcmp( mode, "sort-by-tracker" ) )
        sort_func = compareByTracker;
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
        tr_ctorSetPaused( ctor, TR_FORCE, !pref_flag_get( PREF_KEY_START ) );

    if( tr_ctorGetDeleteSource( ctor, NULL ) )
        tr_ctorSetDeleteSource( ctor,
                               pref_flag_get( PREF_KEY_TRASH_ORIGINAL ) );

    if( tr_ctorGetPeerLimit( ctor, TR_FORCE, NULL ) )
        tr_ctorSetPeerLimit( ctor, TR_FORCE,
                             pref_int_get( TR_PREFS_KEY_PEER_LIMIT_TORRENT ) );

    if( tr_ctorGetDownloadDir( ctor, TR_FORCE, NULL ) )
    {
        const char * path = pref_string_get( TR_PREFS_KEY_DOWNLOAD_DIR );
        tr_ctorSetDownloadDir( ctor, TR_FORCE, path );
    }
}

static int
tr_strcmp( const void * a,
           const void * b )
{
    if( a && b ) return strcmp( a, b );
    if( a ) return 1;
    if( b ) return -1;
    return 0;
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
    const gboolean isEnabled = pref_flag_get( PREF_KEY_DIR_WATCH_ENABLED );

    if( isEnabled )
    {
        const char * basename;
        const char * dirname = pref_string_get( PREF_KEY_DIR_WATCH );
        GDir * dir = g_dir_open( dirname, 0, NULL );

        while(( basename = g_dir_read_name( dir )))
        {
            char * filename = g_build_filename( dirname, basename, NULL );
            maybeAddTorrent( core, filename );
            g_free( filename );
        }

        g_dir_close( dir );
    }
}

static void
updateWatchDir( TrCore * core )
{
    const char *           filename = pref_string_get( PREF_KEY_DIR_WATCH );
    const gboolean         isEnabled = pref_flag_get(
        PREF_KEY_DIR_WATCH_ENABLED );
    struct TrCorePrivate * p = TR_CORE( core )->priv;

    if( p->monitor && ( !isEnabled || tr_strcmp( filename, p->monitor_path ) ) )
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
        const char * mode = pref_string_get( PREF_KEY_SORT_MODE );
        gboolean     isReversed = pref_flag_get( PREF_KEY_SORT_REVERSED );
        setSort( core, mode, isReversed );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_LIMIT_GLOBAL ) )
    {
        const uint16_t val = pref_int_get( key );
        tr_sessionSetPeerLimit( tr_core_session( core ), val );
    }
    else if( !strcmp( key, TR_PREFS_KEY_PEER_LIMIT_TORRENT ) )
    {
        const uint16_t val = pref_int_get( key );
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
                      G_TYPE_DOUBLE,    /* tr_stat.pieceUploadSpeed */
                      G_TYPE_DOUBLE,    /* tr_stat.pieceDownloadSpeed */
                      G_TYPE_INT };     /* tr_stat.status */

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
        pref_save( session );
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

static char*
doCollate( const char * in )
{
    const char * end = in + strlen( in );
    char *       casefold;
    char *       ret;

    while( in < end )
    {
        const gunichar ch = g_utf8_get_char( in );
        if( !g_unichar_isalnum ( ch ) ) /* eat everything before the first alnum
                                          */
            in += g_unichar_to_utf8( ch, NULL );
        else
            break;
    }

    if( in == end )
        return g_strdup ( "" );

    casefold = g_utf8_casefold( in, end - in );
    ret = g_utf8_collate_key( casefold, -1 );
    g_free( casefold );

    return ret;
}

void
tr_core_add_torrent( TrCore     * self,
                     TrTorrent  * gtor,
                     gboolean     doNotify )
{
    const tr_info * inf = tr_torrent_info( gtor );
    const tr_stat * st = tr_torrent_stat( gtor );
    tr_torrent *    tor = tr_torrent_handle( gtor );
    char *          collated = doCollate( inf->name );
    GtkListStore *  store = GTK_LIST_STORE( tr_core_model( self ) );
    GtkTreeIter     unused;

    gtk_list_store_insert_with_values( store, &unused, 0,
                                       MC_NAME,          inf->name,
                                       MC_NAME_COLLATED, collated,
                                       MC_TORRENT,       gtor,
                                       MC_TORRENT_RAW,   tor,
                                       MC_SPEED_UP,      st->pieceUploadSpeed,
                                       MC_SPEED_DOWN,    st->pieceDownloadSpeed,
                                       MC_ACTIVITY,      st->activity,
                                       -1 );

    if( doNotify )
        tr_notify_added( inf->name );

    /* cleanup */
    g_object_unref( G_OBJECT( gtor ) );
    g_free( collated );
}

int
tr_core_load( TrCore * self,
              gboolean forcePaused )
{
    int           i;
    int           count = 0;
    tr_torrent ** torrents;
    tr_ctor *     ctor;

    ctor = tr_ctorNew( tr_core_session( self ) );
    if( forcePaused )
        tr_ctorSetPaused( ctor, TR_FORCE, TRUE );
    tr_ctorSetPeerLimit( ctor, TR_FALLBACK,
                         pref_int_get( TR_PREFS_KEY_PEER_LIMIT_TORRENT ) );

    torrents = tr_sessionLoadTorrents ( tr_core_session( self ), ctor, &count );
    for( i = 0; i < count; ++i )
        tr_core_add_torrent( self, tr_torrent_new_preexisting( torrents[i] ), FALSE );

    tr_free( torrents );
    tr_ctorFree( ctor );

    return count;
}

static void
emitBlocklistUpdated( TrCore * core, int ruleCount )
{
    g_signal_emit( core, TR_CORE_GET_CLASS( core )->blocklistSignal, 0, ruleCount );
}

static void
emitPortTested( TrCore * core, gboolean isOpen )
{
    g_signal_emit( core, TR_CORE_GET_CLASS( core )->portSignal, 0, isOpen );
}

static void
tr_core_errsig( TrCore *         core,
                enum tr_core_err type,
                const char *     msg )
{
    g_signal_emit( core, TR_CORE_GET_CLASS( core )->errsig, 0, type, msg );
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
                g_signal_emit( core, TR_CORE_GET_CLASS( core )->promptsig, 0, ctor );
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
    const gboolean doStart = pref_flag_get( PREF_KEY_START );
    const gboolean doPrompt = pref_flag_get( PREF_KEY_OPTIONS_PROMPT );
    tr_core_apply_defaults( ctor );
    add_ctor( core, ctor, doStart, doPrompt );
}

/* invoked remotely via dbus. */
gboolean
tr_core_add_metainfo( TrCore      * core,
                      const char  * base64_metainfo,
                      gboolean    * setme_success,
                      GError     ** gerr UNUSED )
{
    tr_session * session = tr_core_session( core );

    if( !session )
    {
        *setme_success = FALSE;
    }
    else
    {
        int err;
        int file_length;
        tr_ctor * ctor;
        char * file_contents;
        gboolean do_prompt = pref_flag_get( PREF_KEY_OPTIONS_PROMPT );

        ctor = tr_ctorNew( session );
        tr_core_apply_defaults( ctor );

        file_contents = tr_base64_decode( base64_metainfo, -1, &file_length );
        err = tr_ctorSetMetainfo( ctor, (const uint8_t*)file_contents, file_length );

        if( !err )
            err = add_ctor( core, ctor, do_prompt, TRUE );

        tr_free( file_contents );
        tr_core_torrents_added( core );
        *setme_success = TRUE;
    }

    return TRUE;
}

static void
add_filename( TrCore      * core,
              const char  * filename,
              gboolean      doStart,
              gboolean      doPrompt,
              gboolean      doNotify )
{
    tr_session * session = tr_core_session( core );
    if( filename && session )
    {
        int err;
        tr_ctor * ctor = tr_ctorNew( session );
        tr_core_apply_defaults( ctor );
        tr_ctorSetPaused( ctor, TR_FORCE, !doStart );
        tr_ctorSetMetainfoFromFile( ctor, filename );

        err = add_ctor( core, ctor, doPrompt, doNotify );
        if( err == TR_PARSE_ERR )
            tr_core_errsig( core, TR_PARSE_ERR, filename );
    }
}

gboolean
tr_core_present_window( TrCore      * core UNUSED,
                        gboolean *         success,
                        GError     ** err  UNUSED )
{
    action_activate( "present-main-window" );
    *success = TRUE;
    return TRUE;
}

void
tr_core_add_list( TrCore       * core,
                  GSList       * torrentFiles,
                  pref_flag_t    start,
                  pref_flag_t    prompt,
                  gboolean       doNotify )
{
    const gboolean doStart = pref_flag_eval( start, PREF_KEY_START );
    const gboolean doPrompt = pref_flag_eval( prompt, PREF_KEY_OPTIONS_PROMPT );
    GSList * l;

    for( l = torrentFiles; l != NULL; l = l->next )
        add_filename( core, l->data, doStart, doPrompt, doNotify );

    tr_core_torrents_added( core );
    freestrlist( torrentFiles );
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
tr_core_torrent_destroyed( TrCore * core,
                           int      id )
{
    GtkTreeIter iter;

    if( findTorrentInModel( core, id, &iter ) )
    {
        TrTorrent * gtor;
        GtkTreeModel * model = tr_core_model( core );
        gtk_tree_model_get( model, &iter, MC_TORRENT, &gtor, -1 );
        tr_torrent_clear( gtor );
        gtk_list_store_remove( GTK_LIST_STORE( model ), &iter );
        g_object_unref( G_OBJECT( gtor ) );
    }
}

void
tr_core_remove_torrent( TrCore *    core,
                        TrTorrent * gtor,
                        int         deleteFiles )
{
    const tr_torrent * tor = tr_torrent_handle( gtor );

    if( tor )
    {
        int         id = tr_torrentId( tor );
        GtkTreeIter iter;
        if( findTorrentInModel( core, id, &iter ) )
        {
            GtkTreeModel * model = tr_core_model( core );

            /* remove from the gui */
            gtk_list_store_remove( GTK_LIST_STORE( model ), &iter );

            /* maybe delete the downloaded files */
            if( deleteFiles )
                tr_torrent_delete_files( gtor );

            /* remove the torrent */
            tr_torrent_set_remove_flag( gtor, TRUE );
            g_object_unref( G_OBJECT( gtor ) );
        }
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
    double oldUpSpeed, newUpSpeed;
    double oldDownSpeed, newDownSpeed;
    const tr_stat * st;
    TrTorrent * gtor;

    /* get the old states */
    gtk_tree_model_get( model, iter,
                        MC_TORRENT, &gtor,
                        MC_ACTIVITY, &oldActivity,
                        MC_SPEED_UP, &oldUpSpeed,
                        MC_SPEED_DOWN, &oldDownSpeed,
                        -1 );

    /* get the new states */
    st = tr_torrentStat( tr_torrent_handle( gtor ) );
    newActivity = st->activity;
    newUpSpeed = st->pieceUploadSpeed;
    newDownSpeed = st->pieceDownloadSpeed;

    /* updating the model triggers off resort/refresh,
       so don't do it unless something's actually changed... */
    if( ( newActivity != oldActivity ) ||
        ( (int)(newUpSpeed*10.0) != (int)(oldUpSpeed*10.0) ) ||
        ( (int)(newDownSpeed*10.0) != (int)(oldDownSpeed*10.0) ) )
    {
        gtk_list_store_set( GTK_LIST_STORE( model ), iter,
                            MC_ACTIVITY, newActivity,
                            MC_SPEED_UP, newUpSpeed,
                            MC_SPEED_DOWN, newDownSpeed,
                            -1 );
    }

    /* cleanup */
    g_object_unref( gtor );
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

void
tr_core_quit( TrCore * core )
{
    g_signal_emit( core, TR_CORE_GET_CLASS( core )->quitsig, 0 );
}

/**
***  Hibernate
**/

#ifdef HAVE_DBUS_GLIB

static DBusGProxy*
get_hibernation_inhibit_proxy( void )
{
    GError *          error = NULL;
    DBusGConnection * conn;

    conn = dbus_g_bus_get( DBUS_BUS_SESSION, &error );
    if( error )
    {
        g_warning ( "DBUS cannot connect : %s", error->message );
        g_error_free ( error );
        return NULL;
    }

    return dbus_g_proxy_new_for_name (
               conn,
               "org.freedesktop.PowerManagement",
               "/org/freedesktop/PowerManagement/Inhibit",
               "org.freedesktop.PowerManagement.Inhibit" );
}

static gboolean
gtr_inhibit_hibernation( guint * cookie )
{
    gboolean     success = FALSE;
    DBusGProxy * proxy = get_hibernation_inhibit_proxy( );

    if( proxy )
    {
        GError *     error = NULL;
        const char * application = _( "Transmission Bittorrent Client" );
        const char * reason = _( "BitTorrent Activity" );
        success = dbus_g_proxy_call( proxy, "Inhibit", &error,
                                     G_TYPE_STRING, application,
                                     G_TYPE_STRING, reason,
                                     G_TYPE_INVALID,
                                     G_TYPE_UINT, cookie,
                                     G_TYPE_INVALID );
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
        gboolean success = dbus_g_proxy_call( proxy, "UnInhibit", &error,
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
    gboolean inhibit = pref_flag_get( PREF_KEY_INHIBIT_HIBERNATION );

    /* always allow hibernation when all the torrents are paused */
    if( inhibit ) {
        tr_session * session = tr_core_session( core );

        if( tr_sessionGetActiveTorrentCount( session ) == 0 )
            inhibit = FALSE;
    }

    tr_core_set_hibernation_allowed( core, !inhibit );
}

/**
***  Prefs
**/

static void
commitPrefsChange( TrCore *     core,
                   const char * key )
{
    g_signal_emit( core, TR_CORE_GET_CLASS( core )->prefsig, 0, key );
    pref_save( tr_core_session( core ) );
}

void
tr_core_set_pref( TrCore *     self,
                  const char * key,
                  const char * newval )
{
    const char * oldval = pref_string_get( key );

    if( tr_strcmp( oldval, newval ) )
    {
        pref_string_set( key, newval );
        commitPrefsChange( self, key );
    }
}

void
tr_core_set_pref_bool( TrCore *     self,
                       const char * key,
                       gboolean     newval )
{
    const gboolean oldval = pref_flag_get( key );

    if( oldval != newval )
    {
        pref_flag_set( key, newval );
        commitPrefsChange( self, key );
    }
}

void
tr_core_set_pref_int( TrCore *     self,
                      const char * key,
                      int          newval )
{
    const int oldval = pref_int_get( key );

    if( oldval != newval )
    {
        pref_int_set( key, newval );
        commitPrefsChange( self, key );
    }
}

void
tr_core_set_pref_double( TrCore *     self,
                         const char * key,
                         double       newval )
{
    const double oldval = pref_double_get( key );

    if( oldval != newval )
    {
        pref_double_set( key, newval );
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
    GByteArray * response;
    tr_benc top;
    int64_t intVal;
    int tag;
    struct pending_request_data * data;

    response = vresponse;
    tr_jsonParse( NULL, response->data, response->len, &top, NULL );
    tr_bencDictFindInt( &top, "tag", &intVal );
    tag = (int)intVal;

    data = g_hash_table_lookup( pendingRequests, &tag );
    if( data && data->responseFunc )
        (*data->responseFunc)(data->core, &top, data->responseFuncUserData );

    tr_bencFree( &top );
    g_hash_table_remove( pendingRequests, &tag );
    g_byte_array_free( response, TRUE );
    return FALSE;
}

static void
readResponse( tr_session  * session UNUSED,
              const char  * response,
              size_t        response_len,
              void        * unused UNUSED )
{
    GByteArray * bytes = g_byte_array_new( );
#ifdef DEBUG_RPC
    g_message( "response: [%*.*s]", (int)response_len, (int)response_len, response );
#endif
    g_byte_array_append( bytes, (const uint8_t*)response, response_len );
    gtr_idle_add( readResponseIdle, bytes );
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
    int64_t ruleCount = 0;

    if( tr_bencDictFindDict( response, "arguments", &args ) )
        tr_bencDictFindInt( args, "blocklist-size", &ruleCount );

    if( ruleCount > 0 )
        pref_int_set( "blocklist-date", time( NULL ) );

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
