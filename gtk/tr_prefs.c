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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <third-party/miniupnp/miniwget.h>
#include <libtransmission/transmission.h>
#include "conf.h"
#include "hig.h"
#include "tr_core.h"
#include "tr_prefs.h"
#include "util.h"

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
void
tr_prefs_init_global( void )
{
    cf_check_older_configs( );

    pref_int_set_default    ( PREF_KEY_MAX_PEERS_GLOBAL, 200 );
    pref_int_set_default    ( PREF_KEY_MAX_PEERS_PER_TORRENT, 50 );

    pref_flag_set_default   ( PREF_KEY_TOOLBAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_FILTER_BAR, TRUE );
    pref_flag_set_default   ( PREF_KEY_STATUS_BAR, TRUE );
    pref_string_set_default ( PREF_KEY_STATUS_BAR_STATS, "total-ratio" );

    pref_flag_set_default   ( PREF_KEY_DL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_DL_LIMIT, 100 );
    pref_flag_set_default   ( PREF_KEY_UL_LIMIT_ENABLED, FALSE );
    pref_int_set_default    ( PREF_KEY_UL_LIMIT, 50 );

    pref_flag_set_default   ( PREF_KEY_DIR_ASK, FALSE );
    pref_string_set_default ( PREF_KEY_DIR_DEFAULT, g_get_home_dir() );

    pref_int_set_default    ( PREF_KEY_PORT, TR_DEFAULT_PORT );

    pref_flag_set_default   ( PREF_KEY_NAT, TRUE );
    pref_flag_set_default   ( PREF_KEY_PEX, TRUE );
    pref_flag_set_default   ( PREF_KEY_SYSTRAY, TRUE );
    pref_flag_set_default   ( PREF_KEY_ASKQUIT, TRUE );
    pref_flag_set_default   ( PREF_KEY_ENCRYPTED_ONLY, FALSE );

    pref_string_set_default ( PREF_KEY_ADDSTD, toractionname(TR_TOR_COPY) );
    pref_string_set_default ( PREF_KEY_ADDIPC, toractionname(TR_TOR_COPY) );

    pref_int_set_default    ( PREF_KEY_MSGLEVEL, TR_MSG_INF );

    pref_string_set_default ( PREF_KEY_SORT_MODE, "sort-by-name" );
    pref_flag_set_default   ( PREF_KEY_SORT_REVERSED, FALSE );
    pref_flag_set_default   ( PREF_KEY_MINIMAL_VIEW, FALSE );

    pref_save( NULL );
}

/**
***
**/

int
tr_prefs_get_action( const char * key )
{
    char * val = pref_string_get( key );
    const int ret = toraddaction( val );
    g_free( val );
    return ret;
}

void
tr_prefs_set_action( const char * key, int action )
{
    pref_string_set( key, toractionname(action) );
}

/**
***
**/

#define PREFS_KEY "prefs-key"

static void
response_cb( GtkDialog * dialog, int response UNUSED, gpointer unused UNUSED )
{
    gtk_widget_destroy( GTK_WIDGET(dialog) );
}

static void
toggled_cb( GtkToggleButton * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREFS_KEY );
    const gboolean flag = gtk_toggle_button_get_active( w );
    tr_core_set_pref_bool( TR_CORE(core), key, flag );
}

static GtkWidget*
new_check_button( const char * mnemonic, const char * key, gpointer core )
{
    GtkWidget * w = gtk_check_button_new_with_mnemonic( mnemonic );
    g_object_set_data_full( G_OBJECT(w), PREFS_KEY, g_strdup(key), g_free );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(w), pref_flag_get(key) );
    g_signal_connect( w, "toggled", G_CALLBACK(toggled_cb), core );
    return w;
}

static void
spun_cb( GtkSpinButton * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREFS_KEY );
    const int value = gtk_spin_button_get_value_as_int( w );
    tr_core_set_pref_int( TR_CORE(core), key, value );
}

static GtkWidget*
new_spin_button( const char * key, gpointer core, int low, int high, int step )
{
    GtkWidget * w = gtk_spin_button_new_with_range( low, high, step );
    g_object_set_data_full( G_OBJECT(w), PREFS_KEY, g_strdup(key), g_free );
    gtk_spin_button_set_digits( GTK_SPIN_BUTTON(w), 0 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), pref_int_get(key) );
    g_signal_connect( w, "value-changed", G_CALLBACK(spun_cb), core );
    return w;
}

