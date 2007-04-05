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
#include "dialogs.h"
#include "tr_cell_renderer_progress.h"
#include "tr_icon.h"
#include "tr_prefs.h"
#include "util.h"

#include "transmission.h"

#define UPDATE_INTERVAL         1000
#define PREFNAME                "transmission-dialog-pref-name"
#define FILESWIND_EXTRA_INDENT  4

#define STRIPROOT( path )                                                     \
    ( g_path_is_absolute( (path) ) ? g_path_skip_root( (path) ) : (path) )

struct addcb {
  add_torrents_func_t addfunc;
  void *data;
  gboolean autostart;
  gboolean usingaltdir;
  GtkFileChooser *altdir;
  GtkButtonBox *altbox;
};

struct dirdata
{
    add_torrents_func_t addfunc;
    void              * cbdata;
    GList             * files;
    guint               flags;
};

struct quitdata
{
    callbackfunc_t func;
    void         * cbdata;
};

struct infowind
{
    GtkWidget             * widget;
    TrTorrent             * tor;
    int64_t                 size;
    GtkTreeModel          * model;
    GtkTreeRowReference   * row;
    GtkTreeModel          * filesmodel;
    guint                   timer;
    struct
    {
        tr_tracker_info_t * track;
        GtkLabel          * trackwid;
        GtkLabel          * annwid;
        GtkLabel          * scrwid;
        int                 seed;
        GtkLabel          * seedwid;
        int                 leech;
        GtkLabel          * leechwid;
        int                 done;
        GtkLabel          * donewid;
        uint64_t            up;
        GtkLabel          * upwid;
        uint64_t            down;
        GtkLabel          * downwid;
        uint64_t            left;
        GtkLabel          * leftwid;
    }                       inf;
};

static void
autoclick(GtkWidget *widget, gpointer gdata);
static void
dirclick(GtkWidget *widget, gpointer gdata);
static void
addresp(GtkWidget *widget, gint resp, gpointer gdata);
static GtkWidget *
makeinfotab( TrTorrent * tor, struct infowind * iw );
static void
infoupdate( struct infowind * iw, int force );
void
fmtpeercount( GtkLabel * label, int count );
static void
promptresp( GtkWidget * widget, gint resp, gpointer data );
static void
quitresp( GtkWidget * widget, gint resp, gpointer data );
GtkWidget *
makefilestab( TrTorrent * tor, GtkTreeModel ** modelret );
static void
stylekludge( GObject * obj, GParamSpec * spec, gpointer data );
static void
infowinddead( GtkWidget * widget, gpointer data );
static void
infotorclosed( gpointer data, GObject * tor );
static void
parsepath( GtkTreeStore * store, GtkTreeIter * ret,
           const char * path, int index, uint64_t size );
static uint64_t
getdirtotals( GtkTreeStore * store, GtkTreeIter * parent );
static gboolean
infowindupdate( gpointer data );
static float
updateprogress( GtkTreeModel * model, GtkTreeIter * parent,
                uint64_t total, float * progress );

