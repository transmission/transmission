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

#include <stdlib.h> /* free() */
#include <unistd.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <third-party/miniupnp/miniwget.h>
#include <libtransmission/transmission.h>
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
    const char * str;

    cf_check_older_configs( );

#if HAVE_GIO
    str = NULL;
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DESKTOP );
    if( !str ) str = g_get_home_dir( );
    pref_string_set_default ( PREF_KEY_DIR_WATCH, str );
    pref_flag_set_default   ( PREF_KEY_DIR_WATCH_ENABLED, FALSE );
#endif

    pref_flag_set_default   ( PREF_KEY_BLOCKLIST_ENABLED, FALSE );

    pref_string_set_default ( PREF_KEY_OPEN_DIALOG_FOLDER, g_get_home_dir( ) );

    pref_int_set_default    ( PREF_KEY_MAX_PEERS_GLOBAL, 200 );
    pref_int_set_default    ( PREF_KEY_MAX_PEERS_PER_TORRENT, 50 );

    pref_flag_set_default   ( PREF_KEY_TOOLBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_FILTERBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_STATUSBAR, TRUE );
    pref_string_set_default ( PREF_KEY_STATUSBAR_STATS, "total-ratio" );

    pref_flag_set_default   ( PREF_KEY_DL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_DL_LIMIT, 100 );
    pref_flag_set_default   ( PREF_KEY_UL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_UL_LIMIT, 50 );
    pref_flag_set_default   ( PREF_KEY_OPTIONS_PROMPT, TRUE );

    str = NULL;
#if GLIB_CHECK_VERSION(2,14,0)
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DOWNLOAD );
#endif
    if( !str ) str = g_get_home_dir( );
    pref_string_set_default ( PREF_KEY_DIR_DEFAULT, str );

    pref_int_set_default    ( PREF_KEY_PORT, TR_DEFAULT_PORT );

    pref_flag_set_default   ( PREF_KEY_NOTIFY, TRUE );

    pref_flag_set_default   ( PREF_KEY_NAT, TRUE );
    pref_flag_set_default   ( PREF_KEY_PEX, TRUE );
    pref_flag_set_default   ( PREF_KEY_ASKQUIT, TRUE );
    pref_flag_set_default   ( PREF_KEY_ENCRYPTED_ONLY, FALSE );

    pref_int_set_default    ( PREF_KEY_MSGLEVEL, TR_MSG_INF );

    pref_string_set_default ( PREF_KEY_SORT_MODE, "sort-by-name" );
    pref_flag_set_default   ( PREF_KEY_SORT_REVERSED, FALSE );
    pref_flag_set_default   ( PREF_KEY_MINIMAL_VIEW, FALSE );

    pref_flag_set_default   ( PREF_KEY_START, TRUE );
    pref_flag_set_default   ( PREF_KEY_TRASH_ORIGINAL, FALSE );

    pref_save( NULL );
}

/**
***
**/

#define PREF_KEY "pref-key"

