/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#include <ctype.h> /* isspace */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h> /* free() */
#include <unistd.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>
#include <libtransmission/web.h>
#include "conf.h"
#include "hig.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
void
tr_prefs_init_global( void )
{
    int i;
    char pw[32];
    const char * str;
    const char * pool = "abcdefghijklmnopqrstuvwxyz"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                        "1234567890";

    cf_check_older_configs( );

#if HAVE_GIO
    str = NULL;
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DESKTOP );
    if( !str ) str = g_get_home_dir( );
    pref_string_set_default ( PREF_KEY_DIR_WATCH, str );
    pref_flag_set_default   ( PREF_KEY_DIR_WATCH_ENABLED, FALSE );
#endif

    pref_int_set_default    ( PREF_KEY_PEER_SOCKET_TOS, TR_DEFAULT_PEER_SOCKET_TOS );
    pref_flag_set_default   ( PREF_KEY_ALLOW_HIBERNATION, TRUE );
    pref_flag_set_default   ( PREF_KEY_BLOCKLIST_ENABLED, TR_DEFAULT_BLOCKLIST_ENABLED );

    pref_string_set_default ( PREF_KEY_OPEN_DIALOG_FOLDER, g_get_home_dir( ) );

    pref_int_set_default    ( PREF_KEY_MAX_PEERS_GLOBAL, TR_DEFAULT_GLOBAL_PEER_LIMIT );
    pref_int_set_default    ( PREF_KEY_MAX_PEERS_PER_TORRENT, 50 );

    pref_flag_set_default   ( PREF_KEY_TOOLBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_FILTERBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_STATUSBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_SHOW_TRAY_ICON, FALSE );
    pref_string_set_default ( PREF_KEY_STATUSBAR_STATS, "total-ratio" );

    pref_flag_set_default   ( PREF_KEY_DL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_DL_LIMIT, 100 );
    pref_flag_set_default   ( PREF_KEY_UL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_UL_LIMIT, 50 );
    pref_flag_set_default   ( PREF_KEY_OPTIONS_PROMPT, TRUE );

    pref_int_set_default    ( PREF_KEY_MAIN_WINDOW_HEIGHT, 500 );
    pref_int_set_default    ( PREF_KEY_MAIN_WINDOW_WIDTH, 300 );
    pref_int_set_default    ( PREF_KEY_MAIN_WINDOW_X, 50 );
    pref_int_set_default    ( PREF_KEY_MAIN_WINDOW_Y, 50 );

    pref_string_set_default ( PREF_KEY_PROXY_SERVER, "" );
    pref_int_set_default    ( PREF_KEY_PROXY_TYPE, TR_PROXY_HTTP );
    pref_flag_set_default   ( PREF_KEY_PROXY_SERVER_ENABLED, FALSE );
    pref_flag_set_default   ( PREF_KEY_PROXY_AUTH_ENABLED, FALSE );
    pref_string_set_default ( PREF_KEY_PROXY_USERNAME, "" );
    pref_string_set_default ( PREF_KEY_PROXY_PASSWORD, "" );

    str = NULL;
#if GLIB_CHECK_VERSION(2,14,0)
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DOWNLOAD );
#endif
    if( !str ) str = g_get_home_dir( );
    pref_string_set_default ( PREF_KEY_DOWNLOAD_DIR, str );

    pref_int_set_default    ( PREF_KEY_PORT, TR_DEFAULT_PORT );

    pref_flag_set_default   ( PREF_KEY_NAT, TRUE );
    pref_flag_set_default   ( PREF_KEY_PEX, TR_DEFAULT_PEX_ENABLED );
    pref_flag_set_default   ( PREF_KEY_ASKQUIT, TRUE );
    pref_flag_set_default   ( PREF_KEY_ENCRYPTED_ONLY, FALSE );

    pref_int_set_default    ( PREF_KEY_MSGLEVEL, TR_MSG_INF );

    pref_string_set_default ( PREF_KEY_SORT_MODE, "sort-by-name" );
    pref_flag_set_default   ( PREF_KEY_SORT_REVERSED, FALSE );
    pref_flag_set_default   ( PREF_KEY_MINIMAL_VIEW, FALSE );

    pref_flag_set_default   ( PREF_KEY_START, TRUE );
    pref_flag_set_default   ( PREF_KEY_TRASH_ORIGINAL, FALSE );

    pref_flag_set_default   ( PREF_KEY_RPC_ENABLED, TR_DEFAULT_RPC_ENABLED );
    pref_int_set_default    ( PREF_KEY_RPC_PORT, TR_DEFAULT_RPC_PORT );
    pref_string_set_default ( PREF_KEY_RPC_ACL, TR_DEFAULT_RPC_ACL );

    for( i=0; i<16; ++i )
        pw[i] = pool[ tr_rand( strlen( pool ) ) ];
    pw[16] = '\0';
    pref_string_set_default( PREF_KEY_RPC_USERNAME, "transmission" );
    pref_string_set_default( PREF_KEY_RPC_PASSWORD, pw );
    pref_flag_set_default  ( PREF_KEY_RPC_AUTH_ENABLED, FALSE );

    pref_save( NULL );
}

