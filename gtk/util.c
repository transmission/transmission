/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <ctype.h> /* isxdigit() */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h> /* free() */
#include <string.h> /* strcmp() */

#include <sys/types.h> /* for gtr_lockfile()'s open() */
#include <sys/stat.h> /* for gtr_lockfile()'s open() */
#include <fcntl.h> /* for gtr_lockfile()'s open() */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h> /* g_unlink() */
#ifdef HAVE_GIO
 #include <gio/gio.h> /* g_file_trash() */
#endif
#ifdef HAVE_DBUS_GLIB
 #include <dbus/dbus-glib.h>
#endif

#include <evhttp.h>

#include <libtransmission/transmission.h> /* TR_RATIO_NA, TR_RATIO_INF */
#include <libtransmission/utils.h> /* tr_inf */
#include <libtransmission/version.h> /* tr_inf */

#include "conf.h"
#include "hig.h"
#include "tr-prefs.h"
#include "util.h"

gtr_lockfile_state_t
gtr_lockfile( const char * filename )
{
    gtr_lockfile_state_t ret;

#ifdef WIN32

    HANDLE file = CreateFile( filename,
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL,
                              OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL );
    if( file == INVALID_HANDLE_VALUE )
        ret = GTR_LOCKFILE_EOPEN;
    else if( !LockFile( file, 0, 0, 1, 1 ) )
        ret = GTR_LOCKFILE_ELOCK;
    else
        ret = GTR_LOCKFILE_SUCCESS;

#else

    int fd = open( filename, O_RDWR | O_CREAT, 0666 );
    if( fd < 0 )
        ret = GTR_LOCKFILE_EOPEN;
    else {
        struct flock lk;
        memset( &lk, 0,  sizeof( lk ) );
        lk.l_start = 0;
        lk.l_len = 0;
        lk.l_type = F_WRLCK;
        lk.l_whence = SEEK_SET;
        if( -1 == fcntl( fd, F_SETLK, &lk ) )
            ret = GTR_LOCKFILE_ELOCK;
        else
            ret = GTR_LOCKFILE_SUCCESS;
    }

#endif

    return ret;
}

/***
****
***/


char*
tr_strlratio( char * buf, double ratio, size_t buflen )
{
    return tr_strratio( buf, buflen, ratio, "\xE2\x88\x9E" );
}

#define KILOBYTE_FACTOR 1024.0
#define MEGABYTE_FACTOR ( 1024.0 * 1024.0 )
#define GIGABYTE_FACTOR ( 1024.0 * 1024.0 * 1024.0 )

char*
tr_strlsize( char *  buf,
             guint64 size,
             size_t  buflen )
{
    if( !size )
        g_strlcpy( buf, _( "None" ), buflen );
#if GLIB_CHECK_VERSION( 2, 16, 0 )
    else
    {
        char * tmp = g_format_size_for_display( size );
        g_strlcpy( buf, tmp, buflen );
        g_free( tmp );
    }
#else
    else if( size < (guint64)KILOBYTE_FACTOR )
        g_snprintf( buf, buflen,
                    ngettext( "%'u byte", "%'u bytes",
                              (guint)size ), (guint)size );
    else
    {
        gdouble displayed_size;
        if( size < (guint64)MEGABYTE_FACTOR )
        {
            displayed_size = (gdouble) size / KILOBYTE_FACTOR;
            g_snprintf( buf, buflen, _( "%'.1f KB" ), displayed_size );
        }
        else if( size < (guint64)GIGABYTE_FACTOR )
        {
            displayed_size = (gdouble) size / MEGABYTE_FACTOR;
            g_snprintf( buf, buflen, _( "%'.1f MB" ), displayed_size );
        }
        else
        {
            displayed_size = (gdouble) size / GIGABYTE_FACTOR;
            g_snprintf( buf, buflen, _( "%'.1f GB" ), displayed_size );
        }
    }
#endif
    return buf;
}

