/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <math.h> /* ceil() */
#include <stddef.h>
#include <stdio.h> /* sscanf */
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_free */

#include "actions.h"
#include "details.h"
#include "file-list.h"
#include "tracker-list.h"
#include "hig.h"
#include "util.h"

#define DETAILS_KEY "details-data"

#define UPDATE_INTERVAL_SECONDS 2

struct DetailsImpl
{
    GtkWidget * peersPage;
    GtkWidget * trackerPage;
    GtkWidget * activityPage;

    GtkWidget * honorLimitsCheck;
    GtkWidget * upLimitedCheck;
    GtkWidget * upLimitSpin;
    GtkWidget * downLimitedCheck;
    GtkWidget * downLimitSpin;
    GtkWidget * bandwidthCombo;
    GtkWidget * seedGlobalRadio;
    GtkWidget * seedForeverRadio;
    GtkWidget * seedCustomRadio;
    GtkWidget * seedCustomSpin;
    GtkWidget * maxPeersSpin;

    guint honorLimitsCheckTag;
    guint upLimitedCheckTag;
    guint downLimitedCheckTag;
    guint downLimitSpinTag;
    guint upLimitSpinTag;
    guint bandwidthComboTag;
    guint seedForeverRadioTag;
    guint seedGlobalRadioTag;
    guint seedCustomRadioTag;
    guint seedCustomSpinTag;

    GtkWidget * state_lb;
    GtkWidget * progress_lb;
    GtkWidget * have_lb;
    GtkWidget * dl_lb;
    GtkWidget * ul_lb;
    GtkWidget * failed_lb;
    GtkWidget * ratio_lb;
    GtkWidget * error_lb;
    GtkWidget * swarm_lb;
    GtkWidget * date_added_lb;
    GtkWidget * last_activity_lb;

    GtkWidget * pieces_lb;
    GtkWidget * hash_lb;
    GtkWidget * privacy_lb;
    GtkWidget * creator_lb;
    GtkWidget * date_created_lb;
    GtkWidget * destination_lb;
    GtkWidget * torrentfile_lb;
    GtkTextBuffer * comment_buffer;

    GHashTable * peer_hash;
    GHashTable * webseed_hash;
    GtkListStore * peer_store;
    GtkListStore * webseed_store;
    GtkWidget * seeders_lb;
    GtkWidget * leechers_lb;
    GtkWidget * completed_lb;
    GtkWidget * webseed_view;

    GtkWidget * tracker_list;
    GtkWidget * last_scrape_time_lb;
    GtkWidget * last_scrape_response_lb;
    GtkWidget * next_scrape_countdown_lb;
    GtkWidget * last_announce_time_lb;
    GtkWidget * last_announce_response_lb;
    GtkWidget * next_announce_countdown_lb;
    GtkWidget * manual_announce_countdown_lb;

    GtkWidget * file_list;

    GSList * ids;
    TrCore * core;
    guint periodic_refresh_tag;
    guint prefs_changed_tag;
};

static tr_torrent**
getTorrents( struct DetailsImpl * d, int * setmeCount )
{
    int n = g_slist_length( d->ids );
    int torrentCount = 0;
    tr_session * session = tr_core_session( d->core );
    tr_torrent ** torrents = g_new( tr_torrent*, n );
    GSList * l;

    for( l=d->ids; l!=NULL; l=l->next ) {
        const int id = GPOINTER_TO_INT( l->data );
        tr_torrent * tor = tr_torrentFindFromId( session, id );
        if( tor )
            torrents[torrentCount++] = tor;
    }

    *setmeCount = torrentCount;
    return torrents;
}

/****
*****
*****  OPTIONS TAB
*****
****/

static void
set_togglebutton_if_different( GtkWidget * w, guint tag, gboolean value )
{
    GtkToggleButton * toggle = GTK_TOGGLE_BUTTON( w );
    const gboolean currentValue = gtk_toggle_button_get_active( toggle );
    if( currentValue != value )
    {
        g_signal_handler_block( toggle, tag );
        gtk_toggle_button_set_active( toggle, value );
        g_signal_handler_unblock( toggle, tag );
    }
}

static void
set_int_spin_if_different( GtkWidget * w, guint tag, int value )
{
    GtkSpinButton * spin = GTK_SPIN_BUTTON( w );
    const int currentValue = gtk_spin_button_get_value_as_int( spin );
    if( currentValue != value )
    {
        g_signal_handler_block( spin, tag );
        gtk_spin_button_set_value( spin, value );
        g_signal_handler_unblock( spin, tag );
    }
}

static void
set_double_spin_if_different( GtkWidget * w, guint tag, double value )
{
    GtkSpinButton * spin = GTK_SPIN_BUTTON( w );
    const double currentValue = gtk_spin_button_get_value( spin );
    if( ( (int)(currentValue*100) != (int)(value*100) ) )
    {
        g_signal_handler_block( spin, tag );
        gtk_spin_button_set_value( spin, value );
        g_signal_handler_unblock( spin, tag );
    }
}

static void
set_int_combo_if_different( GtkWidget * w, guint tag, int column, int value )
{
    int i;
    int currentValue;
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
            g_signal_handler_block( combobox, tag );
            gtk_combo_box_set_active_iter( combobox, &iter );
            g_signal_handler_unblock( combobox, tag );
            return;
        }
    }
}

static void
unset_combo( GtkWidget * w, guint tag )
{
    GtkComboBox * combobox = GTK_COMBO_BOX( w );

    g_signal_handler_block( combobox, tag );
    gtk_combo_box_set_active( combobox, -1 );
    g_signal_handler_unblock( combobox, tag );
}

static void
refreshOptions( struct DetailsImpl * di, tr_torrent ** torrents, int n )
{
    /***
    ****  Options Page
    ***/

    /* honorLimitsCheck */
    if( n ) {
        const tr_bool baseline = tr_torrentUsesSessionLimits( torrents[0] );
        int i;
        for( i=1; i<n; ++i )
            if( baseline != tr_torrentUsesSessionLimits( torrents[i] ) )
                break;
        if( i == n )
            set_togglebutton_if_different( di->honorLimitsCheck,
                                           di->honorLimitsCheckTag, baseline );
    }
    
    /* downLimitedCheck */
    if( n ) {
        const tr_bool baseline = tr_torrentUsesSpeedLimit( torrents[0], TR_DOWN );
        int i;
        for( i=1; i<n; ++i )
            if( baseline != tr_torrentUsesSpeedLimit( torrents[i], TR_DOWN ) )
                break;
        if( i == n )
            set_togglebutton_if_different( di->downLimitedCheck,
                                           di->downLimitedCheckTag, baseline );
    }

    /* downLimitSpin */
    if( n ) {
        const int baseline = tr_torrentGetSpeedLimit( torrents[0], TR_DOWN );
        int i;
        for( i=1; i<n; ++i )
            if( baseline != tr_torrentGetSpeedLimit( torrents[i], TR_DOWN ) )
                break;
        if( i == n )
            set_int_spin_if_different( di->downLimitSpin,
                                       di->downLimitSpinTag, baseline );
    }
    
    /* upLimitedCheck */
    if( n ) {
        const tr_bool baseline = tr_torrentUsesSpeedLimit( torrents[0], TR_UP );
        int i;
        for( i=1; i<n; ++i )
            if( baseline != tr_torrentUsesSpeedLimit( torrents[i], TR_UP ) )
                break;
        if( i == n )
            set_togglebutton_if_different( di->upLimitedCheck,
                                           di->upLimitedCheckTag, baseline );
    }

    /* upLimitSpin */
    if( n ) {
        const int baseline = tr_torrentGetSpeedLimit( torrents[0], TR_UP );
        int i;
        for( i=1; i<n; ++i )
            if( baseline != tr_torrentGetSpeedLimit( torrents[i], TR_UP ) )
                break;
        if( i == n )
            set_int_spin_if_different( di->upLimitSpin,
                                       di->upLimitSpinTag, baseline );
    }

    /* bandwidthCombo */
    if( n ) {
        const int baseline = tr_torrentGetPriority( torrents[0] );
        int i;
        for( i=1; i<n; ++i )
            if( baseline != tr_torrentGetPriority( torrents[i] ) )
                break;
        if( i == n )
            set_int_combo_if_different( di->bandwidthCombo,
                                        di->bandwidthComboTag, 0, baseline );
        else
            unset_combo( di->bandwidthCombo, di->bandwidthComboTag );
    }

    /* seedGlobalRadio */
    /* seedForeverRadio */
    /* seedCustomRadio */
    if( n ) {
        guint t;
        const int baseline = tr_torrentGetRatioMode( torrents[0] );
        int i;
        for( i=1; i<n; ++i )
            if( baseline != (int)tr_torrentGetRatioMode( torrents[i] ) )
                break;
        if( i == n ) {
            GtkWidget * w;
            switch( baseline ) {
                case TR_RATIOLIMIT_SINGLE: w = di->seedCustomRadio;
                                           t = di->seedCustomRadioTag; break;
                case TR_RATIOLIMIT_GLOBAL: w = di->seedGlobalRadio;
                                           t = di->seedGlobalRadioTag; break;
                case TR_RATIOLIMIT_UNLIMITED: w = di->seedForeverRadio;
                                              t = di->seedForeverRadioTag; break;
            }
            set_togglebutton_if_different( w, t, TRUE );
        }
    }

    /* seedCustomSpin */
    if( n ) {
        const double baseline = tr_torrentGetRatioLimit( torrents[0] );
        int i;
        for( i=1; i<n; ++i )
            if( (int)(100*baseline) != (int)(100*tr_torrentGetRatioLimit(torrents[i])) )
                break;
        if( i == n )
            set_double_spin_if_different( di->seedCustomSpin,
                                          di->seedCustomSpinTag, baseline );
    }
}