/**
***
**/

#define PREF_KEY "pref-key"

static void
response_cb( GtkDialog * dialog, int response, gpointer unused UNUSED )
{
    if( response == GTK_RESPONSE_HELP ) {
        char * base = gtr_get_help_url( );
        char * url = g_strdup_printf( "%s/html/preferences.html", base );
        gtr_open_file( url );
        g_free( url );
        g_free( base );
    }

    if( response == GTK_RESPONSE_CLOSE )
        gtk_widget_destroy( GTK_WIDGET(dialog) );
}

static void
toggled_cb( GtkToggleButton * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREF_KEY );
    const gboolean flag = gtk_toggle_button_get_active( w );
    tr_core_set_pref_bool( TR_CORE(core), key, flag );
}

static GtkWidget*
new_check_button( const char * mnemonic, const char * key, gpointer core )
{
    GtkWidget * w = gtk_check_button_new_with_mnemonic( mnemonic );
    g_object_set_data_full( G_OBJECT(w), PREF_KEY, g_strdup(key), g_free );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(w), pref_flag_get(key) );
    g_signal_connect( w, "toggled", G_CALLBACK(toggled_cb), core );
    return w;
}

static void
spun_cb( GtkSpinButton * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREF_KEY );
    const int value = gtk_spin_button_get_value_as_int( w );
    tr_core_set_pref_int( TR_CORE(core), key, value );
}

static GtkWidget*
new_spin_button( const char * key, gpointer core, int low, int high, int step )
{
    GtkWidget * w = gtk_spin_button_new_with_range( low, high, step );
    g_object_set_data_full( G_OBJECT(w), PREF_KEY, g_strdup(key), g_free );
    gtk_spin_button_set_digits( GTK_SPIN_BUTTON(w), 0 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), pref_int_get(key) );
    g_signal_connect( w, "value-changed", G_CALLBACK(spun_cb), core );
    return w;
}

static void
entry_changed_cb( GtkEntry * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT( w ), PREF_KEY );
    const char * value = gtk_entry_get_text( w );
    tr_core_set_pref( TR_CORE( core ), key, value );
}

static GtkWidget*
new_entry( const char * key, gpointer core )
{
    GtkWidget * w = gtk_entry_new( );
    char * value = pref_string_get( key );
    if( value )
        gtk_entry_set_text( GTK_ENTRY( w ), value );
    g_object_set_data_full( G_OBJECT(w), PREF_KEY, g_strdup(key), g_free );
    g_signal_connect( w, "changed", G_CALLBACK(entry_changed_cb), core );
    g_free( value );
    return w;
}

static void
chosen_cb( GtkFileChooser * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREF_KEY );
    char * value = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(w) );
    tr_core_set_pref( TR_CORE(core), key, value );
    g_free( value );
}

static GtkWidget*
new_path_chooser_button( const char * key, gpointer core )
{
    GtkWidget * w = gtk_file_chooser_button_new( NULL,
                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
    char * path = pref_string_get( key );
    g_object_set_data_full( G_OBJECT(w), PREF_KEY, g_strdup(key), g_free );
    g_signal_connect( w, "selection-changed", G_CALLBACK(chosen_cb), core );
    gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER(w), path );
    g_free( path );
    return w;
}

static void
target_cb( GtkWidget * tb, gpointer target )
{
    const gboolean b = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( tb ) );
    gtk_widget_set_sensitive( GTK_WIDGET(target), b );
}

struct test_port_data
{
    GtkWidget * label;
    gboolean * alive;
};

static void
testing_port_done( tr_handle   * handle         UNUSED,
                   long          response_code  UNUSED,
                   const void  * response,
                   size_t        response_len,
                   void        * gdata )
{
    struct test_port_data * data = gdata;
    if( *data->alive )
    {
        const int isOpen = response_len && *(char*)response=='1';
        gtk_label_set_markup( GTK_LABEL( data->label ), isOpen
                              ? _("Port is <b>open</b>")
                              : _("Port is <b>closed</b>") );
    }
}

static gboolean
testing_port_begin( gpointer gdata )
{
    struct test_port_data * data = gdata;
    if( *data->alive )
    {
        GtkSpinButton * spin = g_object_get_data( G_OBJECT( data->label ), "tr-port-spin" );
        tr_handle * handle = g_object_get_data( G_OBJECT( data->label ), "handle" );
        const int port = gtk_spin_button_get_value_as_int( spin );
        char url[256];
        snprintf( url, sizeof(url), "http://portcheck.transmissionbt.com/%d", port );
        tr_webRun( handle, url, NULL, testing_port_done, data );
    }
    return FALSE;
}

