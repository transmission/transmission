/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "conf.h"
#include "tr_icon.h"
#include "tr_prefs.h"
#include "tr_torrent.h"
#include "util.h"

#include "transmission.h"

/* used for g_object_set/get_data */
#define PREF_CHECK_LINK         "tr-prefs-check-link-thingy"
#define PREF_SPIN_LAST          "tr-prefs-spinbox-last-val"

/* convenience macros for saving pref id on a widget */
#define SETPREFID( wid, id )                                                  \
    ( g_object_set_data( G_OBJECT( (wid) ), "tr-prefs-id",                    \
                         GINT_TO_POINTER( (id) + 1 ) ) )
#define GETPREFID( wid, id )                                                  \
    do                                                                        \
    {                                                                         \
        (id) = GPOINTER_TO_INT( g_object_get_data( G_OBJECT( (wid) ),         \
                                                   "tr-prefs-id" ) );         \
        g_assert( 0 < (id) );                                                 \
        (id)--;                                                               \
    }                                                                         \
    while( 0 )

enum
{
    PROP_PARENT = 1,
};

#define PTYPE( id )                                                           \
    ( G_TYPE_NONE == defs[(id)]._type ?                                       \
      defs[(id)].typefunc() : defs[(id)]._type )

/* please keep this in sync with the enum in tr_prefs.c */
/* don't forget defs_int, defs_bool, and defs_file too */
static struct
{
    char       * name;
    GType        _type;         /* don't access this directly, use PTYPE() */
    enum { PR_ENABLED, PR_DISABLED, PR_SKIP } status;
    GType (*typefunc)(void);
    const char * label;
    const char * tip;
}
defs[] =
{
    /* PREF_ID_USEDOWNLIMIT */
    { "use-download-limit",     G_TYPE_BOOLEAN, PR_ENABLED,  NULL,
      N_("_Limit download speed"),
      N_("Restrict the download rate") },

    /* PREF_ID_DOWNLIMIT */
    { "download-limit",         G_TYPE_INT,     PR_ENABLED,  NULL,
      N_("Maximum _download speed:"),
      N_("Speed in KiB/sec for restricted download rate") },

    /* PREF_ID_USEUPLIMIT */
    { "use-upload-limit",       G_TYPE_BOOLEAN, PR_ENABLED,  NULL,
      N_("Li_mit upload speed"),
      N_("Restrict the upload rate") },

    /* PREF_ID_UPLIMIT */
    { "upload-limit",           G_TYPE_INT,     PR_ENABLED,  NULL,
      N_("Maximum _upload speed:"),
      N_("Speed in KiB/sec for restricted upload rate") },

    /* PREF_ID_ASKDIR */
    { "ask-download-directory", G_TYPE_BOOLEAN, PR_ENABLED,  NULL,
      N_("Al_ways prompt for download directory"),
      N_("When adding a torrent, always prompt for a directory to download data files into") },

    /* PREF_ID_DIR */
    { "download-directory",     G_TYPE_NONE,   PR_ENABLED,
      gtk_file_chooser_get_type,
      N_("Download di_rectory:"),
      N_("Destination directory for downloaded data files") },

    /* PREF_ID_PORT */
    { "listening-port",         G_TYPE_INT,     PR_ENABLED,  NULL,
      N_("Listening _port:"),
      N_("TCP port number to listen for peer connections") },

    /* PREF_ID_NAT */
    { "use-nat-traversal",      G_TYPE_BOOLEAN, PR_ENABLED,  NULL,
      N_("Au_tomatic port mapping via NAT-PMP or UPnP"),
      N_("Attempt to bypass NAT or firewall to allow incoming peer connections") },

    /* PREF_ID_ICON */
    { "use-tray-icon",          G_TYPE_BOOLEAN,
      ( tr_icon_supported() ? PR_ENABLED : PR_DISABLED ),    NULL,
      N_("Display an _icon in the system tray"),
      N_("Use a system tray / dock / notification area icon") },

    /* PREF_ID_ADDSTD */
    { "add-behavior-standard",  G_TYPE_NONE,    PR_ENABLED,
      gtk_combo_box_get_type,
      N_("For torrents added _normally:"),
      N_("Torrent files added via the toolbar, popup menu, and drag-and-drop") },

    /* PREF_ID_ADDIPC */
    { "add-behavior-ipc",       G_TYPE_NONE,    PR_ENABLED,
      gtk_combo_box_get_type,
      N_("For torrents added e_xternally\n(via the command-line):"),
      N_("For torrents added via the command-line only") },

    /* PREF_ID_MSGLEVEL */
    { "message-level",          G_TYPE_INT,     PR_SKIP, NULL, NULL, NULL },
};