static void
torrent_set_bool( struct DetailsImpl * di, const char * key, gboolean value )
{
    GSList *l;
    tr_benc top, *args, *ids;

    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    args = tr_bencDictAddDict( &top, "arguments", 2 );
    tr_bencDictAddBool( args, key, value );
    ids = tr_bencDictAddList( args, "ids", g_slist_length(di->ids) );
    for( l=di->ids; l; l=l->next )
        tr_bencListAddInt( ids, GPOINTER_TO_INT( l->data ) );

    tr_core_exec( di->core, &top );
    tr_bencFree( &top );
}

static void
torrent_set_int( struct DetailsImpl * di, const char * key, int value )
{
    GSList *l;
    tr_benc top, *args, *ids;

    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    args = tr_bencDictAddDict( &top, "arguments", 2 );
    tr_bencDictAddInt( args, key, value );
    ids = tr_bencDictAddList( args, "ids", g_slist_length(di->ids) );
    for( l=di->ids; l; l=l->next )
        tr_bencListAddInt( ids, GPOINTER_TO_INT( l->data ) );

    tr_core_exec( di->core, &top );
    tr_bencFree( &top );
}

static void
torrent_set_real( struct DetailsImpl * di, const char * key, double value )
{
    GSList *l;
    tr_benc top, *args, *ids;

    tr_bencInitDict( &top, 2 );
    tr_bencDictAddStr( &top, "method", "torrent-set" );
    args = tr_bencDictAddDict( &top, "arguments", 2 );
    tr_bencDictAddReal( args, key, value );
    ids = tr_bencDictAddList( args, "ids", g_slist_length(di->ids) );
    for( l=di->ids; l; l=l->next )
        tr_bencListAddInt( ids, GPOINTER_TO_INT( l->data ) );

    tr_core_exec( di->core, &top );
    tr_bencFree( &top );
}

static void
up_speed_toggled_cb( GtkToggleButton * tb, gpointer d )
{
    torrent_set_bool( d, "uploadLimited", gtk_toggle_button_get_active( tb ) );
}

static void
down_speed_toggled_cb( GtkToggleButton *tb, gpointer d )
{
    torrent_set_bool( d, "downloadLimited", gtk_toggle_button_get_active( tb ) );
}

static void
global_speed_toggled_cb( GtkToggleButton * tb, gpointer d )
{
    torrent_set_bool( d, "honorsSessionLimits", gtk_toggle_button_get_active( tb ) );
}

#define RATIO_KEY "ratio-mode"

static void
ratio_mode_changed_cb( GtkToggleButton * tb, struct DetailsImpl * d )
{
    if( gtk_toggle_button_get_active( tb ) )
    {
        GObject * o = G_OBJECT( tb );
        const int mode = GPOINTER_TO_INT( g_object_get_data( o, RATIO_KEY ) );
        torrent_set_int( d, "seedRatioMode", mode );
    }
}

static void
up_speed_spun_cb( GtkSpinButton * s, struct DetailsImpl * di )
{
    torrent_set_int( di, "uploadLimit", gtk_spin_button_get_value_as_int( s ) );
}

static void
down_speed_spun_cb( GtkSpinButton * s, struct DetailsImpl * di )
{
    torrent_set_int( di, "downloadLimit", gtk_spin_button_get_value_as_int( s ) );
}

static void
ratio_spun_cb( GtkSpinButton * s, struct DetailsImpl * di )
{
    torrent_set_real( di, "seedRatioLimit", gtk_spin_button_get_value( s ) );
}

static void
max_peers_spun_cb( GtkSpinButton * s, struct DetailsImpl * di )
{
    torrent_set_int( di, "peer-limit", gtk_spin_button_get_value( s ) );
}

static char*
get_global_ratio_radiobutton_string( void )
{
    char * s;
    const gboolean b = pref_flag_get( TR_PREFS_KEY_RATIO_ENABLED );
    const double d = pref_double_get( TR_PREFS_KEY_RATIO );

    if( b )
        s = g_strdup_printf( _( "Use _Global setting  (currently: stop seeding when a torrent's ratio reaches %.2f)" ), d );
    else
        s = g_strdup( _( "Use _Global setting  (currently: seed regardless of ratio)" ) );

    return s;
}

static void
prefsChanged( TrCore * core UNUSED, const char *  key, gpointer rb )
{
    if( !strcmp( key, TR_PREFS_KEY_RATIO_ENABLED ) ||
        !strcmp( key, TR_PREFS_KEY_RATIO ) )
    {
        char * s = get_global_ratio_radiobutton_string( );
        gtk_button_set_label( GTK_BUTTON( rb ), s );
        g_free( s );
    }
}

static void
onPriorityChanged( GtkComboBox * w, struct DetailsImpl * di )
{
    GtkTreeIter iter;

    if( gtk_combo_box_get_active_iter( w, &iter ) )
    {
        int val = 0;
        gtk_tree_model_get( gtk_combo_box_get_model( w ), &iter, 0, &val, -1 );
        torrent_set_int( di, "bandwidthPriority", val );
    }
}

