/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <ctype.h> /* isxdigit() */
#include <errno.h>
#include <math.h> /* pow() */
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

#include <libtransmission/transmission.h> /* TR_RATIO_NA, TR_RATIO_INF */
#include <libtransmission/utils.h> /* tr_inf */
#include <libtransmission/web.h> /* tr_webResponseStr() */
#include <libtransmission/version.h> /* tr_inf */

#include "conf.h"
#include "hig.h"
#include "tr-prefs.h"
#include "util.h"

/***
****  UNITS
***/

const int mem_K = 1024;
const char * mem_K_str = N_("KiB");
const char * mem_M_str = N_("MiB");
const char * mem_G_str = N_("GiB");
const char * mem_T_str = N_("TiB");

const int disk_K = 1024;
const char * disk_K_str = N_("KiB");
const char * disk_M_str = N_("MiB");
const char * disk_G_str = N_("GiB");
const char * disk_T_str = N_("TiB");

const int speed_K = 1024;
const char * speed_K_str = N_("KiB/s");
const char * speed_M_str = N_("MiB/s");
const char * speed_G_str = N_("GiB/s");
const char * speed_T_str = N_("TiB/s");

/***
****
***/

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

int
gtr_compare_double( const double a, const double b, int decimal_places )
{
    const int64_t ia = (int64_t)(a * pow( 10, decimal_places ) );
    const int64_t ib = (int64_t)(b * pow( 10, decimal_places ) );
    if( ia < ib ) return -1;
    if( ia > ib ) return  1;
    return 0;
}

/***
****
***/

const char*
gtr_get_unicode_string( int i )
{
    switch( i ) {
        case GTR_UNICODE_UP:      return "\xE2\x86\x91";
        case GTR_UNICODE_DOWN:    return "\xE2\x86\x93";
        case GTR_UNICODE_INF:     return "\xE2\x88\x9E";
        case GTR_UNICODE_BULLET:  return "\xE2\x88\x99";
        default:                  return "err";
    }
}

char*
tr_strlratio( char * buf, double ratio, size_t buflen )
{
    return tr_strratio( buf, buflen, ratio, gtr_get_unicode_string( GTR_UNICODE_INF ) );
}

char*
tr_strlpercent( char * buf, double x, size_t buflen )
{
    return tr_strpercent( buf, x, buflen );
}