static struct
{
    long min;
    long max;
    long def;
}
defs_int[] = 
{
    { 0, 0, 0 },
    /* PREF_ID_DOWNLIMIT */
    { 0, G_MAXLONG, 100 },
    { 0, 0, 0 },
    /* PREF_ID_UPLIMIT */
    { 0, G_MAXLONG, 20 },
    { 0, 0, 0 }, { 0, 0, 0 },
    /* PREF_ID_PORT */
    { 1, 0xffff,    TR_DEFAULT_PORT },
};

static struct
{
    gboolean def;
    int      link;
    gboolean enables;
}
defs_bool[] = 
{
    /* PREF_ID_USEDOWNLIMIT */
    { FALSE, PREF_ID_DOWNLIMIT, TRUE },
    { FALSE, -1, FALSE },
    /* PREF_ID_USEUPLIMIT */
    { TRUE,  PREF_ID_UPLIMIT,   TRUE },
    { FALSE, -1, FALSE },
    /* PREF_ID_ASKDIR */
    { FALSE, PREF_ID_DIR,       FALSE },
    { FALSE, -1, FALSE }, { FALSE, -1, FALSE },
    /* PREF_ID_NAT */
    { TRUE,  -1,                FALSE },
    /* PREF_ID_ICON */
    { TRUE,  -1,                FALSE },
};

static struct
{
    const char         * title;
    GtkFileChooserAction act;
    const char * (*getdef)(void);
}
defs_file[] = 
{
    { NULL, 0, NULL }, { NULL, 0, NULL }, { NULL, 0, NULL },
    { NULL, 0, NULL }, { NULL, 0, NULL },
    /* PREF_ID_DIR */
    { N_("Choose a download directory"),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
      getdownloaddir },
};

struct checkctl
{
    GtkToggleButton * check;
    GtkWidget       * wids[2];
    gboolean          enables;
};

static void
tr_prefs_init( GTypeInstance * instance, gpointer g_class );
static void
tr_prefs_set_property( GObject * object, guint property_id,
                      const GValue * value, GParamSpec * pspec );
static void
tr_prefs_get_property( GObject * object, guint property_id,
                      GValue * value, GParamSpec * pspec);
static void
tr_prefs_class_init( gpointer g_class, gpointer g_class_data );
static void
tr_prefs_dispose( GObject * obj );
static void
gotresp( GtkWidget * widget, int resp, gpointer data );
static int
countprefs( void );
static void
makelinks( struct checkctl ** links );
static void
filllinks( int id, GtkWidget * wid1, GtkWidget * wid2,
           struct checkctl ** links );
static void
pokelink( struct checkctl * link );
static void
addwidget( TrPrefs * self, int id, GtkTable * table, int off,
           GtkTooltips * tips, struct checkctl ** links );
static GtkWidget *
tipbox( GtkWidget * widget, GtkTooltips * tips, const char * tip );
static void
addwid_bool( TrPrefs * self, int id, GtkTooltips * tips,
             GtkWidget ** wid1, struct checkctl ** links );
static void
checkclick( GtkWidget * widget, gpointer data );
static void
addwid_int( TrPrefs * self, int id, GtkTooltips * tips,
            GtkWidget ** wid1, GtkWidget ** wid2 );
static gboolean
spinfocus( GtkWidget * widget, GdkEventFocus *event, gpointer data );
static void
spindie( GtkWidget * widget, gpointer data );
static void
addwid_file( TrPrefs * self, int id, GtkTooltips * tips,
             GtkWidget ** wid1, GtkWidget ** wid2 );
static void
filechosen( GtkWidget * widget, gpointer data );
static GtkTreeModel *
makecombomodel( void );
static void
addwid_combo( TrPrefs * self, int id, GtkTooltips * tips,
              GtkWidget ** wid1, GtkWidget ** wid2 );