static GtkWidget*
new_priority_combo( struct DetailsImpl * di )
{
    int i;
    guint tag;
    GtkWidget * w;
    GtkCellRenderer * r;
    GtkListStore * store;
    const struct {
        int value;
        const char * text;
    } items[] = {
        { TR_PRI_LOW,    N_( "Low" )  },
        { TR_PRI_NORMAL, N_( "Normal" ) },
        { TR_PRI_HIGH,   N_( "High" )  }
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
    tag = g_signal_connect( w, "changed", G_CALLBACK( onPriorityChanged ), di );
    di->bandwidthComboTag = tag;

    /* cleanup */
    g_object_unref( G_OBJECT( store ) );
    return w;
}


static GtkWidget*
options_page_new( struct DetailsImpl * d )
{
    guint tag;
    int i, row;
    char *s;
    GSList *group;
    GtkWidget *t, *w, *tb, *h;

    row = 0;
    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Speed" ) );

    tb = hig_workarea_add_wide_checkbutton( t, &row, _( "Honor global _limits" ), 0 );
    d->honorLimitsCheck = tb;
    tag = g_signal_connect( tb, "toggled", G_CALLBACK( global_speed_toggled_cb ), d );
    d->honorLimitsCheckTag = tag;

    tb = gtk_check_button_new_with_mnemonic( _( "Limit _download speed (KB/s):" ) );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( tb ), FALSE );
    d->downLimitedCheck = tb;
    tag = g_signal_connect( tb, "toggled", G_CALLBACK( down_speed_toggled_cb ), d );
    d->downLimitedCheckTag = tag;

    w = gtk_spin_button_new_with_range( 1, INT_MAX, 5 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), i );
    tag = g_signal_connect( w, "value-changed", G_CALLBACK( down_speed_spun_cb ), d );
    d->downLimitSpinTag = tag;
    hig_workarea_add_row_w( t, &row, tb, w, NULL );
    d->downLimitSpin = w;

    tb = gtk_check_button_new_with_mnemonic( _( "Limit _upload speed (KB/s):" ) );
    d->upLimitedCheck = tb;
    tag = g_signal_connect( tb, "toggled", G_CALLBACK( up_speed_toggled_cb ), d );
    d->upLimitedCheckTag = tag;

    w = gtk_spin_button_new_with_range( 1, INT_MAX, 5 );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( w ), i );
    tag = g_signal_connect( w, "value-changed", G_CALLBACK( up_speed_spun_cb ), d );
    d->upLimitSpinTag = tag;
    hig_workarea_add_row_w( t, &row, tb, w, NULL );
    d->upLimitSpin = w;

    w = new_priority_combo( d );
    hig_workarea_add_row( t, &row, _( "_Bandwidth priority:" ), w, NULL );
    d->bandwidthCombo = w;

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Seed-Until Ratio" ) );

    group = NULL;
    s = get_global_ratio_radiobutton_string( );
    w = gtk_radio_button_new_with_mnemonic( group, s );
    tag = g_signal_connect( d->core, "prefs-changed", G_CALLBACK( prefsChanged ), w );
    d->prefs_changed_tag = tag;
    group = gtk_radio_button_get_group( GTK_RADIO_BUTTON( w ) );
    hig_workarea_add_wide_control( t, &row, w );
    g_free( s );
    g_object_set_data( G_OBJECT( w ), RATIO_KEY, GINT_TO_POINTER( TR_RATIOLIMIT_GLOBAL ) );
    tag = g_signal_connect( w, "toggled", G_CALLBACK( ratio_mode_changed_cb ), d );
    d->seedGlobalRadio = w;
    d->seedGlobalRadioTag = tag;

    w = gtk_radio_button_new_with_mnemonic( group, _( "Seed _regardless of ratio" ) );
    group = gtk_radio_button_get_group( GTK_RADIO_BUTTON( w ) );
    hig_workarea_add_wide_control( t, &row, w );
    g_object_set_data( G_OBJECT( w ), RATIO_KEY, GINT_TO_POINTER( TR_RATIOLIMIT_UNLIMITED ) );
    tag = g_signal_connect( w, "toggled", G_CALLBACK( ratio_mode_changed_cb ), d );
    d->seedForeverRadio = w;
    d->seedForeverRadioTag = tag;

    h = gtk_hbox_new( FALSE, GUI_PAD );
    w = gtk_radio_button_new_with_mnemonic( group, _( "_Stop seeding when a torrent's ratio reaches" ) );
    d->seedCustomRadio = w;
    g_object_set_data( G_OBJECT( w ), RATIO_KEY, GINT_TO_POINTER( TR_RATIOLIMIT_SINGLE ) );
    tag = g_signal_connect( w, "toggled", G_CALLBACK( ratio_mode_changed_cb ), d );
    d->seedCustomRadioTag = tag;
    group = gtk_radio_button_get_group( GTK_RADIO_BUTTON( w ) );
    gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    w = gtk_spin_button_new_with_range( 0, INT_MAX, .05 );
    gtk_spin_button_set_digits( GTK_SPIN_BUTTON( w ), 2 );
    tag = g_signal_connect( w, "value-changed", G_CALLBACK( ratio_spun_cb ), d );
    gtk_box_pack_start( GTK_BOX( h ), w, FALSE, FALSE, 0 );
    hig_workarea_add_wide_control( t, &row, h );
    d->seedCustomSpin = w;
    d->seedCustomSpinTag = tag;
   
    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Peer Connections" ) );

    w = gtk_spin_button_new_with_range( 1, 3000, 5 );
    hig_workarea_add_row( t, &row, _( "_Maximum peers:" ), w, w );
    g_signal_connect( w, "value-changed", G_CALLBACK( max_peers_spun_cb ), d );
    d->maxPeersSpin = w;

    hig_workarea_finish( t, &row );
    return t;
}

/****
*****
*****  ACTIVITY TAB
*****
****/

static const char * activityString( int activity )
{
    switch( activity )
    {
        case TR_STATUS_CHECK_WAIT: return _( "Waiting to verify local data" ); break;
        case TR_STATUS_CHECK:      return _( "Verifying local data" ); break;
        case TR_STATUS_DOWNLOAD:   return _( "Downloading" ); break;
        case TR_STATUS_SEED:       return _( "Seeding" ); break;
        case TR_STATUS_STOPPED:    return _( "Paused" ); break;
    }

    return "";
}

static void
refreshActivity( struct DetailsImpl * di, tr_torrent ** torrents, int n )
{
    int i;
    const char * str;
    const char * none = _( "None" );
    const char * mixed = _( "Mixed" );
    char buf[512];
    const tr_stat ** stats = g_new( const tr_stat*, n );
    for( i=0; i<n; ++i )
        stats[i] = tr_torrentStatCached( torrents[i] );

    /* state_lb */
    if( n <= 0 )
        str = none;
    else {
        const int baseline = stats[0]->activity;
        for( i=1; i<n; ++i )
            if( baseline != (int)stats[i]->activity )
                break;
        if( i==n )
            str = activityString( baseline );
        else
            str = mixed;
    }
    gtk_label_set_text( GTK_LABEL( di->state_lb ), str );


    /* progress_lb */
    if( n <= 0 )
        str = none;
    else {
        double sizeWhenDone = 0;
        double leftUntilDone = 0;
        for( i=0; i<n; ++i ) {
            sizeWhenDone += stats[i]->sizeWhenDone;
            leftUntilDone += stats[i]->leftUntilDone;
        }
        g_snprintf( buf, sizeof( buf ), _( "%.1f%%" ), 100.0*((sizeWhenDone-leftUntilDone)/sizeWhenDone) );
        str = buf;
    }
    gtk_label_set_text( GTK_LABEL( di->progress_lb ), str );


    /* have_lb */
    if( n <= 0 )
        str = none;
    else {
        char buf1[128];
        char buf2[128];
        double haveUnchecked = 0;
        double haveValid = 0;
        double verifiedPieces = 0;
        for( i=0; i<n; ++i ) {
            const double v = stats[i]->haveValid;
            haveUnchecked += stats[i]->haveUnchecked;
            haveValid += v;
            verifiedPieces += v / tr_torrentInfo(torrents[i])->pieceSize;
        }
        tr_strlsize( buf1, haveValid + haveUnchecked, sizeof( buf1 ) );
        tr_strlsize( buf2, haveValid, sizeof( buf2 ) );
        i = (int) ceil( verifiedPieces );
        g_snprintf( buf, sizeof( buf ), ngettext( "%1$s (%2$s verified in %3$d piece)",
                                                  "%1$s (%2$s verified in %3$d pieces)",
                                                  verifiedPieces ),
                                        buf1, buf2, i );
        str = buf;
    }
    gtk_label_set_text( GTK_LABEL( di->have_lb ), str );

    
    /* dl_lb */
    if( n <= 0 )
        str = none;
    else {
        uint64_t sum = 0;
        for( i=0; i<n; ++i ) sum += stats[i]->downloadedEver;
        str = tr_strlsize( buf, sum, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->dl_lb ), str );

    
    /* ul_lb */
    if( n <= 0 )
        str = none;
    else {
        uint64_t sum = 0;
        for( i=0; i<n; ++i ) sum += stats[i]->uploadedEver;
        str = tr_strlsize( buf, sum, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->ul_lb ), str );


    /* corrupt ever */
    if( n <= 0 )
        str = none;
    else {
        uint64_t sum = 0;
        for( i=0; i<n; ++i ) sum += stats[i]->corruptEver;
        str = tr_strlsize( buf, sum, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->failed_lb ), str );


    /* ratio */
    if( n <= 0 )
        str = none;
    else {
        uint64_t up = 0;
        uint64_t down = 0;
        for( i=0; i<n; ++i ) {
            up += stats[i]->uploadedEver;
            down += stats[i]->downloadedEver;
        }
        str = tr_strlratio( buf, tr_getRatio( up, down ), sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->ratio_lb ), str );


    /* swarmspeed */
    if( n <= 0 )
        str = none;
    else {
        double swarmSpeed = 0;
        for( i=0; i<n; ++i )
            swarmSpeed += stats[i]->swarmSpeed;
        str = tr_strlspeed( buf, swarmSpeed, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->swarm_lb ), str );


    /* error */
    if( n <= 0 )
        str = none;
    else {
        const char * baseline = stats[0]->errorString;
        for( i=1; i<n; ++i )
            if( strcmp( baseline, stats[i]->errorString ) )
                break;
        if( i==n )
            str = baseline;
        else
            str = mixed;
    }
    if( !str || !*str )
        str = none;
    gtk_label_set_text( GTK_LABEL( di->error_lb ), str );


    /* date added */
    if( n <= 0 )
        str = none;
    else {
        const time_t baseline = stats[0]->addedDate;
        for( i=1; i<n; ++i )
            if( baseline != stats[i]->addedDate )
                break;
        if( i==n )
            str = gtr_localtime2( buf, baseline, sizeof( buf ) );
        else
            str = mixed;
    }
    gtk_label_set_text( GTK_LABEL( di->date_added_lb ), str );


    /* activity date */
    if( n <= 0 )
        str = none;
    else {
        const time_t baseline = stats[0]->activityDate;
        for( i=1; i<n; ++i )
            if( baseline != stats[i]->activityDate )
                break;
        if( i==n )
            str = gtr_localtime2( buf, baseline, sizeof( buf ) );
        else
            str = mixed;
    }
    gtk_label_set_text( GTK_LABEL( di->last_activity_lb ), str );

    g_free( stats );
}