void
makeaddwind(GtkWindow *parent, add_torrents_func_t addfunc, void *cbdata) {
  GtkWidget *wind = gtk_file_chooser_dialog_new(_("Add a Torrent"), parent,
    GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
  struct addcb *data = g_new(struct addcb, 1);
  GtkWidget *vbox = gtk_vbox_new(FALSE, 3);
  GtkWidget *bbox = gtk_hbutton_box_new();
  GtkWidget *autocheck = gtk_check_button_new_with_mnemonic(
    _("Automatically _start torrent"));
  GtkWidget *dircheck = gtk_check_button_new_with_mnemonic(
    _("Use alternate _download directory"));
  GtkFileFilter *filter = gtk_file_filter_new();
  GtkFileFilter *unfilter = gtk_file_filter_new();
  GtkWidget *getdir = gtk_file_chooser_button_new(
    _("Choose a download directory"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  const char * pref;

  data->addfunc = addfunc;
  data->data = cbdata;
  data->autostart = TRUE;
  data->usingaltdir = FALSE;
  data->altdir = GTK_FILE_CHOOSER(getdir);
  data->altbox = GTK_BUTTON_BOX(bbox);

  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_EDGE);
  gtk_box_pack_start_defaults(GTK_BOX(bbox), dircheck);
  gtk_box_pack_start_defaults(GTK_BOX(bbox), getdir);

  gtk_box_pack_start_defaults(GTK_BOX(vbox), autocheck);
  gtk_box_pack_start_defaults(GTK_BOX(vbox), bbox);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autocheck), TRUE);
  pref = tr_prefs_get( PREF_ID_DIR );
  if( NULL != pref )
  {
      gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( wind ), pref );
  }

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dircheck), FALSE);
  gtk_widget_set_sensitive(getdir, FALSE);

  gtk_file_filter_set_name(filter, _("Torrent files"));
  gtk_file_filter_add_pattern(filter, "*.torrent");
  gtk_file_filter_set_name(unfilter, _("All files"));
  gtk_file_filter_add_pattern(unfilter, "*");

  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(wind), filter);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(wind), unfilter);
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(wind), TRUE);
  gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(wind), vbox);

  g_signal_connect(G_OBJECT(autocheck), "clicked", G_CALLBACK(autoclick),data);
  g_signal_connect(G_OBJECT(dircheck), "clicked", G_CALLBACK(dirclick), data);
  g_signal_connect(G_OBJECT(wind), "response", G_CALLBACK(addresp), data);

  gtk_widget_show_all(wind);
}

static void
autoclick(GtkWidget *widget, gpointer gdata) {
  struct addcb *data = gdata;

  data->autostart = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void
dirclick(GtkWidget *widget, gpointer gdata) {
  struct addcb *data = gdata;

  data->usingaltdir = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  gtk_widget_set_sensitive(GTK_WIDGET(data->altdir), data->usingaltdir);
}

static void
addresp(GtkWidget *widget, gint resp, gpointer gdata) {
  struct addcb *data = gdata;
  GSList *files, *ii;
  GList *stupidgtk;
  int flags;
  char *dir;

  if(GTK_RESPONSE_ACCEPT == resp) {
    dir = NULL;
    if(data->usingaltdir)
      dir = gtk_file_chooser_get_filename(data->altdir);
    files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(widget));
    stupidgtk = NULL;
    for(ii = files; NULL != ii; ii = ii->next)
      stupidgtk = g_list_append(stupidgtk, ii->data);
    flags = ( data->autostart ? TR_TORNEW_RUNNING : TR_TORNEW_PAUSED );
    flags |= addactionflag( tr_prefs_get( PREF_ID_ADDSTD ) );
    data->addfunc( data->data, NULL, stupidgtk, dir, flags );
    if(NULL != dir)
      g_free(dir);
    g_slist_free(files);
    freestrlist(stupidgtk);
  }

  g_free( data );
  gtk_widget_destroy(widget);
}