static void
chosen_cb( GtkFileChooser * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREFS_KEY );
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
    g_object_set_data_full( G_OBJECT(w), PREFS_KEY, g_strdup(key), g_free );
    g_signal_connect( w, "selection-changed", G_CALLBACK(chosen_cb), core );
    gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER(w), path );
    return w;
}

static void
action_cb( GtkComboBox * w, gpointer core )
{
    const char * key = g_object_get_data( G_OBJECT(w), PREFS_KEY );
    GtkTreeIter iter;
    if( gtk_combo_box_get_active_iter( GTK_COMBO_BOX(w), &iter ) )
    {
        int action;
        GtkTreeModel * model = gtk_combo_box_get_model( GTK_COMBO_BOX(w) );
        gtk_tree_model_get( model, &iter, 1, &action, -1 );
        tr_core_set_pref( core, key, toractionname(action) );
    }
}

static GtkWidget*
new_action_combo( const char * key, gpointer core )
{
    const char * s;
    GtkTreeIter iter;
    GtkCellRenderer * rend;
    GtkListStore * model;
    GtkWidget * w;

    model = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );

    s = _("Use the torrent file where it is");
    gtk_list_store_append( model, &iter );
    gtk_list_store_set( model, &iter, 1, TR_TOR_LEAVE, 0, s, -1 );

    s = _("Keep a copy of the torrent file");
    gtk_list_store_append( model, &iter );
    gtk_list_store_set( model, &iter, 1, TR_TOR_COPY, 0, s, -1 );

    s = _("Keep a copy and remove the original");
    gtk_list_store_append( model, &iter );
    gtk_list_store_set( model, &iter, 1, TR_TOR_MOVE, 0, s, -1 );

    w = gtk_combo_box_new_with_model( GTK_TREE_MODEL(model) ); 
    gtk_combo_box_set_active( GTK_COMBO_BOX(w), tr_prefs_get_action(key) );
    g_object_set_data_full( G_OBJECT(w), PREFS_KEY, g_strdup(key), g_free );
    rend = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(w), rend, TRUE );
    gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(w), rend, "text", 0 );
    g_signal_connect( w, "changed", G_CALLBACK(action_cb), core );

    return w;
}

static void
target_cb( GtkWidget * widget, gpointer target )
{
    gtk_widget_set_sensitive( GTK_WIDGET(target), gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) ) );
}

static void
target_invert_cb( GtkWidget * widget, gpointer target )
{
    gtk_widget_set_sensitive( GTK_WIDGET(target), !gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget) ) );
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
        GtkSpinButton * spin = GTK_SPIN_BUTTON( g_object_get_data( o, "tr-port-spin" ) );
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