static GtkWidget*
activity_page_new( struct DetailsImpl * di )
{
    int  row = 0;
    GtkWidget * l;
    GtkWidget * t = hig_workarea_create( );

    hig_workarea_add_section_title( t, &row, _( "Transfer" ) );

    l = di->state_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "State:" ), l, NULL );

    l = di->progress_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Progress:" ), l, NULL );

    l = di->have_lb = gtk_label_new( NULL );
    /* "Have" refers to how much of the torrent we have */
    hig_workarea_add_row( t, &row, _( "Have:" ), l, NULL );

    l = di->dl_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Downloaded:" ), l, NULL );

    l = di->ul_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Uploaded:" ), l, NULL );

    /* how much downloaded data was corrupt */
    l = di->failed_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Failed DL:" ), l, NULL );

    l = di->ratio_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Ratio:" ), l, NULL );

    l = di->swarm_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Swarm speed:" ), l, NULL );

    l = di->error_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Error:" ), l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Dates" ) );

    l = di->date_added_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Started at:" ), l, NULL );

    l = di->last_activity_lb = gtk_label_new( NULL );
    hig_workarea_add_row( t, &row, _( "Last activity at:" ), l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_finish( t, &row );
    return t;
}

/****
*****
*****  INFO TAB
*****
****/

static void
refreshInfo( struct DetailsImpl * di, tr_torrent ** torrents, int n )
{
    int i;
    char buf[128];
    const char * str;
    const char * none = _( "None" );
    const char * mixed = _( "Mixed" );
    const char * unknown = _( "Unknown" );
    const tr_info ** infos = g_new( const tr_info*, n );

    /* info */
    for( i=0; i<n; ++i )
        infos[i] = tr_torrentInfo( torrents[i] );

    /* pieces_lb */
    if( n <= 0 )
        str = none;
    else {
        int sum = 0;
        const int baseline = infos[0]->pieceSize;
        for( i=0; i<n; ++i )
            sum += infos[i]->pieceCount;
        g_snprintf( buf, sizeof( buf ),
                    ngettext( "%'d Piece", "%'d Pieces", sum ), sum );
        for( i=1; i<n; ++i )
            if( baseline != (int)infos[i]->pieceSize )
                break;
        if( i==n ) {
            char tmp1[64];
            char tmp2[64];
            g_strlcpy( tmp1, buf, sizeof( tmp1 ) );
            tr_strlsize( tmp2, baseline, sizeof( tmp2 ) );
            g_snprintf( buf, sizeof( buf ), _( "%1$s @ %2$s" ), tmp1, tmp2 );
        }
        str = buf;
    }
    gtk_label_set_text( GTK_LABEL( di->pieces_lb ), str );
   

    /* hash_lb */
    if( n<=0 )
        str = none;
    else if ( n==1 )
        str = infos[0]->hashString; 
    else
        str = mixed;
    gtk_label_set_text( GTK_LABEL( di->hash_lb ), str );


    /* privacy_lb */
    if( n<=0 )
        str = none;
    else {
        const tr_bool baseline = infos[0]->isPrivate;
        for( i=1; i<n; ++i )
            if( baseline != infos[i]->isPrivate )
                break;
        if( i!=n )
            str = mixed;
        else if( baseline )
            str = _( "Private to this tracker -- PEX disabled" );
        else
            str = _( "Public torrent" );
    }
    gtk_label_set_text( GTK_LABEL( di->privacy_lb ), str );


    /* comment_buffer */
    if( n<=0 )
        str = "";
    else {
        const char * baseline = infos[0]->comment ? infos[0]->comment : "";
        for( i=1; i<n; ++i )
            if( strcmp( baseline, infos[i]->comment ? infos[i]->comment : "" ) )
                break;
        if( i==n )
            str = baseline;
        else
            str = mixed;
    }
    gtk_text_buffer_set_text( di->comment_buffer, str, -1 );


    /* creator_lb */
    if( n<=0 )
        str = none;
    else {
        const char * baseline = infos[0]->creator ? infos[0]->creator : "";
        for( i=1; i<n; ++i )
            if( strcmp( baseline, infos[i]->creator ? infos[i]->creator : "" ) )
                break;
        if( i==n )
            str = baseline;
        else
            str = mixed;
    }
    if( !str || !*str )
        str = unknown;
    gtk_label_set_text( GTK_LABEL( di->creator_lb ), str );


    /* date_created_lb */
    if( n<=0 )
        str = none;
    else {
        const time_t baseline = infos[0]->dateCreated;
        for( i=1; i<n; ++i )
            if( baseline != infos[i]->dateCreated )
                break;
        if( i==n )
            str = gtr_localtime2( buf, baseline, sizeof( buf ) );
        else
            str = mixed;
    }
    gtk_label_set_text( GTK_LABEL( di->date_created_lb ), str );
    

    /* destination_lb */        
    if( n<=0 )
        str = none;
    else {
        const char * baseline = tr_torrentGetDownloadDir( torrents[0] );
        for( i=1; i<n; ++i )
            if( strcmp( baseline, tr_torrentGetDownloadDir( torrents[i] ) ) )
                break;
        if( i==n )
            str = baseline;
        else
            str = mixed;
    }
    gtk_label_set_text( GTK_LABEL( di->destination_lb ), str );


    /* torrentfile_lb */
    if( n<=0 )
        str = none;
    else if( n==1 )
        str = infos[0]->torrent;
    else
        str = mixed;
    gtk_label_set_text( GTK_LABEL( di->torrentfile_lb ), str );

    g_free( infos );
}

static GtkWidget*
info_page_new( struct DetailsImpl * di )
{
    int row = 0;
    GtkTextBuffer * b;
    GtkWidget *l, *w, *fr;
    GtkWidget *t = hig_workarea_create( );

    hig_workarea_add_section_title( t, &row, _( "Details" ) );

        /* pieces */
        l = di->pieces_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _( "Pieces:" ), l, NULL );

        /* hash */
        l = g_object_new( GTK_TYPE_LABEL, "selectable", TRUE,
                                          "ellipsize", PANGO_ELLIPSIZE_END,
                                           NULL );
        hig_workarea_add_row( t, &row, _( "Hash:" ), l, NULL );
        di->hash_lb = l;

        /* privacy */
        l = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _( "Privacy:" ), l, NULL );
        di->privacy_lb = l;

        /* comment */
        b = di->comment_buffer = gtk_text_buffer_new( NULL );
        w = gtk_text_view_new_with_buffer( b );
        gtk_widget_set_size_request( w, 0u, 100u );
        gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW( w ), GTK_WRAP_WORD );
        gtk_text_view_set_editable( GTK_TEXT_VIEW( w ), FALSE );
        fr = gtk_frame_new( NULL );
        gtk_frame_set_shadow_type( GTK_FRAME( fr ), GTK_SHADOW_IN );
        gtk_container_add( GTK_CONTAINER( fr ), w );
        w = hig_workarea_add_row( t, &row, _( "Comment:" ), fr, NULL );
        gtk_misc_set_alignment( GTK_MISC( w ), 0.0f, 0.0f );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Origins" ) );

        l = di->creator_lb = gtk_label_new( NULL );
        gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
        hig_workarea_add_row( t, &row, _( "Creator:" ), l, NULL );

        l = di->date_created_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _( "Date:" ), l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Location" ) );

        l = g_object_new( GTK_TYPE_LABEL, "selectable", TRUE,
                                          "ellipsize", PANGO_ELLIPSIZE_END,
                                          NULL );
        hig_workarea_add_row( t, &row, _( "Destination:" ), l, NULL );
        di->destination_lb = l;

        l = g_object_new( GTK_TYPE_LABEL, "selectable", TRUE,
                                          "ellipsize", PANGO_ELLIPSIZE_END,
                                          NULL );
        hig_workarea_add_row( t, &row, _( "Torrent file:" ), l, NULL );
        di->torrentfile_lb = l;

    hig_workarea_finish( t, &row );
    return t;
}