static void
testing_port_cb( GtkWidget * unused UNUSED, gpointer l )
{
    gtk_label_set_markup( GTK_LABEL( l ), _( "<i>Testing port...</i>" ) );
    /* wait three seconds to give the port forwarding time to kick in */
    struct test_port_data * data = g_new0( struct test_port_data, 1 );
    data->label = l;
    data->alive = g_object_get_data( G_OBJECT( l ), "alive" );
    g_timeout_add( 3000, testing_port_begin, data );
}

static void
dialogDestroyed( gpointer alive, GObject * dialog UNUSED )
{
    *(gboolean*)alive = FALSE;
}

static GtkWidget*
torrentPage( GObject * core )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w;
#ifdef HAVE_GIO
    GtkWidget * l;
#endif

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Adding Torrents" ) );

#ifdef HAVE_GIO
        s = _( "Automatically add torrents from:" );
        l = new_check_button( s, PREF_KEY_DIR_WATCH_ENABLED, core );
        w = new_path_chooser_button( PREF_KEY_DIR_WATCH, core );
        gtk_widget_set_sensitive( GTK_WIDGET(w), pref_flag_get( PREF_KEY_DIR_WATCH_ENABLED ) );
        g_signal_connect( l, "toggled", G_CALLBACK(target_cb), w );
        hig_workarea_add_row_w( t, &row, l, w, NULL );
#endif

        s = _( "Display _options dialog" );
        w = new_check_button( s, PREF_KEY_OPTIONS_PROMPT, core );
        hig_workarea_add_wide_control( t, &row, w );

        s = _( "_Start when added" );
        w = new_check_button( s, PREF_KEY_START, core );
        hig_workarea_add_wide_control( t, &row, w );

        s = _( "Mo_ve source files to Trash" );
        w = new_check_button( s, PREF_KEY_TRASH_ORIGINAL, core ); 
        hig_workarea_add_wide_control( t, &row, w );

        w = new_path_chooser_button( PREF_KEY_DOWNLOAD_DIR, core );
        hig_workarea_add_row( t, &row, _( "_Destination folder:" ), w, NULL );

    hig_workarea_finish( t, &row );
    return t;
}

/***
****
***/

struct blocklist_data
{
    GtkWidget * check;
    GtkWidget * dialog;
    TrCore * core;
    int abortFlag;
    char secondary[256];
};

static void
updateBlocklistText( GtkWidget * w, TrCore * core )
{
    const int n = tr_blocklistGetRuleCount( tr_core_handle( core ) );
    char buf[512];
    g_snprintf( buf, sizeof( buf ),
                ngettext( "Ignore the %'d _blocklisted peer",
                          "Ignore the %'d _blocklisted peers", n ), n );
    gtk_button_set_label( GTK_BUTTON( w ), buf );
}
static gboolean
updateBlocklistTextFromData( gpointer gdata )
{
    struct blocklist_data * data = gdata;
    updateBlocklistText( data->check, data->core );
    return FALSE;
}

static gboolean
blocklistDialogSetSecondary( gpointer gdata )
{
    struct blocklist_data * data = gdata;
    GtkMessageDialog * md = GTK_MESSAGE_DIALOG( data->dialog );
    gtk_message_dialog_format_secondary_text( md, data->secondary );
    return FALSE;
}

static gboolean
blocklistDialogAllowClose( gpointer dialog )
{
    GtkDialog * d = GTK_DIALOG( dialog );
    gtk_dialog_set_response_sensitive( GTK_DIALOG( d ), GTK_RESPONSE_CANCEL, FALSE );
    gtk_dialog_set_response_sensitive( GTK_DIALOG( d ), GTK_RESPONSE_CLOSE, TRUE );
    return FALSE;
}