static void
response_cb( GtkDialog * dialog, int response UNUSED, gpointer unused UNUSED )
{
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

static gpointer
test_port( gpointer data_gpointer )
{
    struct test_port_data * data = data_gpointer;

    if( *data->alive )
    {
        GObject * o = G_OBJECT( data->label );
        GtkSpinButton * spin = g_object_get_data( o, "tr-port-spin" );
        const int port = gtk_spin_button_get_value_as_int( spin );
        int isOpen;
        int size;
        char * text;
        char url[256];

        g_usleep( G_USEC_PER_SEC * 3 ); /* give portmapping time to kick in */
        snprintf( url, sizeof(url), "http://portcheck.transmissionbt.com/%d", port );
        text = miniwget( url, &size );
        /*g_message(" got len %d, [%*.*s]", size, size, size, text );*/
        isOpen = text && *text=='1';
        free( text );

        if( *data->alive )
            gtk_label_set_markup( GTK_LABEL(data->label), isOpen
                ? _("Port is <b>open</b>")
                : _("Port is <b>closed</b>") );
    }

    g_free( data );
    return NULL;
}

static void
testing_port_cb( GtkWidget * unused UNUSED, gpointer l )
{
    struct test_port_data * data = g_new0( struct test_port_data, 1 );
    data->alive = g_object_get_data( G_OBJECT( l ), "alive" );
    data->label = l;
    gtk_label_set_markup( GTK_LABEL(l), _( "<i>Testing port...</i>" ) );
    g_thread_create( test_port, data, FALSE, NULL );
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
    hig_workarea_add_section_title( t, &row, _( "Opening Torrents" ) );

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

        s = _( "_Start when opened" );
        w = new_check_button( s, PREF_KEY_START, core );
        hig_workarea_add_wide_control( t, &row, w );

        s = _( "Mo_ve source files to Trash" );
        w = new_check_button( s, PREF_KEY_TRASH_ORIGINAL, core ); 
        hig_workarea_add_wide_control( t, &row, w );

        w = new_path_chooser_button( PREF_KEY_DIR_DEFAULT, core );
        hig_workarea_add_row( t, &row, _( "_Destination folder:" ), w, NULL );

#ifdef HAVE_LIBNOTIFY
    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Notification" ) );

        s = _( "_Display a message when torrents finish" );
        w = new_check_button( s, PREF_KEY_NOTIFY, core );
        hig_workarea_add_wide_control( t, &row, w );
#endif

    hig_workarea_finish( t, &row );
    return t;
}

/***
****
***/

struct blocklist_data
{
    GtkWidget * dialog;
    TrCore * core;
    int abortFlag;
    char secondary[512];
};

static gboolean
blocklistDialogSetSecondary( gpointer vdata )
{
    struct blocklist_data * data = vdata;
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

static gpointer
updateBlocklist( gpointer vdata )
{
    struct blocklist_data * data = vdata;
    int size = 0;
    int rules = 0;
    const char * url;
    char * text = NULL;
    gchar * filename = NULL;
    GError * err = NULL;
    int fd = -1;
    int ok = 1;

    url = "http://download.m0k.org/transmission/files/level1.gz";

    if( ok && !data->abortFlag )
    {
        g_snprintf( data->secondary, sizeof( data->secondary ),
                    _( "Retrieving blocklist..." ) );
        g_idle_add( blocklistDialogSetSecondary, data );
        text = miniwget( url, &size );
        if( !data->abortFlag && ( !text || !size ) ) {
            ok = FALSE;
            g_snprintf( data->secondary, sizeof( data->secondary ),
                        _( "Unable to get blocklist." ) );
            g_idle_add( blocklistDialogSetSecondary, data );
        }      
    }

    if( ok && !data->abortFlag )
    {
        fd = g_file_open_tmp( "transmission-blockfile-XXXXXX.gz", &filename, &err );
        if( err ) {
            g_snprintf( data->secondary, sizeof( data->secondary ),
                        _( "Unable to get blocklist: %s" ), err->message );
            g_idle_add( blocklistDialogSetSecondary, data );
            g_clear_error( &err );
        } else {
            write( fd, text, size );
            close( fd );
        }
    }
    if( ok && !data->abortFlag )
    {
        g_snprintf( data->secondary, sizeof( data->secondary ),
                    _( "Uncompressing blocklist..." ) );
        g_idle_add( blocklistDialogSetSecondary, data );
        char * cmd = g_strdup_printf( "gunzip %s", filename );
        system( cmd );
        g_free( cmd );
    }
    if( ok && !data->abortFlag )
    {
        g_snprintf( data->secondary, sizeof( data->secondary ),
                    _( "Parsing blocklist..." ) );
        g_idle_add( blocklistDialogSetSecondary, data );
        filename[ strlen(filename) - 3 ] = '\0';
        rules = tr_blocklistSetContent( tr_core_handle( data->core ), filename );
    }
    if( ok && !data->abortFlag )
    {
        g_snprintf( data->secondary, sizeof( data->secondary ),
                    _( "Blocklist now has %'d rules" ), rules );
        g_idle_add( blocklistDialogSetSecondary, data );
        g_idle_add( blocklistDialogAllowClose, data->dialog );
    }

    free( text );
    /* g_free( data ); */
    unlink( filename );
    return NULL;
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
onUpdateBlocklistCB( GtkButton * w, gpointer core )
{
    GtkWidget * d;
    struct blocklist_data * data;
    
    d = gtk_message_dialog_new( GTK_WINDOW( gtk_widget_get_toplevel( GTK_WIDGET( w ) ) ),
                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                GTK_MESSAGE_INFO,
                                GTK_BUTTONS_NONE,
                                _( "Updating Blocklist" ) );
    gtk_dialog_add_buttons( GTK_DIALOG( d ),
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                            NULL );
    gtk_dialog_set_response_sensitive( GTK_DIALOG( d ), GTK_RESPONSE_CLOSE, FALSE );

    data = g_new0( struct blocklist_data, 1 );
    data->dialog = d;
    data->core = core;

    g_signal_connect( d, "response", G_CALLBACK(onUpdateBlocklistResponseCB), data );
    gtk_widget_show( d );
    g_thread_create( updateBlocklist, data, FALSE, NULL );
}

static GtkWidget*
peerPage( GObject * core )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w;
    GtkWidget * b;
    GtkWidget * h;

    t = hig_workarea_create( );
    hig_workarea_add_section_title (t, &row, _("Options"));
        
        s = _("Use peer e_xchange");
        w = new_check_button( s, PREF_KEY_PEX, core );
        hig_workarea_add_wide_control( t, &row, w );
        
        s = _("_Ignore unencrypted peers");
        w = new_check_button( s, PREF_KEY_ENCRYPTED_ONLY, core );
        hig_workarea_add_wide_control( t, &row, w );

    hig_workarea_add_section_divider( t, &row );
    /* section header for the "maximum number of peers" section */
    hig_workarea_add_section_title( t, &row, _( "Limits" ) );
  
        w = new_spin_button( PREF_KEY_MAX_PEERS_GLOBAL, core, 1, 3000, 5 );
        hig_workarea_add_row( t, &row, _( "Maximum peers _overall:" ), w, NULL );
        w = new_spin_button( PREF_KEY_MAX_PEERS_PER_TORRENT, core, 1, 300, 5 );
        hig_workarea_add_row( t, &row, _( "Maximum peers per _torrent:" ), w, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Blocklist" ) );

        s = _("Use IP _blocklist from bluetack.co.uk" );
        w = new_check_button( s, PREF_KEY_BLOCKLIST_ENABLED, core );
        hig_workarea_add_wide_control( t, &row, w );

        b = gtk_button_new_with_mnemonic( _( "_Update Blocklist" ) );
        g_signal_connect( b, "clicked", G_CALLBACK(onUpdateBlocklistCB), core );
        gtk_widget_set_sensitive( GTK_WIDGET(b), pref_flag_get( PREF_KEY_BLOCKLIST_ENABLED ) );
        g_signal_connect( w, "toggled", G_CALLBACK(target_cb), b );
        h = gtk_hbox_new( FALSE, GUI_PAD_BIG );
        gtk_box_pack_start( GTK_BOX(h), b, FALSE, FALSE, 0 );
        hig_workarea_add_wide_control( t, &row, h );

    hig_workarea_finish( t, &row );
    return t;
}

static GtkWidget*
networkPage( GObject * core, gpointer alive )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w, * w2;
    GtkWidget * l;
    GtkWidget * h;

    t = hig_workarea_create( );

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

    hig_workarea_add_section_title (t, &row, _("Ports"));
        
        s = _("_Forward port from router" );
        w = new_check_button( s, PREF_KEY_NAT, core );
        hig_workarea_add_wide_control( t, &row, w );

        h = gtk_hbox_new( FALSE, GUI_PAD_BIG );
        w2 = new_spin_button( PREF_KEY_PORT, core, 1, INT_MAX, 1 );
        gtk_box_pack_start( GTK_BOX(h), w2, FALSE, FALSE, 0 );
        l = gtk_label_new( NULL );
        gtk_misc_set_alignment( GTK_MISC(l), 0.0f, 0.5f );
        gtk_box_pack_start( GTK_BOX(h), l, FALSE, FALSE, 0 );
        hig_workarea_add_row( t, &row, _("Incoming _port:"), h, w );

        g_object_set_data( G_OBJECT(l), "tr-port-spin", w2 );
        g_object_set_data( G_OBJECT(l), "alive", alive );
        testing_port_cb( NULL, l );

        g_signal_connect( w, "toggled", G_CALLBACK(testing_port_cb), l );
        g_signal_connect( w2, "value-changed", G_CALLBACK(testing_port_cb), l );

    hig_workarea_finish( t, &row );
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

    d = gtk_dialog_new_with_buttons( _("Preferences"), parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    gtk_window_set_role( GTK_WINDOW(d), "transmission-preferences-dialog" );
    gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( d ), GUI_PAD );
    g_object_weak_ref( G_OBJECT( d ), dialogDestroyed, alive );

    n = gtk_notebook_new( );

    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              torrentPage( core ),
                              gtk_label_new (_("Torrents")) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              peerPage( core ),
                              gtk_label_new (_("Peers")) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),
                              networkPage( core, alive ),
                              gtk_label_new (_("Network")) );

    g_signal_connect( d, "response", G_CALLBACK(response_cb), core );
    gtk_box_pack_start_defaults( GTK_BOX(GTK_DIALOG(d)->vbox), n );
    gtk_widget_show_all( GTK_DIALOG(d)->vbox );
    return d;
}