/****
*****
*****  PEERS TAB
*****
****/

enum
{
    WEBSEED_COL_KEY,
    WEBSEED_COL_WAS_UPDATED,
    WEBSEED_COL_URL,
    WEBSEED_COL_DOWNLOAD_RATE,
    N_WEBSEED_COLS
};

static const char*
getWebseedColumnNames( int column )
{
    switch( column )
    {
        case WEBSEED_COL_URL: return _( "Webseeds" );
        case WEBSEED_COL_DOWNLOAD_RATE: return _( "Down" );
        default: return "";
    }
}

static GtkListStore*
webseed_model_new( void )
{
    return gtk_list_store_new( N_WEBSEED_COLS,
                               G_TYPE_STRING,   /* key */
                               G_TYPE_BOOLEAN,  /* was-updated */
                               G_TYPE_STRING,   /* url */
                               G_TYPE_FLOAT);   /* download rate */
}

enum
{
    PEER_COL_KEY,
    PEER_COL_WAS_UPDATED,
    PEER_COL_ADDRESS,
    PEER_COL_ADDRESS_COLLATED,
    PEER_COL_DOWNLOAD_RATE_DOUBLE,
    PEER_COL_DOWNLOAD_RATE_STRING,
    PEER_COL_UPLOAD_RATE_DOUBLE,
    PEER_COL_UPLOAD_RATE_STRING,
    PEER_COL_CLIENT,
    PEER_COL_PROGRESS,
    PEER_COL_IS_ENCRYPTED,
    PEER_COL_STATUS,
    N_PEER_COLS
};

static const char*
getPeerColumnName( int column )
{
    switch( column )
    {
        case PEER_COL_ADDRESS: return _( "Address" );
        case PEER_COL_DOWNLOAD_RATE_STRING:
        case PEER_COL_DOWNLOAD_RATE_DOUBLE: return _( "Down" );
        case PEER_COL_UPLOAD_RATE_STRING:
        case PEER_COL_UPLOAD_RATE_DOUBLE: return _( "Up" );
        case PEER_COL_CLIENT: return _( "Client" );
        case PEER_COL_PROGRESS: return _( "%" );
        case PEER_COL_STATUS: return _( "Status" );
        default: return "";
    }
}

static GtkListStore*
peer_store_new( void )
{
    return gtk_list_store_new( N_PEER_COLS,
                               G_TYPE_STRING,   /* key */
                               G_TYPE_BOOLEAN,  /* was-updated */
                               G_TYPE_STRING,   /* address */
                               G_TYPE_STRING,   /* collated address */
                               G_TYPE_FLOAT,    /* download speed float */
                               G_TYPE_STRING,   /* download speed string */
                               G_TYPE_FLOAT,    /* upload speed float */
                               G_TYPE_STRING,   /* upload speed string  */
                               G_TYPE_STRING,   /* client */
                               G_TYPE_INT,      /* progress [0..100] */
                               G_TYPE_BOOLEAN,  /* isEncrypted */
                               G_TYPE_STRING);  /* flagString */
}

static void
initPeerRow( GtkListStore        * store,
             GtkTreeIter         * iter,
             const char          * key,
             const tr_peer_stat  * peer )
{
    int q[4];
    char up_speed[128];
    char down_speed[128];
    char collated_name[128];
    const char * client = peer->client;

    if( !client || !strcmp( client, "Unknown Client" ) )
        client = "";

    tr_strlspeed( up_speed, peer->rateToPeer, sizeof( up_speed ) );
    tr_strlspeed( down_speed, peer->rateToClient, sizeof( down_speed ) );
    if( sscanf( peer->addr, "%d.%d.%d.%d", q, q+1, q+2, q+3 ) != 4 )
        g_strlcpy( collated_name, peer->addr, sizeof( collated_name ) );
    else
        g_snprintf( collated_name, sizeof( collated_name ),
                    "%03d.%03d.%03d.%03d", q[0], q[1], q[2], q[3] );

    gtk_list_store_set( store, iter,
                        PEER_COL_ADDRESS, peer->addr,
                        PEER_COL_ADDRESS_COLLATED, collated_name,
                        PEER_COL_CLIENT, client,
                        PEER_COL_IS_ENCRYPTED, peer->isEncrypted,
                        PEER_COL_KEY, key,
                        -1 );
}

static void
render_encrypted( GtkTreeViewColumn  * column UNUSED,
                  GtkCellRenderer    * renderer,
                  GtkTreeModel       * tree_model,
                  GtkTreeIter        * iter,
                  gpointer             data   UNUSED )
{
    gboolean b = FALSE;

    gtk_tree_model_get( tree_model, iter, PEER_COL_IS_ENCRYPTED, &b, -1 );
    g_object_set( renderer, "xalign", (gfloat)0.0,
                            "yalign", (gfloat)0.5,
                            "stock-id", ( b ? "transmission-lock" : NULL ),
                            NULL );
}

static void
refreshPeerRow( GtkListStore        * store,
                GtkTreeIter         * iter,
                const tr_peer_stat  * peer )
{
    char up_speed[128];
    char down_speed[128];

    if( peer->rateToPeer > 0.01 )
        tr_strlspeed( up_speed, peer->rateToPeer, sizeof( up_speed ) );
    else
        *up_speed = '\0';

    if( peer->rateToClient > 0.01 )
        tr_strlspeed( down_speed, peer->rateToClient, sizeof( down_speed ) );
    else
        *down_speed = '\0';

    gtk_list_store_set( store, iter,
                        PEER_COL_PROGRESS, (int)( 100.0 * peer->progress ),
                        PEER_COL_DOWNLOAD_RATE_DOUBLE, peer->rateToClient,
                        PEER_COL_DOWNLOAD_RATE_STRING, down_speed,
                        PEER_COL_UPLOAD_RATE_DOUBLE, peer->rateToPeer,
                        PEER_COL_UPLOAD_RATE_STRING, up_speed,
                        PEER_COL_STATUS, peer->flagStr,
                        PEER_COL_WAS_UPDATED, TRUE,
                        -1 );
}

static void
refreshPeerList( struct DetailsImpl * di, tr_torrent ** torrents, int n )
{
    int i;
    int * peerCount;
    GtkTreeIter iter;
    GHashTable * hash = di->peer_hash;
    GtkListStore * store = di->peer_store;
    GtkTreeModel * model = GTK_TREE_MODEL( store );
    struct tr_peer_stat ** peers;

    /* step 1: get all the peers */
    peers = g_new( struct tr_peer_stat*, n );
    peerCount = g_new( int, n );
    for( i=0; i<n; ++i )
        peers[i] = tr_torrentPeers( torrents[i], &peerCount[i] );

    /* step 2: mark all the peers in the list as not-updated */
    model = GTK_TREE_MODEL( store );
    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
        gtk_list_store_set( store, &iter, PEER_COL_WAS_UPDATED, FALSE, -1 );
    while( gtk_tree_model_iter_next( model, &iter ) );

    /* step 3: add any new peers */
    for( i=0; i<n; ++i ) {
        int j;
        const tr_torrent * tor = torrents[i];
        for( j=0; j<peerCount[i]; ++j ) {
            const tr_peer_stat * s = &peers[i][j];
            char key[128];
            g_snprintf( key, sizeof(key), "%d.%s", tr_torrentId(tor), s->addr );
            if( g_hash_table_lookup( hash, key ) == NULL ) {
                GtkTreePath * p;
                gtk_list_store_append( store, &iter );
                initPeerRow( store, &iter, key, s );
                p = gtk_tree_model_get_path( model, &iter );
                g_hash_table_insert( hash, g_strdup( key ),
                                     gtk_tree_row_reference_new( model, p ) );
                gtk_tree_path_free( p );
            }
        }
    }

    /* step 4: update the peers */
    for( i=0; i<n; ++i ) {
        int j;
        const tr_torrent * tor = torrents[i];
        for( j=0; j<peerCount[i]; ++j ) {
            const tr_peer_stat * s = &peers[i][j];
            char key[128];
            GtkTreeRowReference * ref;
            GtkTreePath * p;
            g_snprintf( key, sizeof(key), "%d.%s", tr_torrentId(tor), s->addr );
            ref = g_hash_table_lookup( hash, key );
            p = gtk_tree_row_reference_get_path( ref );
            gtk_tree_model_get_iter( model, &iter, p );
            refreshPeerRow( store, &iter, s );
            gtk_tree_path_free( p );
        }
    }

    /* step 5: remove peers that have disappeared */
    model = GTK_TREE_MODEL( store );
    if( gtk_tree_model_get_iter_first( model, &iter ) ) {
        gboolean more = TRUE;
        while( more ) {
            gboolean b;
            gtk_tree_model_get( model, &iter, PEER_COL_WAS_UPDATED, &b, -1 );
            if( b )
                more = gtk_tree_model_iter_next( model, &iter );
            else {
                char * key;
                gtk_tree_model_get( model, &iter, PEER_COL_KEY, &key, -1 );
                g_hash_table_remove( hash, key );
                more = gtk_list_store_remove( store, &iter );
                g_free( key );
            }
        }
    }

    /* step 6: cleanup */
    for( i=0; i<n; ++i )
        tr_torrentPeersFree( peers[i], peerCount[i] );
    tr_free( peers );
    tr_free( peerCount );
}