static void
got_blocklist( tr_handle   * handle         UNUSED,
               long          response_code  UNUSED,
               const void  * response,
               size_t        response_len,
               void        * gdata )
{
    struct blocklist_data * data = gdata;
    const char * text = response;
    int size = response_len;
    int rules = 0;
    gchar * filename = NULL;
    gchar * filename2 = NULL;
    int fd = -1;
    int ok = 1;

    if( !data->abortFlag && ( !text || !size ) )
    {
        ok = FALSE;
        g_snprintf( data->secondary, sizeof( data->secondary ),
                    _( "Unable to get blocklist." ) );
        g_message( data->secondary );
        g_idle_add( blocklistDialogSetSecondary, data );
    }      

    if( ok && !data->abortFlag )
    {
        GError * err = NULL;
        fd = g_file_open_tmp( "transmission-blockfile-XXXXXX", &filename, &err );
        if( err ) {
            g_snprintf( data->secondary, sizeof( data->secondary ),
                        _( "Unable to get blocklist: %s" ), err->message );
            g_warning( data->secondary );
            g_idle_add( blocklistDialogSetSecondary, data );
            g_clear_error( &err );
            ok = FALSE;
        } else {
            write( fd, text, size );
            close( fd );
        }
    }
    if( ok && !data->abortFlag )
    {
        filename2 = g_strdup_printf( "%s.txt", filename );
        g_snprintf( data->secondary, sizeof( data->secondary ),
                    _( "Uncompressing blocklist..." ) );
        g_idle_add( blocklistDialogSetSecondary, data );
        char * cmd = g_strdup_printf( "zcat %s > %s ", filename, filename2 );
        tr_dbg( "%s", cmd );
        system( cmd );
        g_free( cmd );
    }
    if( ok && !data->abortFlag )
    {
        g_snprintf( data->secondary, sizeof( data->secondary ),
                    _( "Parsing blocklist..." ) );
        g_idle_add( blocklistDialogSetSecondary, data );
        rules = tr_blocklistSetContent( tr_core_handle( data->core ), filename2 );
    }
    if( ok && !data->abortFlag )
    {
        g_snprintf( data->secondary, sizeof( data->secondary ),
                    _( "Blocklist updated with %'d entries" ), rules );
        g_idle_add( blocklistDialogSetSecondary, data );
        g_idle_add( blocklistDialogAllowClose, data->dialog );
        g_idle_add( updateBlocklistTextFromData, data );
    }

    /* g_free( data ); */
    if( filename2 ) {
        unlink( filename2 );
        g_free( filename2 );
    }
    if( filename ) {
        unlink( filename );
        g_free( filename );
    }
}

static void
onUpdateBlocklistResponseCB( GtkDialog * dialog, int response, gpointer vdata )
{
    struct blocklist_data * data = vdata;

    if( response == GTK_RESPONSE_CANCEL )
        data->abortFlag = 1;

    data->dialog = NULL;
    gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

static void
onUpdateBlocklistCB( GtkButton * w, gpointer gdata )
{
    GtkWidget * d;
    struct blocklist_data * data = gdata;
    tr_handle * handle = g_object_get_data( G_OBJECT( w ), "handle" );
    const char * url = "http://download.m0k.org/transmission/files/level1.gz";
    
    d = gtk_message_dialog_new( GTK_WINDOW( gtk_widget_get_toplevel( GTK_WIDGET( w ) ) ),
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_MESSAGE_INFO,
                                GTK_BUTTONS_NONE,
                                _( "Updating Blocklist" ) );
    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( d ),
                                              _( "Retrieving blocklist..." ) );
    gtk_dialog_add_buttons( GTK_DIALOG( d ),
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                            NULL );
    gtk_dialog_set_response_sensitive( GTK_DIALOG( d ), GTK_RESPONSE_CLOSE, FALSE );

    data->dialog = d;

    g_signal_connect( d, "response", G_CALLBACK(onUpdateBlocklistResponseCB), data );
    gtk_widget_show( d );

    tr_webRun( handle, url, NULL, got_blocklist, data );
}

static GtkWidget*
peerPage( GObject * core, gboolean * alive )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w;
    GtkWidget * w2;
    GtkWidget * b;
    GtkWidget * h;
    GtkWidget * l;
    struct blocklist_data * data;

    t = hig_workarea_create( );
    hig_workarea_add_section_title (t, &row, _("Options"));

        w = new_check_button( "", PREF_KEY_BLOCKLIST_ENABLED, core );
        updateBlocklistText( w, TR_CORE( core ) );
        h = gtk_hbox_new( FALSE, GUI_PAD_BIG );
        gtk_box_pack_start_defaults( GTK_BOX(h), w );
        b = gtk_button_new_with_mnemonic( _( "_Update Blocklist" ) );

        data = g_new0( struct blocklist_data, 1 );
        data->core = TR_CORE( core );
        data->check = w;

        g_object_set_data( G_OBJECT( b ), "handle", tr_core_handle( TR_CORE( core ) ) );
        g_signal_connect( b, "clicked", G_CALLBACK(onUpdateBlocklistCB), data );
        gtk_box_pack_start( GTK_BOX(h), b, FALSE, FALSE, 0 );
        g_signal_connect( w, "toggled", G_CALLBACK(target_cb), b );
        target_cb( w, b );
        hig_workarea_add_wide_control( t, &row, h );
        
        s = _("_Ignore unencrypted peers");
        w = new_check_button( s, PREF_KEY_ENCRYPTED_ONLY, core );
        hig_workarea_add_wide_control( t, &row, w );

        s = _("Use peer e_xchange");
        w = new_check_button( s, PREF_KEY_PEX, core );
        hig_workarea_add_wide_control( t, &row, w );

        h = gtk_hbox_new( FALSE, GUI_PAD_BIG );
        w2 = new_spin_button( PREF_KEY_PORT, core, 1, INT_MAX, 1 );
        gtk_box_pack_start( GTK_BOX(h), w2, FALSE, FALSE, 0 );
        l = gtk_label_new( NULL );
        gtk_misc_set_alignment( GTK_MISC(l), 0.0f, 0.5f );
        gtk_box_pack_start( GTK_BOX(h), l, FALSE, FALSE, 0 );
        hig_workarea_add_row( t, &row, _("Listening _port:"), h, w2 );

        g_object_set_data( G_OBJECT(l), "tr-port-spin", w2 );
        g_object_set_data( G_OBJECT(l), "alive", alive );
        g_object_set_data( G_OBJECT(l), "handle", tr_core_handle( TR_CORE( core ) ) );
        testing_port_cb( NULL, l );
        g_signal_connect( w2, "value-changed", G_CALLBACK(testing_port_cb), l );
        
    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Limits" ) );
  
        w = new_spin_button( PREF_KEY_MAX_PEERS_GLOBAL, core, 1, 3000, 5 );
        hig_workarea_add_row( t, &row, _( "Maximum peers _overall:" ), w, NULL );
        w = new_spin_button( PREF_KEY_MAX_PEERS_PER_TORRENT, core, 1, 300, 5 );
        hig_workarea_add_row( t, &row, _( "Maximum peers per _torrent:" ), w, NULL );

    hig_workarea_finish( t, &row );
    return t;
}