void
makeinfowind( GtkWindow * parent, GtkTreeModel * model, GtkTreePath * path,
              TrTorrent * tor )
{
    struct infowind   * iw;
    GtkWidget         * wind, * box, * label, * sep, * tabs, * page;
    tr_info_t         * inf;
    char              * name, * size;

    iw   = g_new0( struct infowind, 1 );
    inf  = tr_torrent_info( tor );
    name = g_strdup_printf( _("%s - Properties for %s"),
                            g_get_application_name(), inf->name );
    wind = gtk_dialog_new_with_buttons( name, parent,
                                        GTK_DIALOG_DESTROY_WITH_PARENT |
                                        GTK_DIALOG_NO_SEPARATOR,
                                        GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                        NULL );
    g_free( name );
    gtk_dialog_set_default_response( GTK_DIALOG( wind ), GTK_RESPONSE_ACCEPT );
    gtk_window_set_resizable( GTK_WINDOW( wind ), TRUE );
    gtk_window_set_role( GTK_WINDOW( wind ), "tr-info" );
    gtk_widget_set_name( wind, "TransmissionDialog" );
    box = GTK_DIALOG( wind )->vbox;

    /* add label with file name and size */
    label = gtk_label_new( NULL );
    size = readablesize( inf->totalSize );
    name = g_markup_printf_escaped( "<big>%s (%s)</big>", inf->name, size );
    free( size );
    gtk_label_set_markup( GTK_LABEL( label ), name );
    g_free( name );
    gtk_label_set_selectable( GTK_LABEL( label ), TRUE );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( box ), label, FALSE, FALSE, 6 );

    /* add separator */
    sep = gtk_hseparator_new();
    gtk_widget_show( sep );
    gtk_box_pack_start( GTK_BOX( box ), sep, FALSE, FALSE, 6 );

    /* add tab bar */
    tabs = gtk_notebook_new();
    gtk_widget_show( tabs );
    gtk_box_pack_start( GTK_BOX( box ), tabs, TRUE, TRUE, 6 );

    /* add general tab */
    label = gtk_label_new( _("General") );
    gtk_widget_show( label );
    page = makeinfotab( tor, iw );
    gtk_notebook_append_page( GTK_NOTEBOOK( tabs ), page, label );

    /* add files tab */
    label = gtk_label_new( _("Files") );
    gtk_widget_show( label );
    /* XXX should use sizingmagic() here */
    page = makefilestab( tor, &iw->filesmodel );
    gtk_notebook_append_page( GTK_NOTEBOOK( tabs ), page, label );

    /* set up the callback data */
    iw->widget     = wind;
    iw->tor        = tor;
    iw->size       = inf->totalSize;
    iw->model      = model;
    iw->row        = gtk_tree_row_reference_new( model, path );
    iw->timer      = g_timeout_add( UPDATE_INTERVAL, infowindupdate, iw );

    g_object_ref( model );
    g_object_weak_ref( G_OBJECT( tor ), infotorclosed, iw );
    g_signal_connect( wind, "destroy", G_CALLBACK( infowinddead ), iw );
    g_signal_connect( wind, "response", G_CALLBACK( gtk_widget_destroy ), 0 );
    infoupdate( iw, 1 );
    infowindupdate( iw );

    gtk_widget_show( wind );
}

#define INFOLINE( tab, ii, nam, val )                                         \
    do                                                                        \
    {                                                                         \
        char     * txt = g_markup_printf_escaped( "<b>%s</b>", (nam) );       \
        GtkWidget * wid = gtk_label_new( NULL );                              \
        gtk_misc_set_alignment( GTK_MISC( wid ), 0, .5 );                     \
        gtk_label_set_markup( GTK_LABEL( wid ), txt );                        \
        gtk_table_attach( GTK_TABLE( (tab) ), wid, 0, 1, (ii), (ii) + 1,      \
                          GTK_FILL, GTK_FILL, 0, 0 );                         \
        gtk_label_set_selectable( GTK_LABEL( (val) ), TRUE );                 \
        gtk_misc_set_alignment( GTK_MISC( (val) ), 0, .5 );                   \
        gtk_table_attach( GTK_TABLE( (tab) ), (val), 1, 2, (ii), (ii) + 1,    \
                          GTK_FILL, GTK_FILL, 0, 0);                          \
        (ii)++;                                                               \
        g_free( txt );                                                        \
    } while( 0 )

#define INFOLINEF( tab, ii, fmt, nam, val )                                   \
    do                                                                        \
    {                                                                         \
        char      * buf = g_strdup_printf( fmt, val );                        \
        GtkWidget * lwid = gtk_label_new( buf );                              \
        g_free( buf );                                                        \
        INFOLINE( tab, ii, nam, lwid );                                       \
    } while( 0 )

#define INFOLINEW( tab, ii, nam, val )                                        \
    do                                                                        \
    {                                                                         \
        GtkWidget * lwid = gtk_label_new( (val) );                            \
        INFOLINE( (tab), (ii), (nam), lwid );                                 \
    } while( 0 )

#define INFOLINEA( tab, ii, nam, val )                                        \
    do                                                                        \
    {                                                                         \
        GtkWidget * lwid = gtk_label_new( (val) );                            \
        g_free( val );                                                        \
        INFOLINE( (tab), (ii), (nam), lwid );                                 \
    } while( 0 )

#define INFOLINEU( tab, ii, nam, stor )                                       \
    do                                                                        \
    {                                                                         \
        GtkWidget * lwid = gtk_label_new( NULL );                             \
        (stor) = GTK_LABEL( lwid );                                           \
        INFOLINE( (tab), (ii), (nam), lwid);                                  \
    } while( 0 )