char*
tr_strlsize( char * buf, guint64 bytes, size_t buflen )
{
    if( !bytes )
        g_strlcpy( buf, Q_( "size|None" ), buflen );
    else
        tr_formatter_size_B( buf, bytes, buflen );

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

    g_snprintf( d, sizeof( d ), gtr_ngettext( "%'d day", "%'d days", days ), days );
    g_snprintf( h, sizeof( h ), gtr_ngettext( "%'d hour", "%'d hours", hours ), hours );
    g_snprintf( m, sizeof( m ), gtr_ngettext( "%'d minute", "%'d minutes", minutes ), minutes );
    g_snprintf( s, sizeof( s ), gtr_ngettext( "%'d second", "%'d seconds", seconds ), seconds );

    if( days )
    {
        if( days >= 4 || !hours )
            g_strlcpy( buf, d, buflen );
        else
            g_snprintf( buf, buflen, "%s, %s", d, h );
    }
    else if( hours )
    {
        if( hours >= 4 || !minutes )
            g_strlcpy( buf, h, buflen );
        else
            g_snprintf( buf, buflen, "%s, %s", h, m );
    }
    else if( minutes )
    {
        if( minutes >= 4 || !seconds )
            g_strlcpy( buf, m, buflen );
        else
            g_snprintf( buf, buflen, "%s, %s", m, s );
    }
    else
    {
        g_strlcpy( buf, s, buflen );
    }

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

/* pattern-matching text; ie, legaltorrents.com */
char*
gtr_get_host_from_url( const char * url )
{
    char * name;
    char * h = NULL;

    tr_urlParse( url, -1, NULL, &h, NULL, NULL );

    if( tr_addressIsIP( h ) )
        name = g_strdup( h );
    else {
        const char * first_dot = strchr( h, '.' );
        const char * last_dot = strrchr( h, '.' );
        if( ( first_dot ) && ( last_dot ) && ( first_dot != last_dot ) )
            name = g_strdup( first_dot + 1 );
        else
            name = g_strdup( h );
    }

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
gtr_add_torrent_error_dialog( GtkWidget * child, int err, const char * file )
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
    secondary = g_strdup_printf( fmt, file );

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

const gchar *
gtr_ngettext( const gchar * msgid,
              const gchar * msgid_plural,
              gulong n )
{
#if GLIB_CHECK_VERSION( 2, 18, 0 )
    return g_dngettext( NULL, msgid, msgid_plural, n );
#else
    return ngettext( msgid, msgid_plural, n );
#endif
}

int
gtr_file_trash_or_remove( const char * filename )
{
    if( filename && g_file_test( filename, G_FILE_TEST_EXISTS ) )
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

const char*
gtr_get_help_uri( void )
{
    static char * uri = NULL;

    if( !uri )
    {
        int major, minor;
        const char * fmt = "http://www.transmissionbt.com/help/gtk/%d.%dx";
        sscanf( SHORT_VERSION_STRING, "%d.%d", &major, &minor );
        uri = g_strdup_printf( fmt, major, minor / 10 );
    }

    return uri;
}

void
gtr_open_file( const char * path )
{
    char * uri = NULL;

#ifdef HAVE_GIO
    GFile * file = g_file_new_for_path( path );
    uri = g_file_get_uri( file );
    g_object_unref( G_OBJECT( file ) );
#else
    if( g_path_is_absolute( path ) )
        uri = g_strdup_printf( "file://%s", path );
    else {
        char * cwd = g_get_current_dir();
        uri = g_strdup_printf( "file://%s/%s", cwd, path );
        g_free( cwd );
    }
#endif

    gtr_open_uri( uri );
    g_free( uri );
}

void
gtr_open_uri( const char * uri )
{
    if( uri )
    {
        gboolean opened = FALSE;

#ifdef HAVE_GIO
        if( !opened )
            opened = g_app_info_launch_default_for_uri( uri, NULL, NULL );
#endif

        if( !opened ) {
            char * argv[] = { (char*)"xdg-open", (char*)uri, NULL };
            opened = g_spawn_async( NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                                    NULL, NULL, NULL, NULL );
        }

        if( !opened )
            g_message( "Unable to open \"%s\"", uri );
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

/***
****
***/

void
gtr_combo_box_set_active_enum( GtkComboBox * combo_box, int value )
{
    int i;
    int currentValue;
    const int column = 0;
    GtkTreeIter iter;
    GtkTreeModel * model = gtk_combo_box_get_model( combo_box );

    /* do the value and current value match? */
    if( gtk_combo_box_get_active_iter( combo_box, &iter ) ) {
        gtk_tree_model_get( model, &iter, column, &currentValue, -1 );
        if( currentValue == value )
            return;
    }

    /* find the one to select */
    i = 0;
    while(( gtk_tree_model_iter_nth_child( model, &iter, NULL, i++ ))) {
        gtk_tree_model_get( model, &iter, column, &currentValue, -1 );
        if( currentValue == value ) {
            gtk_combo_box_set_active_iter( combo_box, &iter );
            return;
        }
    }
}


GtkWidget *
gtr_combo_box_new_enum( const char * text_1, ... )
{
    GtkWidget * w;
    GtkCellRenderer * r;
    GtkListStore * store;
    va_list vl;
    const char * text;
    va_start( vl, text_1 );

    store = gtk_list_store_new( 2, G_TYPE_INT, G_TYPE_STRING );

    text = text_1;
    if( text != NULL ) do
    {
        const int val = va_arg( vl, int );
        gtk_list_store_insert_with_values( store, NULL, INT_MAX, 0, val, 1, text, -1 );
        text = va_arg( vl, const char * );
    }
    while( text != NULL );

    w = gtk_combo_box_new_with_model( GTK_TREE_MODEL( store ) );
    r = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( w ), r, TRUE );
    gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT( w ), r, "text", 1, NULL );

    /* cleanup */
    g_object_unref( store );
    return w;
}

int
gtr_combo_box_get_active_enum( GtkComboBox * combo_box )
{
    int value = 0;
    GtkTreeIter iter;

    if( gtk_combo_box_get_active_iter( combo_box, &iter ) )
        gtk_tree_model_get( gtk_combo_box_get_model( combo_box ), &iter, 0, &value, -1 );

    return value;
}

GtkWidget *
gtr_priority_combo_new( void )
{
    return gtr_combo_box_new_enum( _( "High" ),   TR_PRI_HIGH,
                                   _( "Normal" ), TR_PRI_NORMAL,
                                   _( "Low" ),    TR_PRI_LOW,
                                   NULL );
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

GdkWindow*
gtr_widget_get_window( GtkWidget * w )
{
#if GTK_CHECK_VERSION( 2,14,0 )
    return gtk_widget_get_window( w );
#else
    return w->window;
#endif
}

gboolean
gtr_widget_get_realized( GtkWidget * w )
{
#if GTK_CHECK_VERSION( 2,20,0 )
    return gtk_widget_get_realized( w );
#else
    return GTK_WIDGET_REALIZED( w ) != 0;
#endif
}

void
gtr_widget_set_visible( GtkWidget * w, gboolean b )
{
    /* toggle the transient children, too */
    if( GTK_IS_WINDOW( w ) )
    {
        GList * l;
        GList * windows = gtk_window_list_toplevels( );
        GtkWindow * window = GTK_WINDOW( w );

        for( l=windows; l!=NULL; l=l->next )
            if( GTK_IS_WINDOW( l->data ) )
                if( gtk_window_get_transient_for( GTK_WINDOW( l->data ) ) == window )
                    gtr_widget_set_visible( GTK_WIDGET( l->data ), b );

        g_list_free( windows );
    }

#if GTK_CHECK_VERSION( 2,18,0 )
    gtk_widget_set_visible( w, b );
#else
    if( b )
        gtk_widget_show( w );
    else
        gtk_widget_hide( w );
#endif
}

static GtkWidget*
gtr_dialog_get_content_area( GtkDialog * dialog )
{
#if GTK_CHECK_VERSION( 2,14,0 )
    return gtk_dialog_get_content_area( dialog );
#else
    return dialog->vbox;
#endif
}

void
gtr_dialog_set_content( GtkDialog * dialog, GtkWidget * content )
{
    GtkWidget * vbox = gtr_dialog_get_content_area( dialog );
    gtk_box_pack_start( GTK_BOX( vbox ), content, TRUE, TRUE, 0 );
    gtk_widget_show_all( content );
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

guint
gtr_idle_add( GSourceFunc function, gpointer data )
{
#if GTK_CHECK_VERSION( 2,12,0 )
    return gdk_threads_add_idle( function, data );
#else
    return g_idle_add_full( G_PRIORITY_DEFAULT,
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
gtr_http_failure_dialog( GtkWidget * parent, const char * url, long response_code )
{
    GtkWindow * window = getWindow( parent );

    GtkWidget * w = gtk_message_dialog_new( window, 0,
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_CLOSE,
                                            _( "Error opening \"%s\"" ), url );

    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ),
                                              _( "Server returned \"%1$ld %2$s\"" ),
                                              response_code,
                                              tr_webGetResponseStr( response_code ) );

    g_signal_connect_swapped( w, "response", G_CALLBACK( gtk_widget_destroy ), w );
    gtk_widget_show( w );
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
        g_string_append_printf( gstr, _( "This magnet link appears to be intended for something other than BitTorrent. BitTorrent magnet links have a section containing \"%s\"." ), xt );
    }

    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ), "%s", gstr->str );
    g_signal_connect_swapped( w, "response", G_CALLBACK( gtk_widget_destroy ), w );
    gtk_widget_show( w );
    g_string_free( gstr, TRUE );
}

/***
****
***/

void
gtr_paste_clipboard_url_into_entry( GtkWidget * e )
{
  size_t i;

  char * text[] = {
    gtk_clipboard_wait_for_text( gtk_clipboard_get( GDK_SELECTION_PRIMARY ) ),
    gtk_clipboard_wait_for_text( gtk_clipboard_get( GDK_SELECTION_CLIPBOARD ) )
  };

  for( i=0; i<G_N_ELEMENTS(text); ++i ) {
      char * s = text[i];
      if( s && ( gtr_is_supported_url( s ) || gtr_is_magnet_link( s )
                                           || gtr_is_hex_hashcode( s ) ) ) {
          gtk_entry_set_text( GTK_ENTRY( e ), s );
          break;
      }
  }

  for( i=0; i<G_N_ELEMENTS(text); ++i )
    g_free( text[i] );
}

/***
****
***/

void
gtr_label_set_text( GtkLabel * lb, const char * newstr )
{
    const char * oldstr = gtk_label_get_text( lb );

    if( ( oldstr == NULL ) || strcmp( oldstr, newstr ) )
        gtk_label_set_text( lb, newstr );
}