char*
tr_strlspeed( char * buf,
              double kb_sec,
              size_t buflen )
{
    const double speed = kb_sec;

    if( speed < 1000.0 )  /* 0.0 KB to 999.9 KB */
        g_snprintf( buf, buflen, _( "%'.1f KB/s" ), speed );
    else if( speed < 102400.0 ) /* 0.98 MB to 99.99 MB */
        g_snprintf( buf, buflen, _( "%'.2f MB/s" ), ( speed / KILOBYTE_FACTOR ) );
    else if( speed < 1024000.0 ) /* 100.0 MB to 999.9 MB */
        g_snprintf( buf, buflen, _( "%'.1f MB/s" ), ( speed / MEGABYTE_FACTOR ) );
    else /* insane speeds */
        g_snprintf( buf, buflen, _( "%'.2f GB/s" ), ( speed / GIGABYTE_FACTOR ) );

    return buf;
}

char*
tr_strltime( char * buf,
             int    seconds,
             size_t buflen )
{
    int  days, hours, minutes;
    char d[128], h[128], m[128], s[128];

    if( seconds < 0 )
        seconds = 0;

    days = seconds / 86400;
    hours = ( seconds % 86400 ) / 3600;
    minutes = ( seconds % 3600 ) / 60;
    seconds = ( seconds % 3600 ) % 60;

    g_snprintf( d, sizeof( d ), ngettext( "%'d day", "%'d days",
                                          days ), days );
    g_snprintf( h, sizeof( h ), ngettext( "%'d hour", "%'d hours",
                                          hours ), hours );
    g_snprintf( m, sizeof( m ),
                ngettext( "%'d minute", "%'d minutes", minutes ), minutes );
    g_snprintf( s, sizeof( s ),
                ngettext( "%'d second", "%'d seconds", seconds ), seconds );

    if( days )
    {
        if( days >= 4 || !hours )
        {
            g_strlcpy( buf, d, buflen );
        }
        else
        {
            g_snprintf( buf, buflen, "%s, %s", d, h );
        }
    }
    else if( hours )
    {
        if( hours >= 4 || !minutes )
        {
            g_strlcpy( buf, h, buflen );
        }
        else
        {
            g_snprintf( buf, buflen, "%s, %s", h, m );
        }
    }
    else if( minutes )
    {
        if( minutes >= 4 || !seconds )
        {
            g_strlcpy( buf, m, buflen );
        }
        else
        {
            g_snprintf( buf, buflen, "%s, %s", m, s );
        }
    }
    else
    {
        g_strlcpy( buf, s, buflen );
    }

    return buf;
}

char *
gtr_localtime( time_t time )
{
    const struct tm tm = *localtime( &time );
    char            buf[256], *eoln;

    g_strlcpy( buf, asctime( &tm ), sizeof( buf ) );
    if( ( eoln = strchr( buf, '\n' ) ) )
        *eoln = '\0';

    return g_locale_to_utf8( buf, -1, NULL, NULL, NULL );
}

char *
gtr_localtime2( char * buf, time_t time, size_t buflen )
{
    char * tmp = gtr_localtime( time );
    g_strlcpy( buf, tmp, buflen );
    g_free( tmp );
    return buf;
}

int
gtr_mkdir_with_parents( const char * path, int mode )
{
#if GLIB_CHECK_VERSION( 2, 8, 0 )
    return !g_mkdir_with_parents( path, mode );
#else
    return !tr_mkdirp( path, mode );
#endif
}

GSList *
dupstrlist( GSList * l )
{
    GSList * ret = NULL;

    for( ; l != NULL; l = l->next )
        ret = g_slist_prepend( ret, g_strdup( l->data ) );
    return g_slist_reverse( ret );
}

char *
joinstrlist( GSList *list,
             char *  sep )
{
    GSList * l;
    GString *gstr = g_string_new ( NULL );

    for( l = list; l != NULL; l = l->next )
    {
        g_string_append ( gstr, (char*)l->data );
        if( l->next != NULL )
            g_string_append ( gstr, ( sep ) );
    }
    return g_string_free ( gstr, FALSE );
}

void
freestrlist( GSList *list )
{
    g_slist_foreach ( list, (GFunc)g_free, NULL );
    g_slist_free ( list );
}