static void
combochosen( GtkWidget * widget, gpointer data );
static void
savepref( TrPrefs * self, int id, const char * val );

GType
tr_prefs_get_type( void )
{
    static GType type = 0;

    if( 0 == type )
    {
        static const GTypeInfo info =
        {
            sizeof( TrPrefsClass ),
            NULL,                       /* base_init */
            NULL,                       /* base_finalize */
            tr_prefs_class_init,        /* class_init */
            NULL,                       /* class_finalize */
            NULL,                       /* class_data */
            sizeof( TrPrefs ),
            0,                          /* n_preallocs */
            tr_prefs_init,              /* instance_init */
            NULL,
        };
        type = g_type_register_static( GTK_TYPE_DIALOG, "TrPrefs", &info, 0 );
    }

    return type;
}

static void
tr_prefs_class_init( gpointer g_class, gpointer g_class_data SHUTUP )
{
    GObjectClass * gobject_class;
    TrPrefsClass  * trprefs_class;
    GParamSpec   * pspec;

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->set_property = tr_prefs_set_property;
    gobject_class->get_property = tr_prefs_get_property;
    gobject_class->dispose      = tr_prefs_dispose;

    pspec = g_param_spec_object( "parent", _("Parent"),
                                 _("The parent GtkWindow."),
                                 GTK_TYPE_WINDOW, G_PARAM_READWRITE );
    g_object_class_install_property( gobject_class, PROP_PARENT, pspec );

    trprefs_class = TR_PREFS_CLASS( g_class );
    trprefs_class->changesig =
        g_signal_new( "prefs-changed", G_TYPE_FROM_CLASS( g_class ),
                       G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                       g_cclosure_marshal_VOID__INT,
                       G_TYPE_NONE, 1, G_TYPE_INT );
}

static void
tr_prefs_init( GTypeInstance * instance, gpointer g_class SHUTUP )
{
    struct checkctl * links[ ALEN( defs_bool ) ];
    TrPrefs     * self = ( TrPrefs * )instance;
    char        * title;
    GtkWidget   * table;
    GtkTooltips * tips;
    int           rows, ii, off;

    self->combomodel = makecombomodel();
    self->disposed   = FALSE;

    title = g_strdup_printf( _("%s Preferences"), g_get_application_name() );
    gtk_window_set_title( GTK_WINDOW( self ), title );
    g_free( title );
    gtk_dialog_set_has_separator( GTK_DIALOG( self ), FALSE );
    gtk_dialog_add_button( GTK_DIALOG( self ), GTK_STOCK_CLOSE,
                           GTK_RESPONSE_CLOSE );
    gtk_widget_set_name( GTK_WIDGET( self ), "TransmissionDialog");
    gtk_dialog_set_default_response( GTK_DIALOG( self ), GTK_RESPONSE_CLOSE );
    gtk_container_set_border_width( GTK_CONTAINER( self ), 6 );
    gtk_window_set_resizable( GTK_WINDOW( self ), FALSE );

    rows = countprefs();
    table = gtk_table_new( rows, 2, FALSE );
    gtk_table_set_col_spacings( GTK_TABLE( table ), 8 );
    gtk_table_set_row_spacings( GTK_TABLE( table ), 8 );

    tips = gtk_tooltips_new();
    g_object_ref( tips );
    gtk_object_sink( GTK_OBJECT( tips ) );
    gtk_tooltips_enable( tips );
    g_signal_connect_swapped( self, "destroy",
                              G_CALLBACK( g_object_unref ), tips );

    memset( links, 0, sizeof( links ) );
    makelinks( links );
    off = 0;
    for( ii = 0; PREF_MAX_ID > ii; ii++ )
    {
        if( PR_SKIP != defs[ii].status )
        {
            addwidget( self, ii, GTK_TABLE( table ), off, tips, links );
            off++;
        }
    }
    g_assert( rows == off );
    for( ii = 0; ALEN( links ) > ii; ii++ )
    {
        g_assert( NULL == links[ii] || NULL != links[ii]->check );
        if( NULL != links[ii] )
        {
            pokelink( links[ii] );
        }
    }

    gtk_box_pack_start_defaults( GTK_BOX( GTK_DIALOG( self )->vbox ), table );
    g_signal_connect( self, "response", G_CALLBACK( gotresp ), NULL );
    gtk_widget_show_all( table );
}