static GtkTreeModel*
allow_deny_model_new( void )
{
    GtkTreeIter iter;
    GtkListStore * store = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_CHAR );
    gtk_list_store_append( store, &iter );
    gtk_list_store_set( store, &iter, 0, _( "Allow" ), 1, '+', -1 );
    gtk_list_store_append( store, &iter );
    gtk_list_store_set( store, &iter, 0, _( "Deny" ), 1, '-', -1 );
    return GTK_TREE_MODEL( store );
}

enum
{
    COL_ADDRESS,
    COL_PERMISSION,
    N_COLS
};

static GtkTreeModel*
acl_tree_model_new( const char * acl )
{
    int i;
    char ** rules;
    GtkListStore * store = gtk_list_store_new( N_COLS,
                                               G_TYPE_STRING,
                                               G_TYPE_STRING );
    rules = g_strsplit( acl, ",", 0 );

    for( i=0; rules && rules[i]; ++i )
    {
        const char * s = rules[i];
        while( isspace( *s ) ) ++s;
        if( *s=='+' || *s=='-' )
        {
            GtkTreeIter iter;
            gtk_list_store_append( store, &iter );
            gtk_list_store_set( store, &iter,
                COL_PERMISSION, *s=='+' ? _( "Allow" ) : _( "Deny" ) ,
                COL_ADDRESS, s+1,
                -1 );
        }
    }

    g_strfreev( rules );
    return GTK_TREE_MODEL( store );
}

struct remote_page
{
    TrCore * core;
    GtkTreeView * view;
    GtkListStore * store;
    GtkWidget * remove_button;
    GSList * widgets;
    GSList * auth_widgets;
    GtkToggleButton * rpc_tb;
    GtkToggleButton * auth_tb;
};