static void
refreshWebseedList( struct DetailsImpl * di, tr_torrent ** torrents, int n )
{
    int i;
    int total = 0;
    GtkTreeIter iter;
    GHashTable * hash = di->webseed_hash;
    GtkListStore * store = di->webseed_store;
    GtkTreeModel * model = GTK_TREE_MODEL( store );
    

    /* step 1: mark all webseeds as not-updated */
    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
        gtk_list_store_set( store, &iter, WEBSEED_COL_WAS_UPDATED, FALSE, -1 );
    while( gtk_tree_model_iter_next( model, &iter ) );

    /* step 2: add any new webseeds */
    for( i=0; i<n; ++i ) {
        int j;
        const tr_torrent * tor = torrents[i];
        const tr_info * inf = tr_torrentInfo( tor );
        total += inf->webseedCount;
        for( j=0; j<inf->webseedCount; ++j ) {
            char key[256];
            const char * url = inf->webseeds[j];
            g_snprintf( key, sizeof(key), "%d.%s", tr_torrentId( tor ), url );
            if( g_hash_table_lookup( hash, key ) == NULL ) {
                GtkTreePath * p;
                gtk_list_store_append( store, &iter );
                gtk_list_store_set( store, &iter, WEBSEED_COL_URL, url, -1 );
                p = gtk_tree_model_get_path( model, &iter );
                g_hash_table_insert( hash, g_strdup( key ),
                                     gtk_tree_row_reference_new( model, p ) );
                gtk_tree_path_free( p );
            }
        }
    }

    /* step 3: update the webseeds */
    for( i=0; i<n; ++i ) {
        int j;
        const tr_torrent * tor = torrents[i];
        const tr_info * inf = tr_torrentInfo( tor );
        float * speeds = tr_torrentWebSpeeds( tor );
        for( j=0; j<inf->webseedCount; ++j ) {
            char key[256];
            const char * url = inf->webseeds[j];
            GtkTreePath * p;
            GtkTreeRowReference * ref;
            g_snprintf( key, sizeof(key), "%d.%s", tr_torrentId( tor ), url );
            ref = g_hash_table_lookup( hash, key );
            p = gtk_tree_row_reference_get_path( ref );
            gtk_tree_model_get_iter( model, &iter, p );
            gtk_list_store_set( store, &iter, WEBSEED_COL_DOWNLOAD_RATE, (int)speeds[j], -1 );
            gtk_tree_path_free( p );
        }
        tr_free( speeds );
    }

    /* step 4: remove webseeds that have disappeared */
    if( gtk_tree_model_get_iter_first( model, &iter ) ) {
        gboolean more = TRUE;
        while( more ) {
            gboolean b;
            gtk_tree_model_get( model, &iter, WEBSEED_COL_WAS_UPDATED, &b, -1 );
            if( b )
                more = gtk_tree_model_iter_next( model, &iter );
            else {
                char * key;
                gtk_tree_model_get( model, &iter, WEBSEED_COL_KEY, &key, -1 );
                g_hash_table_remove( hash, key );
                more = gtk_list_store_remove( store, &iter );
                g_free( key );
            }
        }
    }

    /* most of the time there are no webseeds...
       if that's the case, don't waste space showing an empty list */
    if( total > 0 )
        gtk_widget_show( di->webseed_view );
    else
        gtk_widget_hide( di->webseed_view );
}

static void
refreshPeers( struct DetailsImpl * di, tr_torrent ** torrents, int n )
{
    int i;
    char buf[512];
    const char * none = _( "None" );

    /* seeders_lb */
    /* leechers_lb */
    /* completed_lb */
    if( n<=0 ) {
        gtk_label_set_text( GTK_LABEL( di->seeders_lb ), none );
        gtk_label_set_text( GTK_LABEL( di->leechers_lb ), none );
        gtk_label_set_text( GTK_LABEL( di->completed_lb ), none );
    } else {
        int seeders = 0;
        int leechers = 0;
        int completed = 0;
        for( i=0; i<n; ++i ) {
            const tr_stat * s = tr_torrentStat( torrents[i] );
            seeders = s->seeders;
            leechers = s->leechers;
            completed += s->timesCompleted;
        }
        g_snprintf( buf, sizeof( buf ), "%'d", seeders );
        gtk_label_set_text( GTK_LABEL( di->seeders_lb ), buf );
        g_snprintf( buf, sizeof( buf ), "%'d", leechers );
        gtk_label_set_text( GTK_LABEL( di->leechers_lb ), buf );
        g_snprintf( buf, sizeof( buf ), "%'d", completed );
        gtk_label_set_text( GTK_LABEL( di->completed_lb ), buf );
    }

    refreshPeerList( di, torrents, n );
    refreshWebseedList( di, torrents, n );
}

#if GTK_CHECK_VERSION( 2,12,0 )
static gboolean
onPeerViewQueryTooltip( GtkWidget   * widget,
                        gint          x,
                        gint          y,
                        gboolean      keyboard_tip,
                        GtkTooltip  * tooltip,
                        gpointer      user_data UNUSED )
{
    gboolean       show_tip = FALSE;
    GtkTreeModel * model;
    GtkTreeIter    iter;

    if( gtk_tree_view_get_tooltip_context( GTK_TREE_VIEW( widget ),
                                           &x, &y, keyboard_tip,
                                           &model, NULL, &iter ) )
    {
        const char * pch;
        char *       str = NULL;
        GString *    gstr = g_string_new( NULL );
        gtk_tree_model_get( model, &iter, PEER_COL_STATUS, &str, -1 );
        for( pch = str; pch && *pch; ++pch )
        {
            const char * s = NULL;
            switch( *pch )
            {
                case 'O': s = _( "Optimistic unchoke" ); break;
                case 'D': s = _( "Downloading from this peer" ); break;
                case 'd': s = _( "We would download from this peer if they would let us" ); break;
                case 'U': s = _( "Uploading to peer" ); break; 
                case 'u': s = _( "We would upload to this peer if they asked" ); break;
                case 'K': s = _( "Peer has unchoked us, but we're not interested" ); break;
                case '?': s = _( "We unchoked this peer, but they're not interested" ); break;
                case 'E': s = _( "Encrypted connection" ); break; 
                case 'X': s = _( "Peer was discovered through Peer Exchange (PEX)" ); break;
                case 'I': s = _( "Peer is an incoming connection" ); break;
            }
            if( s )
                g_string_append_printf( gstr, "%c: %s\n", *pch, s );
        }
        if( gstr->len ) /* remove the last linefeed */
            g_string_set_size( gstr, gstr->len - 1 );
        gtk_tooltip_set_text( tooltip, gstr->str );
        g_string_free( gstr, TRUE );
        g_free( str );
        show_tip = TRUE;
    }

    return show_tip;
}
#endif