static void
tr_prefs_set_property( GObject * object, guint property_id,
                      const GValue * value, GParamSpec * pspec)
{
    TrPrefs * self = ( TrPrefs * )object;

    if( self->disposed )
    {
        return;
    }

    switch( property_id )
    {
        case PROP_PARENT:
            gtk_window_set_transient_for( GTK_WINDOW( self ),
                                          g_value_get_object( value ) );
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
tr_prefs_get_property( GObject * object, guint property_id,
                      GValue * value, GParamSpec * pspec )
{
    TrPrefs   * self = ( TrPrefs * )object;
    GtkWindow * trans;

    if( self->disposed )
    {
        return;
    }

    switch( property_id )
    {
        case PROP_PARENT:
            trans = gtk_window_get_transient_for( GTK_WINDOW( self ) );
            g_value_set_object( value, trans );
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
tr_prefs_dispose( GObject * obj )
{
    TrPrefs      * self = ( TrPrefs * )obj;
    GObjectClass * parent;

    if( self->disposed )
    {
        return;
    }
    self->disposed = TRUE;

    g_object_unref( self->combomodel );

    /* Chain up to the parent class */
    parent = g_type_class_peek( g_type_parent( TR_PREFS_TYPE ) );
    parent->dispose( obj );
}

TrPrefs *
tr_prefs_new( void )
{
    return g_object_new( TR_PREFS_TYPE, NULL );
}

TrPrefs *
tr_prefs_new_with_parent( GtkWindow * parent )
{
    return g_object_new( TR_PREFS_TYPE, "parent", parent, NULL );
}

const char *
tr_prefs_name( int id )
{
    g_assert( 0 <= id && PREF_MAX_ID > id  && ALEN( defs ) == PREF_MAX_ID );
    return defs[id].name;
}

gboolean
tr_prefs_get_int( int id, int * val )
{
    const char * str;
    char       * end;
    int          ret;

    str = tr_prefs_get( id );
    if( NULL == str || '\0' == *str )
    {
        return FALSE;
    }

    errno = 0;
    ret = strtol( str, &end, 10 );
    if( 0 != errno || NULL == end || '\0' != *end )
    {
        return FALSE;
    }
    *val = ret;
    return TRUE;
}

gboolean
tr_prefs_get_bool( int id, gboolean * val )
{
    const char * str;

    str = tr_prefs_get( id );
    if( NULL == str )
    {
        return FALSE;
    }
    *val = strbool( str );
    return TRUE;
}

int
tr_prefs_get_int_with_default( int id )
{
    int ret;

    g_assert( 0 <= id && ALEN( defs ) > id &&
              G_TYPE_INT == PTYPE( id ) && ALEN( defs_int ) > id );

    if( tr_prefs_get_int( id, &ret ) )
    {
        return ret;
    }
    return defs_int[id].def;
}

gboolean
tr_prefs_get_bool_with_default( int id )
{
    gboolean ret;

    g_assert( 0 <= id && ALEN( defs ) > id &&
              G_TYPE_BOOLEAN == PTYPE( id ) && ALEN( defs_bool ) > id );

    if( tr_prefs_get_bool( id, &ret ) )
    {
        return ret;
    }
    return defs_bool[id].def;

}

static void
gotresp( GtkWidget * widget, int resp SHUTUP, gpointer data SHUTUP )
{
    gtk_widget_destroy( widget );
}

static int
countprefs( void )
{
    int ii, ret;

    g_assert( ALEN( defs ) == PREF_MAX_ID );
    ret = 0;
    for( ii = 0; PREF_MAX_ID > ii; ii++ )
    {
        if( PR_SKIP != defs[ii].status )
        {
            ret++;
        }
    }

    return ret;
}

static void
makelinks( struct checkctl ** links )
{
    int ii;

    g_assert( ALEN( defs ) == PREF_MAX_ID );
    for( ii = 0; PREF_MAX_ID > ii; ii++ )
    {
        if( PR_SKIP == defs[ii].status || G_TYPE_BOOLEAN != PTYPE( ii ) )
        {
            continue;
        }
        g_assert( ALEN( defs_bool ) > ii );
        if( 0 <= defs_bool[ii].link )
        {
            links[ii] = g_new0( struct checkctl, 1 );
        }
    }
}

static void
filllinks( int id, GtkWidget * wid1, GtkWidget * wid2,
           struct checkctl ** links )
{
    int ii;

    g_assert( ALEN( defs ) >= ALEN( defs_bool ) );
    for( ii = 0; ALEN( defs_bool) > ii; ii++ )
    {
        if( NULL == links[ii] )
        {
            g_assert( PR_SKIP == defs[ii].status ||
                      G_TYPE_BOOLEAN != PTYPE( ii ) ||
                      0 > defs_bool[ii].link );
        }
        else
        {
            g_assert( PR_SKIP != defs[ii].status &&
                      G_TYPE_BOOLEAN == PTYPE( ii ) &&
                      0 <= defs_bool[ii].link );
            if( id == defs_bool[ii].link )
            {
                links[ii]->wids[0] = wid1;
                links[ii]->wids[1] = wid2;
            }
        }
    }
}

static void
pokelink( struct checkctl * link )
{
    gboolean active;

    active = gtk_toggle_button_get_active( link->check );
    active = ( link->enables ? active : !active );
    gtk_widget_set_sensitive( link->wids[0], active );
    gtk_widget_set_sensitive( link->wids[1], active );
}

static void
addwidget( TrPrefs * self, int id, GtkTable * table, int off,
           GtkTooltips * tips, struct checkctl ** links )
{
    GType       type;
    GtkWidget * add1, * add2;

    g_assert( ALEN( defs ) > id );

    type = PTYPE( id );
    add1 = NULL;
    add2 = NULL;
    if( G_TYPE_BOOLEAN == type )
    {
        addwid_bool( self, id, tips, &add1, links );
    }
    else if( G_TYPE_INT == type )
    {
        addwid_int( self, id, tips, &add1, &add2 );
    }
    else if( GTK_TYPE_FILE_CHOOSER == type )
    {
        addwid_file( self, id, tips, &add1, &add2 );
    }
    else if( GTK_TYPE_COMBO_BOX == type )
    {
        addwid_combo( self, id, tips, &add1, &add2 );
    }
    else
    {
        g_assert_not_reached();
    }

    g_assert( NULL != add1 );
    filllinks( id, add1, add2, links );
    if( NULL == add2 )
    {
        gtk_table_attach_defaults( table, add1, 0, 2, off, off + 1 );
    }
    else
    {
        gtk_table_attach_defaults( table, add1, 0, 1, off, off + 1 );
        gtk_table_attach_defaults( table, add2, 1, 2, off, off + 1 );
    }
    if( PR_DISABLED == defs[id].status )
    {
        gtk_widget_set_sensitive( add1, FALSE );
        if( NULL != add2 )
        {
            gtk_widget_set_sensitive( add2, FALSE );
        }
    }
}

/* wrap a widget in an event box with a tooltip */
static GtkWidget *
tipbox( GtkWidget * widget, GtkTooltips * tips, const char * tip )
{
    GtkWidget * box;

    box = gtk_event_box_new();
    gtk_container_add( GTK_CONTAINER( box ), widget );
    gtk_tooltips_set_tip( tips, box, tip, "" );

    return box;
}

static void
addwid_bool( TrPrefs * self, int id, GtkTooltips * tips,
             GtkWidget ** wid1, struct checkctl ** links )
{
    GtkWidget  * check;
    gboolean     active;

    g_assert( ALEN( defs ) > id && G_TYPE_BOOLEAN == PTYPE( id ) );
    check = gtk_check_button_new_with_mnemonic( gettext( defs[id].label ) );
    gtk_tooltips_set_tip( tips, check, gettext( defs[id].tip ), "" );
    if( 0 > defs_bool[id].link )
    {
        g_assert( NULL == links[id] );
    }
    else
    {
        links[id]->check = GTK_TOGGLE_BUTTON( check );
        links[id]->enables = defs_bool[id].enables;
        g_object_set_data_full( G_OBJECT( check ), PREF_CHECK_LINK,
                                links[id], g_free );
    }
    active = tr_prefs_get_bool_with_default( id );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( check ), active );
    SETPREFID( check, id );
    g_signal_connect( check, "clicked", G_CALLBACK( checkclick ), self );

    *wid1 = check;
}

static void
checkclick( GtkWidget * widget, gpointer data )
{
    TrPrefs         * self;
    struct checkctl * link;
    int               id;
    gboolean          active;

    TR_IS_PREFS( data );
    self = TR_PREFS( data );
    link = g_object_get_data( G_OBJECT( widget ), PREF_CHECK_LINK );
    GETPREFID( widget, id );

    if( NULL != link )
    {
        pokelink( link );
    }

    active = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget ) );
    savepref( self, id, ( active ? "yes" : "no" ) );
}

static void
addwid_int( TrPrefs * self, int id, GtkTooltips * tips,
            GtkWidget ** wid1, GtkWidget ** wid2 )
{
    GtkWidget * spin, * label;
    int         val, * last;

    g_assert( ALEN( defs ) > id && G_TYPE_INT == PTYPE( id ) );
    spin = gtk_spin_button_new_with_range( defs_int[id].min,
                                           defs_int[id].max, 1 );
    label = gtk_label_new_with_mnemonic( gettext( defs[id].label ) );
    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), spin );
    gtk_misc_set_alignment( GTK_MISC( label ), 0, .5 );
    gtk_spin_button_set_numeric( GTK_SPIN_BUTTON( spin ), TRUE );
    gtk_tooltips_set_tip( tips, spin, gettext( defs[id].tip ), "" );
    val = tr_prefs_get_int_with_default( id );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( spin ), val );
    last = g_new( int, 1 );
    *last = val;
    g_object_set_data_full( G_OBJECT( spin ), PREF_SPIN_LAST, last, g_free );
    SETPREFID( spin, id );
    /* I don't trust that focus-out-event will always work,
       so save pref on widget destruction too */
    g_signal_connect( spin, "focus-out-event", G_CALLBACK( spinfocus ), self );
    g_signal_connect( spin, "destroy", G_CALLBACK( spindie ), self );

    *wid1 = tipbox( label, tips, gettext( defs[id].tip ) );
    *wid2 = spin;
}