char *
decode_uri( const char * uri )
{
    gboolean in_query = FALSE;
    char *   ret = g_new( char, strlen( uri ) + 1 );
    char *   out = ret;

    for( ; uri && *uri; )
    {
        char ch = *uri;
        if( ch == '?' )
            in_query = TRUE;
        else if( ch == '+' && in_query )
            ch = ' ';
        else if( ch == '%' && isxdigit( (unsigned char)uri[1] )
               && isxdigit( (unsigned char)uri[2] ) )
        {
            char buf[3] = { uri[1], uri[2], '\0' };
            ch = (char) g_ascii_strtoull( buf, NULL, 16 );
            uri += 2;
        }

        ++uri;
        *out++ = ch;
    }

    *out = '\0';
    return ret;
}


gboolean
gtr_is_supported_url( const char * str )
{
    return !strncmp( str, "ftp://", 6 )
        || !strncmp( str, "http://", 7 )
        || !strncmp( str, "https://", 8 );
}

gboolean
gtr_is_magnet_link( const char * str )
{
    return !strncmp( str, "magnet:?", 8 );
}

gboolean
gtr_is_hex_hashcode( const char * str )
{
    int i;

    if( !str || ( strlen( str ) != 40 ) )
        return FALSE;

    for( i=0; i<40; ++i )
        if( !isxdigit( str[i] ) )
            return FALSE;

    return TRUE;
}

void
addTorrentErrorDialog( GtkWidget *  child,
                       int          err,
                       const char * filename )
{
    GtkWidget *  w;
    GtkWidget *  win;
    const char * fmt;
    char *       secondary;

    switch( err )
    {
        case TR_PARSE_ERR: fmt = _( "The torrent file \"%s\" contains invalid data." ); break;
        case TR_PARSE_DUPLICATE: fmt = _( "The torrent file \"%s\" is already in use." ); break;
        default: fmt = _( "The torrent file \"%s\" encountered an unknown error." ); break;
    }
    secondary = g_strdup_printf( fmt, filename );
    win = ( !child || GTK_IS_WINDOW( child ) )
          ? child
          : gtk_widget_get_ancestor( child ? GTK_WIDGET(
                                         child ) : NULL, GTK_TYPE_WINDOW );
    w = gtk_message_dialog_new( GTK_WINDOW( win ),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_ERROR,
                               GTK_BUTTONS_CLOSE,
                               "%s", _( "Error opening torrent" ) );
    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ),
                                              "%s", secondary );
    g_signal_connect_swapped( w, "response",
                              G_CALLBACK( gtk_widget_destroy ), w );
    gtk_widget_show_all( w );
    g_free( secondary );
}
typedef void ( PopupFunc )( GtkWidget*, GdkEventButton* );

/* pop up the context menu if a user right-clicks.
   if the row they right-click on isn't selected, select it. */

gboolean
on_tree_view_button_pressed( GtkWidget *      view,
                             GdkEventButton * event,
                             gpointer         func )
{
    GtkTreeView * tv = GTK_TREE_VIEW( view );

    if( event->type == GDK_BUTTON_PRESS  &&  event->button == 3 )
    {
        GtkTreeSelection * selection = gtk_tree_view_get_selection( tv );
        GtkTreePath *      path;
        if( gtk_tree_view_get_path_at_pos ( tv,
                                            (gint) event->x,
                                            (gint) event->y,
                                            &path, NULL, NULL, NULL ) )
        {
            if( !gtk_tree_selection_path_is_selected ( selection, path ) )
            {
                gtk_tree_selection_unselect_all ( selection );
                gtk_tree_selection_select_path ( selection, path );
            }
            gtk_tree_path_free( path );
        }

        if( func != NULL )
            ( (PopupFunc*)func )( view, event );

        return TRUE;
    }

    return FALSE;
}

/* if the user clicked in an empty area of the list,
 * clear all the selections. */