#define INFOSEP( tab, ii )                                                    \
    do                                                                        \
    {                                                                         \
        GtkWidget * wid = gtk_hseparator_new();                               \
        gtk_table_attach( GTK_TABLE( (tab) ), wid, 0, 2, (ii), (ii) + 1,      \
                          GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0 );            \
        (ii)++;                                                               \
    } while( 0 )

GtkWidget *
makeinfotab( TrTorrent * tor, struct infowind * iw )
{
    const int   rowcount = 17;
    tr_info_t * inf;
    int         ii;
    GtkWidget * table;

    inf   = tr_torrent_info( tor );
    table = gtk_table_new( rowcount, 2, FALSE );
    gtk_table_set_col_spacings( GTK_TABLE( table ), 12 );
    gtk_table_set_row_spacings( GTK_TABLE( table ), 12 );
    gtk_container_set_border_width( GTK_CONTAINER( table ), 6 );

    ii = 0;

    INFOLINEU( table, ii, _("Tracker:"),      iw->inf.trackwid );
    INFOLINEU( table, ii, _("Announce:"),     iw->inf.annwid );
    INFOLINEU( table, ii, _("Scrape:"),       iw->inf.scrwid );
    INFOSEP(   table, ii );
    INFOLINEW( table, ii, _("Info Hash:"),    inf->hashString );
    INFOLINEA( table, ii, _("Piece Size:"),   readablesize( inf->pieceSize ) );
    INFOLINEF( table, ii, "%i", _("Pieces:"), inf->pieceCount );
    INFOLINEA( table, ii, _("Total Size:"),   readablesize( inf->totalSize ) );
    INFOSEP(   table, ii );
    INFOLINEU( table, ii, _("Seeders:"),      iw->inf.seedwid );
    INFOLINEU( table, ii, _("Leechers:"),     iw->inf.leechwid );
    INFOLINEU( table, ii, _("Completed:"),    iw->inf.donewid );
    INFOSEP(   table, ii );
    INFOLINEW( table, ii, _("Directory:"),
               tr_torrentGetFolder( tr_torrent_handle( tor ) ) );
    INFOLINEU( table, ii, _("Downloaded:"),   iw->inf.downwid );
    INFOLINEU( table, ii, _("Uploaded:"),     iw->inf.upwid );
    INFOLINEU( table, ii, _("Remaining:"),    iw->inf.leftwid );

    g_assert( rowcount == ii );

    gtk_widget_show_all( table );

    return table;
}

void
infoupdate( struct infowind * iw, int force )
{
    int                 seed, leech, done;
    uint64_t            up, down, left;
    tr_tracker_info_t * track;
    GtkTreePath       * path;
    GtkTreeIter         iter;
    char              * str;

    path = gtk_tree_row_reference_get_path( iw->row );
    if( NULL == path || !gtk_tree_model_get_iter( iw->model, &iter, path ) )
    {
        g_free( path );
        return;
    }
    gtk_tree_model_get( iw->model, &iter, MC_TRACKER, &track,
                        MC_SEED, &seed, MC_LEECH, &leech, MC_DONE, &done,
                        MC_DOWN, &down, MC_UP, &up, MC_LEFT, &left, -1 );

    if( track != iw->inf.track || force )
    {
        if( 80 == track->port )
        {
            str = g_strdup_printf( "http://%s", track->address );
        }
        else
        {
            str = g_strdup_printf( "http://%s:%i",
                                   track->address, track->port );
        }
        gtk_label_set_text( iw->inf.trackwid, str );
        g_free( str );
        gtk_label_set_text( iw->inf.annwid, track->announce );
        gtk_label_set_text( iw->inf.scrwid, track->scrape );
    }
    if( seed != iw->inf.seed || force )
    {
        fmtpeercount( iw->inf.seedwid, seed );
        iw->inf.seed = seed;
    }
    if( leech != iw->inf.leech || force )
    {
        fmtpeercount( iw->inf.leechwid, leech );
        iw->inf.leech = leech;
    }
    if( done != iw->inf.done || force )
    {
        fmtpeercount( iw->inf.donewid, done );
        iw->inf.done = done;
    }
    if( down != iw->inf.down || force )
    {
        str = readablesize( down );
        gtk_label_set_text( iw->inf.downwid, str );
        g_free( str );
        iw->inf.down = down;
    }
    if( up != iw->inf.up || force )
    {
        str = readablesize( up );
        gtk_label_set_text( iw->inf.upwid, str );
        g_free( str );
        iw->inf.up = up;
    }
    if( left != iw->inf.left || force )
    {
        str = readablesize( left );
        gtk_label_set_text( iw->inf.leftwid, str );
        g_free( str );
        iw->inf.left = left;
    }
}