static GtkWidget*
peer_page_new( struct DetailsImpl * di )
{
    guint i;
    const char * str;
    GtkListStore *store;
    GtkWidget *v, *w, *ret, *sw, *l, *vbox, *hbox;
    GtkWidget *webtree = NULL;
    GtkTreeViewColumn * c;
    GtkCellRenderer *   r;
    int view_columns[] = { PEER_COL_IS_ENCRYPTED,
                           PEER_COL_UPLOAD_RATE_STRING,
                           PEER_COL_DOWNLOAD_RATE_STRING,
                           PEER_COL_PROGRESS,
                           PEER_COL_STATUS,
                           PEER_COL_ADDRESS,
                           PEER_COL_CLIENT };


    /* webseeds */

    store = di->webseed_store = webseed_model_new( );
    v = gtk_tree_view_new_with_model( GTK_TREE_MODEL( store ) );
    g_signal_connect( v, "button-release-event", G_CALLBACK( on_tree_view_button_released ), NULL );
    gtk_tree_view_set_rules_hint( GTK_TREE_VIEW( v ), TRUE );
    g_object_unref( G_OBJECT( store ) );

    str = getWebseedColumnNames( WEBSEED_COL_URL );
    r = gtk_cell_renderer_text_new( );
    g_object_set( G_OBJECT( r ), "ellipsize", PANGO_ELLIPSIZE_END, NULL );
    c = gtk_tree_view_column_new_with_attributes( str, r, "text", WEBSEED_COL_URL, NULL );
    g_object_set( G_OBJECT( c ), "expand", TRUE, NULL );
    gtk_tree_view_column_set_sort_column_id( c, WEBSEED_COL_URL );
    gtk_tree_view_append_column( GTK_TREE_VIEW( v ), c );

    str = getWebseedColumnNames( WEBSEED_COL_DOWNLOAD_RATE );
    r = gtk_cell_renderer_text_new( );
    c = gtk_tree_view_column_new_with_attributes( str, r, "text", WEBSEED_COL_DOWNLOAD_RATE, NULL );
    gtk_tree_view_column_set_sort_column_id( c, WEBSEED_COL_DOWNLOAD_RATE );
    gtk_tree_view_append_column( GTK_TREE_VIEW( v ), c );

    w = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( w ),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW( w ),
                                         GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( w ), v );

    webtree = w;
    di->webseed_view = w;

    /* peers */

    store  = di->peer_store = peer_store_new( );
    v = GTK_WIDGET( g_object_new( GTK_TYPE_TREE_VIEW,
                                  "model",  gtk_tree_model_sort_new_with_model( GTK_TREE_MODEL( store ) ),
                                  "rules-hint", TRUE,
#if GTK_CHECK_VERSION( 2,12,0 )
                                  "has-tooltip", TRUE,
#endif
                                  NULL ) );

#if GTK_CHECK_VERSION( 2,12,0 )
    g_signal_connect( v, "query-tooltip",
                      G_CALLBACK( onPeerViewQueryTooltip ), NULL );
#endif
    g_object_unref( G_OBJECT( store ) );
    g_signal_connect( v, "button-release-event",
                      G_CALLBACK( on_tree_view_button_released ), NULL );

    for( i=0; i<G_N_ELEMENTS( view_columns ); ++i )
    {
        const int col = view_columns[i];
        const char * t = getPeerColumnName( col );
        int sort_col = col;

        switch( col )
        {
            case PEER_COL_ADDRESS:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                sort_col = PEER_COL_ADDRESS_COLLATED;
                break;

            case PEER_COL_CLIENT:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                break;

            case PEER_COL_PROGRESS:
                r = gtk_cell_renderer_progress_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "value", PEER_COL_PROGRESS, NULL );
                break;

            case PEER_COL_IS_ENCRYPTED:
                r = gtk_cell_renderer_pixbuf_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, NULL );
                gtk_tree_view_column_set_sizing( c, GTK_TREE_VIEW_COLUMN_FIXED );
                gtk_tree_view_column_set_fixed_width( c, 20 );
                gtk_tree_view_column_set_cell_data_func( c, r, render_encrypted, NULL, NULL );
                break;

            case PEER_COL_DOWNLOAD_RATE_STRING:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                sort_col = PEER_COL_DOWNLOAD_RATE_DOUBLE;
                break;

            case PEER_COL_UPLOAD_RATE_STRING:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                sort_col = PEER_COL_UPLOAD_RATE_DOUBLE;
                break;

            case PEER_COL_STATUS:
                r = gtk_cell_renderer_text_new( );
                c = gtk_tree_view_column_new_with_attributes( t, r, "text", col, NULL );
                break;

            default:
                abort( );
        }

        gtk_tree_view_column_set_resizable( c, FALSE );
        gtk_tree_view_column_set_sort_column_id( c, sort_col );
        gtk_tree_view_append_column( GTK_TREE_VIEW( v ), c );
    }

    /* the 'expander' column has a 10-pixel margin on the left
       that doesn't look quite correct in any of these columns...
       so create a non-visible column and assign it as the
       'expander column. */
    {
        GtkTreeViewColumn *c = gtk_tree_view_column_new( );
        gtk_tree_view_column_set_visible( c, FALSE );
        gtk_tree_view_append_column( GTK_TREE_VIEW( v ), c );
        gtk_tree_view_set_expander_column( GTK_TREE_VIEW( v ), c );
    }

    w = sw = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( w ),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW( w ),
                                         GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( w ), v );


    vbox = gtk_vbox_new( FALSE, GUI_PAD );
    gtk_container_set_border_width( GTK_CONTAINER( vbox ), GUI_PAD_BIG );

    v = gtk_vpaned_new( );
    gtk_paned_pack1( GTK_PANED( v ), webtree, FALSE, TRUE );
    gtk_paned_pack2( GTK_PANED( v ), sw, TRUE, TRUE );
    gtk_box_pack_start( GTK_BOX( vbox ), v, TRUE, TRUE, 0 );

    hbox = gtk_hbox_new( FALSE, GUI_PAD );
    l = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( l ), _( "<b>Seeders:</b>" ) );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    l = di->seeders_lb = gtk_label_new( NULL );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
    gtk_box_pack_start( GTK_BOX( hbox ), w, TRUE, TRUE, 0 );
    l = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( l ), _( "<b>Leechers:</b>" ) );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    l = di->leechers_lb = gtk_label_new( NULL );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
    gtk_box_pack_start( GTK_BOX( hbox ), w, TRUE, TRUE, 0 );
    l = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL( l ), _( "<b>Times Completed:</b>" ) );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    l = di->completed_lb = gtk_label_new( NULL );
    gtk_box_pack_start( GTK_BOX( hbox ), l, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    /* ip-to-GtkTreeRowReference */
    di->peer_hash = g_hash_table_new_full( g_str_hash,
                                           g_str_equal,
                                           (GDestroyNotify)g_free,
                                           (GDestroyNotify)gtk_tree_row_reference_free );

    /* url-to-GtkTreeRowReference */
    di->webseed_hash = g_hash_table_new_full( g_str_hash,
                                              g_str_equal,
                                              (GDestroyNotify)g_free,
                                              (GDestroyNotify)gtk_tree_row_reference_free );
                           
    ret = vbox;
    return ret;
}



/****
*****  TRACKER
****/