static gboolean
spinfocus( GtkWidget * widget, GdkEventFocus *event SHUTUP, gpointer data )
{
    TrPrefs * self;
    int     * last, id, cur;
    char    * str;

    TR_IS_PREFS( data );
    self = TR_PREFS( data );
    last = g_object_get_data( G_OBJECT( widget ), PREF_SPIN_LAST );
    GETPREFID( widget, id );
    cur = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON( widget ) );

    if( cur != *last )
    {
        str = g_strdup_printf( "%i", cur );
        savepref( self, id, str );
        g_free( str );
        *last = cur;
    }

    /* continue propagating the event */
    return FALSE;
}

static void
spindie( GtkWidget * widget, gpointer data )
{
    spinfocus( widget, NULL, data );
}

static void
addwid_file( TrPrefs * self, int id, GtkTooltips * tips,
             GtkWidget ** wid1, GtkWidget ** wid2 )
{
    GtkWidget  * file, * label;
    const char * pref;

    g_assert( ALEN( defs ) > id && GTK_TYPE_FILE_CHOOSER == PTYPE( id ) );
    file = gtk_file_chooser_button_new( gettext( defs_file[id].title ),
                                        defs_file[id].act );
    label = gtk_label_new_with_mnemonic( gettext( defs[id].label ) );
    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), file );
    gtk_misc_set_alignment( GTK_MISC( label ), 0, .5 );
    pref = tr_prefs_get( id );
    if( NULL == pref )
    {
        pref = defs_file[id].getdef();
    }
    gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( file ), pref );
    SETPREFID( file, id );
    g_signal_connect( file, "selection-changed",
                      G_CALLBACK( filechosen ), self );

    *wid1 = tipbox( label, tips, gettext( defs[id].tip ) );
    *wid2 = tipbox( file,  tips, gettext( defs[id].tip ) );
}