void
fmtpeercount( GtkLabel * label, int count )
{
    char str[16];

    if( 0 > count )
    {
        gtk_label_set_text( label, _("?") );
    }
    else
    {
        snprintf( str, sizeof str, "%i", count );
        gtk_label_set_text( label, str );
    }
}

void
promptfordir( GtkWindow * parent, add_torrents_func_t addfunc, void *cbdata,
              GList * files, guint flags )
{
    struct dirdata * stuff;
    GtkWidget      * wind;

    stuff = g_new( struct dirdata, 1 );
    stuff->addfunc = addfunc;
    stuff->cbdata  = cbdata;
    stuff->files   = dupstrlist( files );
    stuff->flags   = flags;

    wind =  gtk_file_chooser_dialog_new( _("Choose a directory"), parent,
                                         GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                         NULL );
    gtk_file_chooser_set_local_only( GTK_FILE_CHOOSER( wind ), TRUE );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER( wind ), FALSE );
    gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( wind ),
                                   getdownloaddir() );

    g_signal_connect( G_OBJECT( wind ), "response",
                      G_CALLBACK( promptresp ), stuff );

    gtk_widget_show_all(wind);
}

static void
promptresp( GtkWidget * widget, gint resp, gpointer data )
{
    struct dirdata * stuff;
    char           * dir;

    stuff = data;

    if( GTK_RESPONSE_ACCEPT == resp )
    {
        dir = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( widget ) );
        /* it seems that we will always get a directory */
        g_assert( NULL != dir );
        stuff->addfunc( stuff->cbdata, NULL, stuff->files, dir, stuff->flags );
        g_free( dir );
    }

    freestrlist( stuff->files );
    g_free( stuff );
    gtk_widget_destroy( widget );
}

void
askquit( GtkWindow * parent, callbackfunc_t func, void * cbdata )
{
    struct quitdata * stuff;
    GtkWidget * wind;

    if( !tr_prefs_get_bool_with_default( PREF_ID_ASKQUIT ) )
    {
        func( cbdata );
        return;
    }

    stuff          = g_new( struct quitdata, 1 );
    stuff->func    = func;
    stuff->cbdata  = cbdata;

    wind = gtk_message_dialog_new( parent, GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                   _("Are you sure you want to quit %s?"),
                                   g_get_application_name() );
    gtk_dialog_set_default_response( GTK_DIALOG( wind ), GTK_RESPONSE_YES );
    g_signal_connect( G_OBJECT( wind ), "response",
                      G_CALLBACK( quitresp ), stuff );

    gtk_widget_show_all( wind );
}

static void
quitresp( GtkWidget * widget, gint resp, gpointer data )
{
    struct quitdata * stuff;

    stuff = data;

    if( GTK_RESPONSE_YES == resp )
    {
        stuff->func( stuff->cbdata );
    }

    g_free( stuff );
    gtk_widget_destroy( widget );
}

enum filescols
{
    FC_STOCK = 0, FC_LABEL, FC_PROG, FC_KEY, FC_INDEX, FC_SIZE, FC__MAX
};