gboolean
on_tree_view_button_released( GtkWidget *      view,
                              GdkEventButton * event,
                              gpointer         unused UNUSED )
{
    GtkTreeView * tv = GTK_TREE_VIEW( view );

    if( !gtk_tree_view_get_path_at_pos ( tv,
                                         (gint) event->x,
                                         (gint) event->y,
                                         NULL, NULL, NULL, NULL ) )
    {
        GtkTreeSelection * selection = gtk_tree_view_get_selection( tv );
        gtk_tree_selection_unselect_all ( selection );
    }

    return FALSE;
}

gpointer
tr_object_ref_sink( gpointer object )
{
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( object );
#else
    g_object_ref( object );
    gtk_object_sink( GTK_OBJECT( object ) );
#endif
    return object;
}

int
tr_file_trash_or_remove( const char * filename )
{
    if( filename && *filename )
    {
        gboolean trashed = FALSE;
#ifdef HAVE_GIO
        GError * err = NULL;
        GFile *  file = g_file_new_for_path( filename );
        trashed = g_file_trash( file, NULL, &err );
        if( err )
            g_message( "Unable to trash file \"%s\": %s", filename, err->message );
        g_clear_error( &err );
        g_object_unref( G_OBJECT( file ) );
#endif

        if( !trashed && g_remove( filename ) )
        {
            const int err = errno;
            g_message( "Unable to remove file \"%s\": %s", filename, g_strerror( err ) );
        }
    }

    return 0;
}

char*
gtr_get_help_url( void )
{
    const char * fmt = "http://www.transmissionbt.com/help/gtk/%d.%dx";
    int          major, minor;

    sscanf( SHORT_VERSION_STRING, "%d.%d", &major, &minor );
    return g_strdup_printf( fmt, major, minor / 10 );
}

void
gtr_open_file( const char * path )
{
    if( path )
    {
        gboolean opened = FALSE;
#ifdef HAVE_GIO
        if( !opened )
        {
            GFile * file = g_file_new_for_path( path );
            char *  uri = g_file_get_uri( file );
            opened = g_app_info_launch_default_for_uri( uri, NULL, NULL );
            g_free( uri );
            g_object_unref( G_OBJECT( file ) );
        }
#endif
        if( !opened )
        {
            char * argv[] = { (char*)"xdg-open", (char*)path, NULL };
            opened = g_spawn_async( NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                                    NULL, NULL, NULL, NULL );
        }

        if( !opened )
        {
            g_message( "Unable to open \"%s\"", path );
        }
    }
}

#define VALUE_SERVICE_NAME        "com.transmissionbt.Transmission"
#define VALUE_SERVICE_OBJECT_PATH "/com/transmissionbt/Transmission"
#define VALUE_SERVICE_INTERFACE   "com.transmissionbt.Transmission"

gboolean
gtr_dbus_add_torrent( const char * filename )
{
    /* FIXME: why is this static? */
    static gboolean handled = FALSE;

#ifdef HAVE_DBUS_GLIB
    char * payload;
    gsize file_length;
    char * file_contents = NULL;

    /* If it's a file, load its contents and send them over the wire...
     * it might be a temporary file that's going to disappear. */
    if( g_file_get_contents( filename, &file_contents, &file_length, NULL ) )
        payload = tr_base64_encode( file_contents, file_length, NULL );
    else if( gtr_is_supported_url( filename ) || gtr_is_magnet_link( filename ) )
        payload = tr_strdup( filename );
    else
        payload = NULL;

    if( payload != NULL )
    {
        GError * err = NULL;
        DBusGConnection * conn;
        DBusGProxy * proxy = NULL;

        if(( conn = dbus_g_bus_get( DBUS_BUS_SESSION, &err )))
            proxy = dbus_g_proxy_new_for_name (conn, VALUE_SERVICE_NAME,
                                                     VALUE_SERVICE_OBJECT_PATH,
                                                     VALUE_SERVICE_INTERFACE );
        else if( err )
           g_message( "err: %s", err->message );

        if( proxy )
            dbus_g_proxy_call( proxy, "AddMetainfo", &err,
                               G_TYPE_STRING, payload,
                               G_TYPE_INVALID,
                               G_TYPE_BOOLEAN, &handled,
                               G_TYPE_INVALID );
        if( err )
           g_message( "err: %s", err->message );

        if( proxy )
            g_object_unref( proxy );
        if( conn )
            dbus_g_connection_unref( conn );

        tr_free( payload );
    }

    g_free( file_contents );

#endif
    return handled;
}