static void
refreshACL( struct remote_page * page )
{
    GtkTreeIter iter;
    GtkTreeModel * model = GTK_TREE_MODEL( page->store );
    GString * gstr = g_string_new( NULL );

    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
    {
        char * permission;
        char * address;
        gtk_tree_model_get( model, &iter, COL_PERMISSION, &permission,
                                          COL_ADDRESS, &address,
                                          -1 );
        g_string_append_c( gstr, strcmp(permission,_("Allow")) ? '-' : '+' );
        g_string_append( gstr, address );
        g_string_append( gstr, ", " );
        g_free( address );
        g_free( permission );
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    g_string_truncate( gstr, gstr->len-2 ); /* remove the trailing ", " */

    tr_core_set_pref( page->core, PREF_KEY_RPC_ACL, gstr->str );

    g_string_free( gstr, TRUE );
}

static void
onPermissionEdited( GtkCellRendererText  * renderer UNUSED,
                    gchar                * path_string,
                    gchar                * new_text,
                    gpointer               gpage )
{
    GtkTreeIter iter;
    GtkTreePath * path = gtk_tree_path_new_from_string( path_string );
    struct remote_page * page = gpage;
    GtkTreeModel * model = GTK_TREE_MODEL( page->store );
    if( gtk_tree_model_get_iter( model, &iter, path ) )
        gtk_list_store_set( page->store, &iter, COL_PERMISSION, new_text, -1 );
    gtk_tree_path_free( path );
    refreshACL( page );
}

static void
onAddressEdited( GtkCellRendererText  * r UNUSED,
                 gchar                * path_string,
                 gchar                * new_text,
                 gpointer               gpage )
{
    char * acl;
    GtkTreeIter iter;
    struct remote_page * page = gpage;
    GtkTreeModel * model = GTK_TREE_MODEL( page->store );
    tr_handle * session = tr_core_handle( page->core );
    GtkTreePath * path = gtk_tree_path_new_from_string( path_string );

    acl = g_strdup_printf( "+%s", new_text );
    if( !tr_sessionTestRPCACL( session, acl, NULL ) )
        if( gtk_tree_model_get_iter( model, &iter, path ) )
            gtk_list_store_set( page->store, &iter, COL_ADDRESS, new_text, -1 );

    g_free( acl );
    gtk_tree_path_free( path );
    refreshACL( page );
}

static void
onAddACLClicked( GtkButton * b UNUSED, gpointer gpage )
{
    GtkTreeIter iter;
    GtkTreePath * path;
    struct remote_page * page = gpage;
    gtk_list_store_append( page->store, &iter );
    gtk_list_store_set( page->store, &iter,
                        COL_PERMISSION, _( "Allow" ),
                        COL_ADDRESS, _( "0.0.0.0" ),
                        -1 );

    path = gtk_tree_model_get_path( GTK_TREE_MODEL( page->store ), &iter );
    gtk_tree_view_set_cursor(
        page->view, path,
        gtk_tree_view_get_column( page->view, COL_ADDRESS ),
        TRUE );
    gtk_tree_path_free( path );
}

static void
onRemoveACLClicked( GtkButton * b UNUSED, gpointer gpage )
{
    struct remote_page * page = gpage;
    GtkTreeSelection * sel = gtk_tree_view_get_selection( page->view );
    GtkTreeIter iter;
    if( gtk_tree_selection_get_selected( sel, NULL, &iter ) )
    {
        gtk_list_store_remove( page->store, &iter );
        refreshACL( page );
    }
}

static void
refreshRPCSensitivity( struct remote_page * page )
{
    GSList * l;
    const int rpc_active = gtk_toggle_button_get_active( page->rpc_tb );
    const int auth_active = gtk_toggle_button_get_active( page->auth_tb );
    GtkTreeSelection * sel = gtk_tree_view_get_selection( page->view );
    const int have_addr = gtk_tree_selection_get_selected( sel, NULL, NULL );
    const int n_rules = gtk_tree_model_iter_n_children(
                                       GTK_TREE_MODEL( page->store ), NULL );

    for( l=page->widgets; l!=NULL; l=l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ), rpc_active );

    for( l=page->auth_widgets; l!=NULL; l=l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ), rpc_active && auth_active);

    gtk_widget_set_sensitive( page->remove_button,
                              rpc_active && have_addr && n_rules>1 );
}

static void
onRPCToggled( GtkToggleButton * tb UNUSED, gpointer page )
{
    refreshRPCSensitivity( page );
}
static void
onACLSelectionChanged( GtkTreeSelection * sel UNUSED, gpointer page )
{
    refreshRPCSensitivity( page );
}