GtkWidget *
tr_prefs_dialog_new( GObject * core, GtkWindow * parent )
{
    int row = 0;
    const char * s;
    GtkWidget * t;
    GtkWidget * w, * w2;
    GtkWidget * l;
    GtkWidget * h;
    GtkWidget * d;
    GtkTooltips * tips;
    gboolean * alive;

    alive = g_new( gboolean, 1 );
    *alive = TRUE;

    tips = gtk_tooltips_new( );

    d = gtk_dialog_new_with_buttons( _("Preferences"), parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    gtk_window_set_role( GTK_WINDOW(d), "transmission-preferences-dialog" );
    gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( d ), GUI_PAD );
    g_object_weak_ref( G_OBJECT( d ), dialogDestroyed, alive );

    g_signal_connect( d, "response", G_CALLBACK(response_cb), core );

    t = hig_workarea_create ();

    hig_workarea_add_section_title (t, &row, _("Speed Limits"));
    hig_workarea_add_section_spacer (t, row, 2);

        s = _("_Limit upload speed (KiB/s)");
        w = new_check_button( s, PREF_KEY_UL_LIMIT_ENABLED, core );
        w2 = new_spin_button( PREF_KEY_UL_LIMIT, core, 0, INT_MAX, 5 );
        gtk_widget_set_sensitive( GTK_WIDGET(w2), pref_flag_get( PREF_KEY_UL_LIMIT_ENABLED ) );
        g_signal_connect( w, "toggled", G_CALLBACK(target_cb), w2 );
        hig_workarea_add_double_control( t, &row, w, w2 );

        s = _("Li_mit download speed (KiB/s)");
        w = new_check_button( s, PREF_KEY_DL_LIMIT_ENABLED, core );
        w2 = new_spin_button( PREF_KEY_DL_LIMIT, core, 0, INT_MAX, 5 );
        gtk_widget_set_sensitive( GTK_WIDGET(w2), pref_flag_get( PREF_KEY_DL_LIMIT_ENABLED ) );
        g_signal_connect( w, "toggled", G_CALLBACK(target_cb), w2 );
        hig_workarea_add_double_control( t, &row, w, w2 );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title (t, &row, _("Downloads"));
    hig_workarea_add_section_spacer (t, row, 3);

        s = _("P_rompt for download directory");
        w = new_check_button( s, PREF_KEY_DIR_ASK, core );
        w2 = new_path_chooser_button( PREF_KEY_DIR_DEFAULT, core );
        gtk_widget_set_sensitive( GTK_WIDGET(w2), !pref_flag_get( PREF_KEY_DIR_ASK ) );
        g_signal_connect( w, "toggled", G_CALLBACK(target_invert_cb), w2 );
        hig_workarea_add_double_control( t, &row, w, w2 );

        w = new_action_combo( PREF_KEY_ADDSTD, core );
        s = _("For torrents added _normally:");
        l = hig_workarea_add_row( t, &row, s, w, NULL );

        w = new_action_combo( PREF_KEY_ADDIPC, core );
        s = _("For torrents added from _command-line:");
        l = hig_workarea_add_row( t, &row, s, w, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Peer Connections" ) );
    hig_workarea_add_section_spacer(t , row, 2 );
  
        w = new_spin_button( PREF_KEY_MAX_PEERS_GLOBAL, core, 1, 3000, 5 );
        hig_workarea_add_row( t, &row, _( "Global maximum connected peers:" ), w, NULL );
        w = new_spin_button( PREF_KEY_MAX_PEERS_PER_TORRENT, core, 1, 300, 5 );
        hig_workarea_add_row( t, &row, _( "Maximum connected peers for new torrents:" ), w, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title (t, &row, _("Network"));
    hig_workarea_add_section_spacer (t, row, 2);
        
        s = _("_Automatically map port" );
        w = new_check_button( s, PREF_KEY_NAT, core );
        hig_workarea_add_wide_control( t, &row, w );
        gtk_tooltips_set_tip( GTK_TOOLTIPS( tips ), w, _( "NAT traversal uses either NAT-PMP or UPnP" ), NULL );

        h = gtk_hbox_new( FALSE, GUI_PAD );
        w2 = new_spin_button( PREF_KEY_PORT, core, 1, INT_MAX, 1 );
        gtk_box_pack_start( GTK_BOX(h), w2, FALSE, FALSE, 0 );
        l = gtk_label_new( NULL );
        gtk_misc_set_alignment( GTK_MISC(l), 0.0f, 0.5f );
        gtk_box_pack_start( GTK_BOX(h), l, FALSE, FALSE, 0 );
        hig_workarea_add_row( t, &row, _("Incoming TCP _Port"), h, w );

        g_object_set_data( G_OBJECT(l), "tr-port-spin", w2 );
        g_object_set_data( G_OBJECT(l), "alive", alive );
        testing_port_cb( NULL, l );

        g_signal_connect( w, "toggled", G_CALLBACK(toggled_cb), l );
        g_signal_connect( w2, "value-changed", G_CALLBACK(testing_port_cb), l );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title (t, &row, _("Options"));
    hig_workarea_add_section_spacer (t, row, 3);
        
        s = _("Use peer _exchange if possible");
        w = new_check_button( s, PREF_KEY_PEX, core );
        hig_workarea_add_wide_control( t, &row, w );
        
        s = _("_Ignore unencrypted peers");
        w = new_check_button( s, PREF_KEY_ENCRYPTED_ONLY, core );
        hig_workarea_add_wide_control( t, &row, w );
        
        s = _("Show an icon in the system _tray");
        w = new_check_button( s, PREF_KEY_SYSTRAY, core );
        hig_workarea_add_wide_control( t, &row, w );
        
        s = _("Confirm _quit");
        w = new_check_button( s, PREF_KEY_ASKQUIT, core );
        hig_workarea_add_wide_control( t, &row, w );

    hig_workarea_finish (t, &row);
    gtk_box_pack_start_defaults( GTK_BOX(GTK_DIALOG(d)->vbox), t );
    gtk_widget_show_all( GTK_DIALOG(d)->vbox );
    return d;
}