gboolean
gtr_dbus_present_window( void )
{
    static gboolean   success = FALSE;

#ifdef HAVE_DBUS_GLIB
    DBusGProxy *      proxy = NULL;
    GError *          err = NULL;
    DBusGConnection * conn;
    if( ( conn = dbus_g_bus_get( DBUS_BUS_SESSION, &err ) ) )
        proxy = dbus_g_proxy_new_for_name ( conn, VALUE_SERVICE_NAME,
                                            VALUE_SERVICE_OBJECT_PATH,
                                            VALUE_SERVICE_INTERFACE );
    else if( err )
        g_message( "err: %s", err->message );
    if( proxy )
        dbus_g_proxy_call( proxy, "PresentWindow", &err,
                           G_TYPE_INVALID,
                           G_TYPE_BOOLEAN, &success,
                           G_TYPE_INVALID );
    if( err )
        g_message( "err: %s", err->message );

    g_object_unref( proxy );
    dbus_g_connection_unref( conn );
#endif
    return success;
}

GtkWidget *
gtr_button_new_from_stock( const char * stock,
                           const char * mnemonic )
{
    GtkWidget * image = gtk_image_new_from_stock( stock,
                                                  GTK_ICON_SIZE_BUTTON );
    GtkWidget * button = gtk_button_new_with_mnemonic( mnemonic );

    gtk_button_set_image( GTK_BUTTON( button ), image );
    return button;
}

/***
****
***/

void
gtr_widget_set_tooltip_text( GtkWidget * w, const char * tip )
{
#if GTK_CHECK_VERSION( 2,12,0 )
    gtk_widget_set_tooltip_text( w, tip );
#else
    static GtkTooltips * tips = NULL;
    if( tips == NULL )
        tips = gtk_tooltips_new( );
    gtk_tooltips_set_tip( tips, w, tip, NULL );
#endif
}

void
gtr_toolbar_set_orientation( GtkToolbar      * toolbar,
                             GtkOrientation    orientation )
{
#if GTK_CHECK_VERSION( 2,16,0 )
    gtk_orientable_set_orientation( GTK_ORIENTABLE( toolbar ), orientation );
#else
    gtk_toolbar_set_orientation( toolbar, orientation );
#endif
}

/***
****
***/

#if !GTK_CHECK_VERSION( 2,12,0 )
struct gtr_func_data
{
    GSourceFunc function;
    gpointer data;
};

static gboolean
gtr_thread_func( gpointer data )
{
    struct gtr_func_data * idle_data = data;
    gboolean more;

    gdk_threads_enter( );
    more = idle_data->function( idle_data->data );
    gdk_threads_leave( );

    if( !more )
        g_free( data );

    return more;
}
#endif

void
gtr_idle_add( GSourceFunc function, gpointer data )
{
#if GTK_CHECK_VERSION( 2,12,0 )
    gdk_threads_add_idle( function, data );
#else
    struct gtr_func_data * d = g_new( struct gtr_func_data, 1 );
    d->function = function;
    d->data = data;
    g_idle_add( gtr_thread_func, d );
#endif
}

guint
gtr_timeout_add_seconds( guint seconds, GSourceFunc function, gpointer data )
{
#if GTK_CHECK_VERSION( 2,14,0 )
    return gdk_threads_add_timeout_seconds( seconds, function, data );
#elif GTK_CHECK_VERSION( 2,12,0 )
    return gdk_threads_add_timeout( seconds*1000, function, data );
#else
    struct gtr_func_data * d = g_new( struct gtr_func_data, 1 );
    d->function = function;
    d->data = data;
    return g_timeout_add( seconds*1000, gtr_thread_func, d );
#endif
}