static GtkWidget*
remotePage( GObject * core )
{
    const char  * s;
    int row = 0;
    GtkWidget * t;
    GtkWidget * w;
    struct remote_page * page = g_new0( struct remote_page, 1 );

    page->core = TR_CORE( core );

    t = hig_workarea_create( );
    g_object_set_data_full( G_OBJECT( t ), "page", page, g_free );

    hig_workarea_add_section_title( t, &row, _( "Remote Access" ) );

        /* "enabled" checkbutton */
        s = _( "A_llow requests from transmission-remote, Clutch, etc." );
        w = new_check_button( s, PREF_KEY_RPC_ENABLED, core );
        hig_workarea_add_wide_control( t, &row, w );
        page->rpc_tb = GTK_TOGGLE_BUTTON( w );
        g_signal_connect( w, "clicked", G_CALLBACK(onRPCToggled), page );

        /* require authentication */
        s = _( "Require _authentication" );
        w = new_check_button( s, PREF_KEY_RPC_AUTH_ENABLED, core );
        hig_workarea_add_wide_control( t, &row, w );
        page->auth_tb = GTK_TOGGLE_BUTTON( w );
        page->widgets = g_slist_append( page->widgets, w );
        g_signal_connect( w, "clicked", G_CALLBACK(onRPCToggled), page );

        /* username */
        s = _( "_Username:" );
        w = new_entry( PREF_KEY_RPC_USERNAME, core );
        page->auth_widgets = g_slist_append( page->auth_widgets, w );
        w = hig_workarea_add_row( t, &row, s, w, NULL );
        page->auth_widgets = g_slist_append( page->auth_widgets, w );

        /* password */
        s = _( "_Password:" );
        w = new_entry( PREF_KEY_RPC_PASSWORD, core );
        gtk_entry_set_visibility( GTK_ENTRY( w ), FALSE );
        page->auth_widgets = g_slist_append( page->auth_widgets, w );
        w = hig_workarea_add_row( t, &row, s, w, NULL );
        page->auth_widgets = g_slist_append( page->auth_widgets, w );

        /* port */
        w = new_spin_button( PREF_KEY_RPC_PORT, core, 0, 65535, 1 );
        page->widgets = g_slist_append( page->widgets, w );
        w = hig_workarea_add_row( t, &row, _( "Listening _port:" ), w, NULL );
        page->widgets = g_slist_append( page->widgets, w );

        /* access control list */
        {
        char * val = pref_string_get( PREF_KEY_RPC_ACL );
        GtkTreeModel * m = acl_tree_model_new( val );
        GtkTreeViewColumn * c;
        GtkCellRenderer * r;
        GtkTreeSelection * sel;
        GtkTreeView * v;
        GtkWidget * w;
        GtkWidget * h;
        GtkTooltips * tips = gtk_tooltips_new( );

        s = _( "Access control list:" );
        page->store = GTK_LIST_STORE( m );
        w = gtk_tree_view_new_with_model( m );

        page->widgets = g_slist_append( page->widgets, w );
        v = page->view = GTK_TREE_VIEW( w );
        gtk_tooltips_set_tip( tips, w,
            _( "IP addresses may use wildcards, such as 192.168.*.*" ),
            NULL );
        sel = gtk_tree_view_get_selection( v );
        g_signal_connect( sel, "changed",
                          G_CALLBACK( onACLSelectionChanged ), page );
        g_object_unref( G_OBJECT( m ) );
        gtk_tree_view_set_headers_visible( v, TRUE );
        w = gtk_frame_new( NULL );
        gtk_frame_set_shadow_type( GTK_FRAME( w ), GTK_SHADOW_IN );
        gtk_container_add( GTK_CONTAINER( w ), GTK_WIDGET( v ) );

        /* ip address column */
        r = gtk_cell_renderer_text_new( );
        g_signal_connect( r, "edited",
                          G_CALLBACK( onAddressEdited ), page );
        g_object_set( G_OBJECT( r ), "editable", TRUE, NULL );
        c = gtk_tree_view_column_new_with_attributes( _( "IP Address" ), r,
                "text", COL_ADDRESS,
                NULL );
        gtk_tree_view_column_set_expand( c, TRUE );
        gtk_tree_view_append_column( v, c );

        w = hig_workarea_add_row( t, &row, s, w, NULL );
        gtk_misc_set_alignment( GTK_MISC( w ), 0.0f, 0.1f );
        page->widgets = g_slist_append( page->widgets, w );
        g_free( val );

        /* permission column */
        m = allow_deny_model_new( );
        r = gtk_cell_renderer_combo_new( );
        g_object_set( G_OBJECT( r ), "model", m,
                                     "editable", TRUE,
                                     "has-entry", FALSE,
                                     "text-column", 0,
                                     NULL );
        c = gtk_tree_view_column_new_with_attributes( _( "Permission" ), r,
                "text", COL_PERMISSION,
                NULL );
        g_signal_connect( r, "edited",
                          G_CALLBACK( onPermissionEdited ), page );
        gtk_tree_view_append_column( v, c );

        h = gtk_hbox_new( TRUE, GUI_PAD );
        w = gtk_button_new_from_stock( GTK_STOCK_REMOVE );
        g_signal_connect( w, "clicked", G_CALLBACK(onRemoveACLClicked), page );
        page->remove_button = w;
        onACLSelectionChanged( sel, page );
        gtk_box_pack_start_defaults( GTK_BOX( h ), w );
        w = gtk_button_new_from_stock( GTK_STOCK_ADD );
        page->widgets = g_slist_append( page->widgets, w );
        g_signal_connect( w, "clicked", G_CALLBACK(onAddACLClicked), page );
        gtk_box_pack_start_defaults( GTK_BOX( h ), w );
        w = gtk_hbox_new( FALSE, 0 );
        gtk_box_pack_start_defaults( GTK_BOX( w ), gtk_alignment_new( 0, 0, 0, 0 ) );
        gtk_box_pack_start( GTK_BOX( w ), h, FALSE, FALSE, 0 );
        hig_workarea_add_wide_control( t, &row, w );
        }

    refreshRPCSensitivity( page );
    hig_workarea_finish( t, &row );
    return t;
}

struct ProxyPage
{
    GSList * proxy_widgets;
    GSList * proxy_auth_widgets;
};

static void
refreshProxySensitivity( struct ProxyPage * p )
{
    GSList * l;
    const gboolean proxy_enabled = pref_flag_get( PREF_KEY_PROXY_SERVER_ENABLED );
    const gboolean proxy_auth_enabled = pref_flag_get( PREF_KEY_PROXY_AUTH_ENABLED );

    for( l=p->proxy_widgets; l!=NULL; l=l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ), proxy_enabled );

    for( l=p->proxy_auth_widgets; l!=NULL; l=l->next )
        gtk_widget_set_sensitive( GTK_WIDGET( l->data ),
                                  proxy_enabled && proxy_auth_enabled);
}

static void
onProxyToggled( GtkToggleButton * tb UNUSED, gpointer user_data )
{
    refreshProxySensitivity( user_data );
}

static void
proxyPageFree( gpointer gpage )
{
    struct ProxyPage * page = gpage;
    g_slist_free( page->proxy_widgets );
    g_slist_free( page->proxy_auth_widgets );
    g_free( page );
}