static void
filechosen( GtkWidget * widget, gpointer data )
{
    TrPrefs    * self;
    const char * dir;
    int          id;

    TR_IS_PREFS( data );
    self = TR_PREFS( data );
    dir = gtk_file_chooser_get_current_folder( GTK_FILE_CHOOSER( widget ) );
    GETPREFID( widget, id );
    savepref( self, id, dir );
}

static GtkTreeModel *
makecombomodel( void )
{
    GtkListStore * list;
    GtkTreeIter    iter;

    /* create the model used by the two popup menus */
    list = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_INT );
    gtk_list_store_append( list, &iter );
    gtk_list_store_set( list, &iter, 1, 0, 0,
                        _("Use the torrent file where it is"), -1 );
    gtk_list_store_append( list, &iter );
    gtk_list_store_set( list, &iter, 1, TR_TORNEW_SAVE_COPY, 0,
                        _("Keep a copy of the torrent file"), -1 );
    gtk_list_store_append( list, &iter );
    gtk_list_store_set( list, &iter, 1, TR_TORNEW_SAVE_MOVE, 0,
                        _("Keep a copy and remove the original"), -1 );

    return GTK_TREE_MODEL( list );
}

static void
addwid_combo( TrPrefs * self, int id, GtkTooltips * tips,
              GtkWidget ** wid1, GtkWidget ** wid2 )
{
    GtkWidget       * combo, * label;
    GtkCellRenderer * rend;
    GtkTreeIter       iter;
    guint             prefsflag, modelflag;

    g_assert( ALEN( defs ) > id && GTK_TYPE_COMBO_BOX == PTYPE( id ) );
    combo = gtk_combo_box_new();
    label = gtk_label_new_with_mnemonic( gettext( defs[id].label ) );
    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), combo );
    gtk_misc_set_alignment( GTK_MISC( label ), 0, .5 );
    gtk_combo_box_set_model( GTK_COMBO_BOX( combo ), self->combomodel );
    rend = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( combo ), rend, TRUE );
    gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT( combo ), rend, "text", 0 );

    prefsflag = addactionflag( tr_prefs_get( id ) );
    if( gtk_tree_model_get_iter_first( self->combomodel, &iter ) )
    {
        do
        {
            gtk_tree_model_get( self->combomodel, &iter, 1, &modelflag, -1 );
            if( modelflag == prefsflag)
            {
                gtk_combo_box_set_active_iter( GTK_COMBO_BOX( combo ), &iter );
                break;
            }
        }
        while( gtk_tree_model_iter_next( self->combomodel, &iter ) );
    }
    SETPREFID( combo, id );
    g_signal_connect( combo, "changed", G_CALLBACK( combochosen ), self );

    *wid1 = tipbox( label, tips, gettext( defs[id].tip ) );
    *wid2 = tipbox( combo, tips, gettext( defs[id].tip ) );
}