GtkWidget *
makefilestab( TrTorrent * tor, GtkTreeModel ** modelret )
{
    GType cols[] =
    {
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_FLOAT,
        G_TYPE_STRING, G_TYPE_INT, G_TYPE_UINT64
    };
    tr_info_t         * inf;
    GtkTreeStore      * store;
    int                 ii;
    GtkWidget         * view, * scroll, * frame;
    GtkCellRenderer   * rend;
    GtkTreeViewColumn * col;
    GtkTreeSelection  * sel;
    char              * label;

    g_assert( ALEN( cols ) == FC__MAX );

    /* set up the model */
    inf       = tr_torrent_info( tor );
    store     = gtk_tree_store_newv( FC__MAX, cols );
    *modelret = GTK_TREE_MODEL( store );
    for( ii = 0; ii < inf->fileCount; ii++ )
    {
        parsepath( store, NULL, STRIPROOT( inf->files[ii].name ),
                   ii, inf->files[ii].length );
    }
    getdirtotals( store, NULL );
    gtk_tree_sortable_set_sort_column_id( GTK_TREE_SORTABLE( store ),
                                          FC_KEY, GTK_SORT_ASCENDING );

    /* create the view */
    view = gtk_tree_view_new_with_model( GTK_TREE_MODEL( store ) );
    /* add file column */
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand( col, TRUE );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_AUTOSIZE );
    gtk_tree_view_column_set_title( col, _("File") );
    /* add icon renderer */
    rend = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start( col, rend, FALSE );
    gtk_tree_view_column_add_attribute( col, rend, "stock-id", FC_STOCK );
    /* add text renderer */
    rend = gtk_cell_renderer_text_new();
    g_object_set( rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL );
    gtk_tree_view_column_pack_start( col, rend, TRUE );
    gtk_tree_view_column_add_attribute( col, rend, "markup", FC_LABEL );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    /* add progress column */
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title( col, _("Progress") );
    rend = tr_cell_renderer_progress_new();
    /* this string is only used to determine the size of the progress bar */
    label = g_markup_printf_escaped( "<small>%s</small>", _("  fnord    fnord  ") );
    g_object_set( rend, "show-text", FALSE, "bar-sizing", label, NULL );
    g_free( label );
    gtk_tree_view_column_pack_start( col, rend, FALSE );
    gtk_tree_view_column_add_attribute( col, rend, "progress", FC_PROG );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    /* XXX this shouldn't be necessary */
    g_signal_connect( view, "notify::style", G_CALLBACK( stylekludge ), rend );
    /* set up view */
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_selection_set_mode( sel, GTK_SELECTION_NONE );
    gtk_tree_view_expand_all( GTK_TREE_VIEW( view ) );
    gtk_tree_view_set_search_column( GTK_TREE_VIEW( view ), FC_LABEL );
    gtk_widget_show( view );

    /* create the scrolled window and stick the view in it */
    scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scroll ),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );
    gtk_container_add( GTK_CONTAINER( scroll ), view );
    gtk_widget_show( scroll );

    /* create a frame around the scroll to make it look a little better */
    frame = gtk_frame_new( NULL );
    gtk_frame_set_shadow_type( GTK_FRAME( frame ), GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( frame ), scroll );
    gtk_widget_show( frame );

    return frame;
}

/* kludge to have the progress bars notice theme changes */
static void
stylekludge( GObject * obj, GParamSpec * spec, gpointer data )
{
    TrCellRendererProgress * rend = TR_CELL_RENDERER_PROGRESS( data );

    if( 0 == strcmp( "style", spec->name ) )
    {
        tr_cell_renderer_progress_reset_style( rend );
        gtk_widget_queue_draw( GTK_WIDGET( obj ) );
    }
}

static void
infowinddead( GtkWidget * widget SHUTUP, gpointer data )
{
    struct infowind * iw = data;

    g_object_weak_unref( G_OBJECT( iw->tor ), infotorclosed, iw );
    infotorclosed( iw, G_OBJECT( iw->tor ) );
}

static void
infotorclosed( gpointer data, GObject * tor SHUTUP )
{
    struct infowind * iw = data;

    g_source_remove( iw->timer );
    g_object_unref( iw->filesmodel );
    g_object_unref( iw->model );
    gtk_tree_row_reference_free( iw->row );
    gtk_widget_destroy( iw->widget );
    g_free( iw );
}