static GtkWidget*
networkPage( GObject * core )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w, * w2;
    struct ProxyPage * page = tr_new0( struct ProxyPage, 1 );

    t = hig_workarea_create( );

    hig_workarea_add_section_title (t, &row, _( "Router" ) );

        s = _("Use port _forwarding from my router" );
        w = new_check_button( s, PREF_KEY_NAT, core );
        hig_workarea_add_wide_control( t, &row, w );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title (t, &row, _("Bandwidth"));

        s = _("Limit _download speed (KB/s):");
        w = new_check_button( s, PREF_KEY_DL_LIMIT_ENABLED, core );
        w2 = new_spin_button( PREF_KEY_DL_LIMIT, core, 0, INT_MAX, 5 );
        gtk_widget_set_sensitive( GTK_WIDGET(w2), pref_flag_get( PREF_KEY_DL_LIMIT_ENABLED ) );
        g_signal_connect( w, "toggled", G_CALLBACK(target_cb), w2 );
        hig_workarea_add_row_w( t, &row, w, w2, NULL );

        s = _("Limit _upload speed (KB/s):");
        w = new_check_button( s, PREF_KEY_UL_LIMIT_ENABLED, core );
        w2 = new_spin_button( PREF_KEY_UL_LIMIT, core, 0, INT_MAX, 5 );
        gtk_widget_set_sensitive( GTK_WIDGET(w2), pref_flag_get( PREF_KEY_UL_LIMIT_ENABLED ) );
        g_signal_connect( w, "toggled", G_CALLBACK(target_cb), w2 );
        hig_workarea_add_row_w( t, &row, w, w2, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title (t, &row, _( "Tracker Proxy" ) );

        s = _( "Connect to tracker with HTTP proxy" );
        w = new_check_button( s, PREF_KEY_PROXY_SERVER_ENABLED, core );
        g_signal_connect( w, "toggled", G_CALLBACK(onProxyToggled), page );
        hig_workarea_add_wide_control( t, &row, w );

        s = _( "Proxy server:" );
        w = new_entry( PREF_KEY_PROXY_SERVER, core );
        page->proxy_widgets = g_slist_append( page->proxy_widgets, w );
        w = hig_workarea_add_row( t, &row, s, w, NULL );
        page->proxy_widgets = g_slist_append( page->proxy_widgets, w );

        s = _( "_Authentication is required" );
        w = new_check_button( s, PREF_KEY_PROXY_AUTH_ENABLED, core );
        g_signal_connect( w, "toggled", G_CALLBACK(onProxyToggled), page );
        hig_workarea_add_wide_control( t, &row, w );
        page->proxy_widgets = g_slist_append( page->proxy_widgets, w );

        s = _( "_Username:" );
        w = new_entry( PREF_KEY_PROXY_USERNAME, core );
        page->proxy_auth_widgets = g_slist_append( page->proxy_auth_widgets, w );
        w = hig_workarea_add_row( t, &row, s, w, NULL );
        page->proxy_auth_widgets = g_slist_append( page->proxy_auth_widgets, w );

        s = _( "_Password:" );
        w = new_entry( PREF_KEY_PROXY_PASSWORD, core );
        gtk_entry_set_visibility( GTK_ENTRY( w ), FALSE );
        page->proxy_auth_widgets = g_slist_append( page->proxy_auth_widgets, w );
        w = hig_workarea_add_row( t, &row, s, w, NULL );
        page->proxy_auth_widgets = g_slist_append( page->proxy_auth_widgets, w );

    hig_workarea_finish( t, &row );
    g_object_set_data_full( G_OBJECT( t ), "page", page, proxyPageFree );

    refreshProxySensitivity( page );
    return t;
}

GtkWidget *
tr_prefs_dialog_new( GObject * core, GtkWindow * parent )
{
    GtkWidget * d;
    GtkWidget * n;
    gboolean * alive;

    alive = g_new( gboolean, 1 );
    *alive = TRUE;

    d = gtk_dialog_new_with_buttons( _( "Transmission Preferences" ), parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    gtk_window_set_role( GTK_WINDOW(d), "transmission-preferences-dialog" );
    gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( d ), GUI_PAD );
    g_object_weak_ref( G_OBJECT( d ), dialogDestroyed, alive );

    n = gtk_notebook_new( );
    gtk_container_set_border_width ( GTK_CONTAINER ( n ), GUI_PAD );

    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              torrentPage( core ),
                              gtk_label_new (_("Torrents")) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              peerPage( core, alive ),
                              gtk_label_new (_("Peers")) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              networkPage( core ),
                              gtk_label_new (_("Network")) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              remotePage( core ),
                              gtk_label_new (_("Remote")) );

    g_signal_connect( d, "response", G_CALLBACK(response_cb), core );
    gtk_box_pack_start_defaults( GTK_BOX(GTK_DIALOG(d)->vbox), n );
    gtk_widget_show_all( GTK_DIALOG(d)->vbox );
    return d;
}