static void
combochosen( GtkWidget * widget, gpointer data )
{
    TrPrefs      * self;
    GtkTreeIter    iter;
    GtkTreeModel * model;
    guint          flags;
    int            id;

    TR_IS_PREFS( data );
    self = TR_PREFS( data );
    if( gtk_combo_box_get_active_iter( GTK_COMBO_BOX( widget ), &iter ) )
    {
        model = gtk_combo_box_get_model( GTK_COMBO_BOX( widget ) );
        gtk_tree_model_get( model, &iter, 1, &flags, -1 );
        GETPREFID( widget, id );
        savepref( self, id, addactionname( flags ) );
    }
}

static void
savepref( TrPrefs * self, int id, const char * val )
{
    const char   * name, * old;
    char         * errstr;
    TrPrefsClass * class;

    name = tr_prefs_name( id );
    old = cf_getpref( name );
    if( NULL == old )
    {
        if( old == val )
        {
            return;
        }
    }
    else
    {
        if( 0 == strcmp( old, val ) )
        {
            return;
        }
    }
    cf_setpref( name, val );

    /* write prefs to disk */
    cf_saveprefs( &errstr );
    if( NULL != errstr )
    {
        errmsg( GTK_WINDOW( self ), "%s", errstr );
        g_free( errstr );
    }

    /* signal a pref change */
    class = g_type_class_peek( TR_PREFS_TYPE );
    g_signal_emit( self, class->changesig, 0, id );
}