static void
parsepath( GtkTreeStore * store, GtkTreeIter * ret,
           const char * path, int index, uint64_t size )
{
    GtkTreeModel * model;
    GtkTreeIter  * parent, start, iter;
    char         * file, * dir, * lower, * mykey, * modelkey;
    const char   * stock;

    model  = GTK_TREE_MODEL( store );
    parent = NULL;
    file   = g_path_get_basename( path );
    if( 0 != strcmp( file, path ) )
    {
        dir = g_path_get_dirname( path );
        parsepath( store, &start, dir, index, size );
        g_free( dir );
        parent = &start;
    }

    lower = g_utf8_casefold( file, -1 );
    mykey = g_utf8_collate_key( lower, -1 );
    if( gtk_tree_model_iter_children( model, &iter, parent ) )
    {
        do
        {
            gtk_tree_model_get( model, &iter, FC_KEY, &modelkey, -1 );
            if( NULL != modelkey && 0 == strcmp( mykey, modelkey ) )
            {
                goto done;
            }
        }
        while( gtk_tree_model_iter_next( model, &iter ) );
    }

    gtk_tree_store_append( store, &iter, parent );
    if( NULL == ret )
    {
        stock = GTK_STOCK_FILE;
    }
    else
    {
        stock = GTK_STOCK_DIRECTORY;
        size  = 0;
        index = -1;
    }
    gtk_tree_store_set( store, &iter, FC_INDEX, index, FC_LABEL, file,
                        FC_KEY, mykey, FC_STOCK, stock, FC_SIZE, size, -1 );
  done:
    g_free( mykey );
    g_free( lower );
    g_free( file );
    if( NULL != ret )
    {
        memcpy( ret, &iter, sizeof( iter ) );
    }
}

static uint64_t
getdirtotals( GtkTreeStore * store, GtkTreeIter * parent )
{
    GtkTreeModel * model;
    GtkTreeIter    iter;
    uint64_t       mysize, subsize;
    char         * sizestr, * name, * label;

    model  = GTK_TREE_MODEL( store );
    mysize = 0;
    if( gtk_tree_model_iter_children( model, &iter, parent ) )
    {
        do
        {
            if( gtk_tree_model_iter_has_child( model, &iter ) )
            {
                subsize = getdirtotals( store, &iter );
                gtk_tree_store_set( store, &iter, FC_SIZE, subsize, -1 );
            }
            else
            {
                gtk_tree_model_get( model, &iter, FC_SIZE, &subsize, -1 );
            }
            gtk_tree_model_get( model, &iter, FC_LABEL, &name, -1 );
            sizestr = readablesize( subsize );
            label = g_markup_printf_escaped( "<small>%s (%s)</small>",
                                             name, sizestr );
            g_free( sizestr );
            g_free( name );
            gtk_tree_store_set( store, &iter, FC_LABEL, label, -1 );
            g_free( label );
            mysize += subsize;
        }
        while( gtk_tree_model_iter_next( model, &iter ) );
    }

    return mysize;
}

static gboolean
infowindupdate( gpointer data )
{
    struct infowind  * iw;
    float            * progress;

    iw       = data;
    progress = tr_torrentCompletion( tr_torrent_handle( iw->tor ) );
    updateprogress( iw->filesmodel, NULL, iw->size, progress );
    free( progress );
    infoupdate( iw, 0 );

    return TRUE;
}

static float
updateprogress( GtkTreeModel * model, GtkTreeIter * parent,
                uint64_t total, float * progress )
{
    GtkTreeIter    iter;
    float          myprog, subprog;
    int            index;
    uint64_t       size;

    myprog = 0.0;
    if( gtk_tree_model_iter_children( model, &iter, parent ) )
    {
        do
        {
            if( gtk_tree_model_iter_has_child( model, &iter ) )
            {
                gtk_tree_model_get( model, &iter, FC_SIZE, &size, -1 );
                subprog = updateprogress( model, &iter, size, progress );
            }
            else
            {
                gtk_tree_model_get( model, &iter,
                                    FC_SIZE, &size, FC_INDEX, &index, -1 );
                g_assert( 0 <= index );
                subprog = progress[index];
            }
            gtk_tree_store_set( GTK_TREE_STORE( model ), &iter,
                                FC_PROG, subprog, -1 );
            myprog += subprog * ( total ? ( float )size / ( float )total : 1 );
        }
        while( gtk_tree_model_iter_next( model, &iter ) );
    }

    return myprog;
}
