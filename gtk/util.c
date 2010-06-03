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

const char*
gtr_get_unicode_string( int i )
{
    switch( i ) {
        case GTR_UNICODE_UP:   return "\xE2\x86\x91";
        case GTR_UNICODE_DOWN: return "\xE2\x86\x93";
        case GTR_UNICODE_INF:  return "\xE2\x88\x9E";
        default:               return "err";
    }
}

char*
tr_strlratio( char * buf, double ratio, size_t buflen )
{
    return tr_strratio( buf, buflen, ratio, gtr_get_unicode_string( GTR_UNICODE_INF ) );
}

static double KiB = 1024.0;
static double MiB = ( 1024.0 * 1024.0 );
static double GiB = ( 1024.0 * 1024.0 * 1024.0 );

char*
tr_strlsize( char * buf, guint64 bytes, size_t buflen )
{
    if( !bytes )
        g_strlcpy( buf, _( "None" ), buflen );
    else if( bytes < KiB )
        g_snprintf( buf, buflen, ngettext( "%'u byte", "%'u bytes", (guint)bytes ), (guint)bytes );
    else if( bytes < MiB )
        g_snprintf( buf, buflen, _( "%'.1f KiB" ), bytes / KiB );
    else if( bytes < GiB )
        g_snprintf( buf, buflen, _( "%'.1f MiB" ), bytes / MiB );
    else
        g_snprintf( buf, buflen, _( "%'.1f GiB" ), bytes / GiB );
    return buf;
}

char*
tr_strlspeed( char * buf, double kb_sec, size_t buflen )
{
    const double speed = kb_sec;

    if( speed < 1000.0 )  /* 0.0 KiB to 999.9 KiB */
        g_snprintf( buf, buflen, _( "%'.1f KiB/s" ), speed );
    else if( speed < 102400.0 ) /* 0.98 MiB to 99.99 MiB */
        g_snprintf( buf, buflen, _( "%'.2f MiB/s" ), ( speed / KiB ) );
    else if( speed < 1024000.0 ) /* 100.0 MiB to 999.9 MiB */
        g_snprintf( buf, buflen, _( "%'.1f MiB/s" ), ( speed / MiB ) );
    else /* insane speeds */
        g_snprintf( buf, buflen, _( "%'.2f GiB/s" ), ( speed / GiB ) );

    return buf;
}

char*
tr_strltime( char * buf, int seconds, size_t buflen )
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