static void
refreshTracker( struct DetailsImpl * di, tr_torrent ** torrents, int n )
{
    int i;
    char buf[256];
    const char * str;
    const char * none = _("None" );
    const char * mixed = _( "Mixed" );
    const char * noneSent = _( "None sent" );
    const char * inProgress = _( "In progress" );
    const time_t now = time( NULL );
    const tr_stat ** stats;

    stats = g_new( const tr_stat*, n );
    for( i=0; i<n; ++i )
        stats[i] = tr_torrentStatCached( torrents[i] );


    /* last_scrape_time_lb */
    if( n<1 )
        str = none;
    else {
        const time_t baseline = stats[0]->lastScrapeTime;
        for( i=1; i<n; ++i )
            if( baseline != stats[i]->lastScrapeTime )
                break;
        if( i!=n )
            str = mixed;
        else if( baseline==0 )
            str = noneSent;
        else
            str = gtr_localtime2( buf, baseline, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->last_scrape_time_lb ), str );


    /* last_scrape_response_lb */
    if( n<1 )
        str = none;
    else {
        const char * baseline = stats[0]->scrapeResponse;
        for( i=1; i<n; ++i )
            if( strcmp( baseline, stats[i]->scrapeResponse ) )
                break;
        if( i==n )
            str = baseline;
        else
            str = mixed;
    }
    gtk_label_set_text( GTK_LABEL( di->last_scrape_response_lb ), str );


    /* next_scrape_countdown_lb */
    if( n<1 )
        str = none;
    else {
        const time_t baseline = stats[0]->nextScrapeTime;
        for( i=1; i<n; ++i )
            if( baseline != stats[i]->nextScrapeTime )
                break;
        if( i!=n )
            str = mixed;
        else if( baseline <= now )
            str = inProgress;
        else
            str = tr_strltime( buf, baseline - now, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->next_scrape_countdown_lb ), str );


    /* last_announce_time_lb */
    if( n<1 )
        str = none;
    else {
        const time_t baseline = stats[0]->lastAnnounceTime;
        for( i=1; i<n; ++i )
            if( baseline != stats[i]->lastAnnounceTime )
                break;
        if( i!=n )
            str = mixed;
        else if( baseline==0 )
            str = noneSent;
        else
            str = gtr_localtime2( buf, baseline, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->last_announce_time_lb ), str );


    /* last_announce_response_lb */
    if( n<1 )
        str = none;
    else {
        const char * baseline = stats[0]->announceResponse;
        for( i=1; i<n; ++i )
            if( strcmp( baseline, stats[i]->announceResponse ) )
                break;
        if( i==n )
            str = baseline;
        else
            str = mixed;
    }
    gtk_label_set_text( GTK_LABEL( di->last_announce_response_lb ), str );


    /* next_announce_countdown_lb */
    if( n<1 )
        str = none;
    else {
        const time_t baseline = stats[0]->nextAnnounceTime;
        for( i=1; i<n; ++i )
            if( baseline != stats[i]->nextAnnounceTime )
                break;
        if( i!=n )
            str = mixed;
        else if( baseline==0 )
            str = none;
        else if( baseline==1 || baseline<=now )
            str = inProgress;
        else
            str = tr_strltime( buf, baseline - now, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->next_announce_countdown_lb ), str );


    /* manual_announce_countdown_lb */
    if( n<1 )
        str = none;
    else {
        const time_t baseline = stats[0]->manualAnnounceTime;
        for( i=1; i<n; ++i )
            if( baseline != stats[i]->manualAnnounceTime )
                break;
        if( i!=n )
            str = mixed;
        else if( baseline<1 )
            str = none;
        else if( baseline<=now )
            str = inProgress;
        else
            str = tr_strltime( buf, baseline - now, sizeof( buf ) );
    }
    gtk_label_set_text( GTK_LABEL( di->manual_announce_countdown_lb ), str );


    g_free( stats );
}

static GtkWidget*
tracker_page_new( struct DetailsImpl * di )
{
    int row = 0;
    const char * s;
    GtkWidget *t, *l, *w;

    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Trackers" ) );

        w = tracker_list_new( tr_core_session( di->core ), -1, FALSE );
        hig_workarea_add_wide_control( t, &row, w );
        di->tracker_list = w;

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Scrape" ) );

        s = _( "Last scrape at:" );
        l = gtk_label_new( NULL );
        di->last_scrape_time_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        s = _( "Tracker responded:" );
        l = gtk_label_new( NULL );
        di->last_scrape_response_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        s = _( "Next scrape in:" );
        l = gtk_label_new( NULL );
        di->next_scrape_countdown_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Announce" ) );

        l = gtk_label_new( NULL );
        gtk_label_set_ellipsize( GTK_LABEL( l ), PANGO_ELLIPSIZE_END );
        hig_workarea_add_row( t, &row, _( "Tracker:" ), l, NULL );

        s = _( "Last announce at:" );
        l = gtk_label_new( NULL );
        di->last_announce_time_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        s = _( "Tracker responded:" );
        l = gtk_label_new( NULL );
        di->last_announce_response_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        s = _( "Next announce in:" );
        l = gtk_label_new( NULL );
        di->next_announce_countdown_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

        /* how long until the tracker will honor user
         * pressing the "ask for more peers" button */
        s = _( "Manual announce allowed in:" );
        l = gtk_label_new( NULL );
        di->manual_announce_countdown_lb = l;
        hig_workarea_add_row( t, &row, s, l, NULL );

    hig_workarea_finish( t, &row );
    return t;
}


/****
*****  DIALOG
****/

static void
refresh( struct DetailsImpl * di )
{
    int n;
    tr_torrent ** torrents = getTorrents( di, &n );

    refreshInfo( di, torrents, n );
    refreshPeers( di, torrents, n );
    refreshTracker( di, torrents, n );
    refreshOptions( di, torrents, n );
    refreshActivity( di, torrents, n );

    g_free( torrents );
}

static gboolean
periodic_refresh( gpointer data )
{
    refresh( data );
    return TRUE;
}

static void
details_free( gpointer gdata )
{
    struct DetailsImpl * data = gdata;
    g_signal_handler_disconnect( data->core, data->prefs_changed_tag );
    g_source_remove( data->periodic_refresh_tag );
    g_slist_free( data->ids );
    g_free( data );
}

static void
response_cb( GtkDialog * dialog, int a UNUSED, gpointer b UNUSED )
{
    GtkWidget * w = GTK_WIDGET( dialog );
    torrent_inspector_set_torrents( w, NULL );
    gtk_widget_hide( w );
}

GtkWidget*
torrent_inspector_new( GtkWindow * parent, TrCore * core )
{
    GtkWidget * d, * n, * w, * l;
    struct DetailsImpl * di = g_new0( struct DetailsImpl, 1 );

    /* create the dialog */
    di->core = core;
    d = gtk_dialog_new_with_buttons( NULL, parent, 0,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    gtk_window_set_role( GTK_WINDOW( d ), "tr-info" );
    g_signal_connect( d, "response", G_CALLBACK( response_cb ), NULL );
    gtk_dialog_set_has_separator( GTK_DIALOG( d ), FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( d ), GUI_PAD );
    g_object_set_data_full( G_OBJECT( d ), DETAILS_KEY, di, details_free );

    n = gtk_notebook_new( );
    gtk_container_set_border_width( GTK_CONTAINER( n ), GUI_PAD );

    w = activity_page_new( di );
    l = gtk_label_new( _( "Activity" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, l );

    w = peer_page_new( di );
    l = gtk_label_new( _( "Peers" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ),  w, l );

    w = tracker_page_new( di );
    l = gtk_label_new( _( "Tracker" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, l );

    w = info_page_new( di );
    l = gtk_label_new( _( "Information" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, l );

    w = file_list_new( core, 0 );
    gtk_container_set_border_width( GTK_CONTAINER( w ), GUI_PAD_BIG );
    l = gtk_label_new( _( "Files" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, l );
    di->file_list = w;

    w = options_page_new( di );
    l = gtk_label_new( _( "Options" ) );
    gtk_notebook_append_page( GTK_NOTEBOOK( n ), w, l );

    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( d )->vbox ), n, TRUE, TRUE, 0 );

    di->periodic_refresh_tag = gtr_timeout_add_seconds( UPDATE_INTERVAL_SECONDS,
                                                        periodic_refresh, di );
    periodic_refresh( di );
    gtk_widget_show_all( GTK_DIALOG( d )->vbox );
    return d;
}

void
torrent_inspector_set_torrents( GtkWidget * w, GSList * ids )
{
    struct DetailsImpl * di = g_object_get_data( G_OBJECT( w ), DETAILS_KEY );
    const int len = g_slist_length( ids );
    char title[256];

    g_slist_free( di->ids );
    di->ids = g_slist_copy( ids );

    if( len == 1 )
    {
        const int id = GPOINTER_TO_INT( ids->data );
        tr_session * session = tr_core_session( di->core );
        tr_torrent * tor = tr_torrentFindFromId( session, id );
        const tr_info * inf = tr_torrentInfo( tor );
        g_snprintf( title, sizeof( title ), _( "%s Properties" ), inf->name );

        file_list_set_torrent( di->file_list, id );
        tracker_list_set_torrent( di->tracker_list, id );
        
    }
   else
   {
        file_list_clear( di->file_list );
        tracker_list_clear( di->tracker_list );
        g_snprintf( title, sizeof( title ), _( "%'d Torrent Properties" ), len );
    }

    gtk_window_set_title( GTK_WINDOW( w ), title );

    refresh( di );
}