/* pattern-matching text; ie, legaltorrents.com */
char*
gtr_get_host_from_url( const char * url )
{
    char * h = NULL;
    char * name;
    const char * first_dot;
    const char * last_dot;

    tr_urlParse( url, -1, NULL, &h, NULL, NULL );
    first_dot = strchr( h, '.' );
    last_dot = strrchr( h, '.' );

    if( ( first_dot ) && ( last_dot ) && ( first_dot != last_dot ) )
        name = g_strdup( first_dot + 1 );
    else
        name = g_strdup( h );

    tr_free( h );
    return name;
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

static GtkWindow *
getWindow( GtkWidget * w )
{
    if( w == NULL )
        return NULL;

    if( GTK_IS_WINDOW( w ) )
        return GTK_WINDOW( w );

    return GTK_WINDOW( gtk_widget_get_ancestor( w, GTK_TYPE_WINDOW ) );
}

void
addTorrentErrorDialog( GtkWidget *  child,
                       int          err,
                       const char * filename )
{
    char * secondary;
    const char * fmt;
    GtkWidget * w;
    GtkWindow * win = getWindow( child );

    switch( err )
    {
        case TR_PARSE_ERR: fmt = _( "The torrent file \"%s\" contains invalid data." ); break;
        case TR_PARSE_DUPLICATE: fmt = _( "The torrent file \"%s\" is already in use." ); break;
        default: fmt = _( "The torrent file \"%s\" encountered an unknown error." ); break;
    }
    secondary = g_strdup_printf( fmt, filename );

    w = gtk_message_dialog_new( win,
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
gtr_object_ref_sink( gpointer object )
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
gtr_file_trash_or_remove( const char * filename )
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
                               G_TYPE_STRING, filename,
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
gtr_priority_combo_set_value( GtkWidget * w, tr_priority_t value )
{
    int i;
    int currentValue;
    const int column = 0;
    GtkTreeIter iter;
    GtkComboBox * combobox = GTK_COMBO_BOX( w );
    GtkTreeModel * model = gtk_combo_box_get_model( combobox );

    /* do the value and current value match? */
    if( gtk_combo_box_get_active_iter( combobox, &iter ) ) {
        gtk_tree_model_get( model, &iter, column, &currentValue, -1 );
        if( currentValue == value )
            return;
    }

    /* find the one to select */
    i = 0;
    while(( gtk_tree_model_iter_nth_child( model, &iter, NULL, i++ ))) {
        gtk_tree_model_get( model, &iter, column, &currentValue, -1 );
        if( currentValue == value ) {
            gtk_combo_box_set_active_iter( combobox, &iter );
            return;
        }
    }
}

tr_priority_t
gtr_priority_combo_get_value( GtkWidget * w )
{
    int value = 0;
    GtkTreeIter iter;
    GtkComboBox * combo_box = GTK_COMBO_BOX( w );

    if( gtk_combo_box_get_active_iter( combo_box, &iter ) )
        gtk_tree_model_get( gtk_combo_box_get_model( combo_box ), &iter, 0, &value, -1 );

    return value;
}

GtkWidget *
gtr_priority_combo_new( void )
{
    int i;
    GtkWidget * w;
    GtkCellRenderer * r;
    GtkListStore * store;
    const struct {
        int value;
        const char * text;
    } items[] = {
        { TR_PRI_HIGH,   N_( "High" )  },
        { TR_PRI_NORMAL, N_( "Normal" ) },
        { TR_PRI_LOW,    N_( "Low" )  }
    };

    store = gtk_list_store_new( 2, G_TYPE_INT, G_TYPE_STRING );
    for( i=0; i<(int)G_N_ELEMENTS(items); ++i ) {
        GtkTreeIter iter;
        gtk_list_store_append( store, &iter );
        gtk_list_store_set( store, &iter, 0, items[i].value,
                                          1, _( items[i].text ),
                                         -1 );
    }

    w = gtk_combo_box_new_with_model( GTK_TREE_MODEL( store ) );
    r = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( w ), r, TRUE );
    gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT( w ), r, "text", 1, NULL );

    /* cleanup */
    g_object_unref( store );
    return w;
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

static void
gtr_func_data_free( gpointer data )
{
#if GTK_CHECK_VERSION( 2,10,0 )
    g_slice_free( struct gtr_func_data, data );
#else
    g_free( data );
#endif
}

static struct gtr_func_data *
gtr_func_data_new( GSourceFunc function, gpointer data )
{
#if GTK_CHECK_VERSION( 2,10,0 )
    struct gtr_func_data * d = g_slice_new( struct gtr_func_data );
#else
    struct gtr_func_data * d = g_new( struct gtr_func_data, 1 );
#endif
    d->function = function;
    d->data = data;
    return d;
}

static gboolean
gtr_thread_func( gpointer data )
{
    gboolean more;
    struct gtr_func_data * idle_data = data;

    gdk_threads_enter( );
    more = idle_data->function( idle_data->data );
    gdk_threads_leave( );

    return more;
}
#endif

void
gtr_idle_add( GSourceFunc function, gpointer data )
{
#if GTK_CHECK_VERSION( 2,12,0 )
    gdk_threads_add_idle( function, data );
#else
    g_idle_add_full( G_PRIORITY_DEFAULT,
                     gtr_thread_func,
                     gtr_func_data_new( function, data ),
                     gtr_func_data_free );
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
    return g_timeout_add_full( G_PRIORITY_DEFAULT,
                               seconds * 1000,
                               gtr_thread_func,
                               gtr_func_data_new( function, data ),
                               gtr_func_data_free );
#endif
}

void
gtr_unrecognized_url_dialog( GtkWidget * parent, const char * url )
{
    const char * xt = "xt=urn:btih";

    GtkWindow * window = getWindow( parent );

    GString * gstr = g_string_new( NULL );

    GtkWidget * w = gtk_message_dialog_new( window, 0,
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_CLOSE,
                                            "%s", _( "Unrecognized URL" ) );

    g_string_append_printf( gstr, _( "Transmission doesn't know how to use \"%s\"" ), url );

    if( gtr_is_magnet_link( url ) && ( strstr( url, xt ) == NULL ) )
    {
        g_string_append_printf( gstr, "\n \n" );
        g_string_append_printf( gstr, _( "This magnet link appears to be intended for something other than BitTorrent.  BitTorrent magnet links have a section containing \"%s\"." ), xt );
    }

    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ), "%s", gstr->str );
    g_signal_connect_swapped( w, "response", G_CALLBACK( gtk_widget_destroy ), w );
    gtk_widget_show( w );
    g_string_free( gstr, TRUE );
}
