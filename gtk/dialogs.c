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

struct fileswind
{
    GtkWidget    * widget;
    TrTorrent    * tor;
    GtkTreeModel * model;
    guint          timer;
};

static void
autoclick(GtkWidget *widget, gpointer gdata);
static void
dirclick(GtkWidget *widget, gpointer gdata);
static void
addresp(GtkWidget *widget, gint resp, gpointer gdata);
static void
promptresp( GtkWidget * widget, gint resp, gpointer data );
static void
quitresp( GtkWidget * widget, gint resp, gpointer data );
static void
stylekludge( GObject * obj, GParamSpec * spec, gpointer data );
static void
setscroll( void * arg );
static void
fileswindresp( GtkWidget * widget, gint resp, gpointer data );
static void
filestorclosed( gpointer data, GObject * tor );
static void
parsepath( GtkTreeStore * store, GtkTreeIter * ret,
           const char * path, int index, uint64_t size );
static uint64_t
getdirtotals( GtkTreeStore * store, GtkTreeIter * parent );
static gboolean
fileswindupdate( gpointer data );
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
      gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( getdir ), pref );
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

#define INFOLINE(tab, ii, nam, val) \
  do { \
    char *txt = g_markup_printf_escaped("<b>%s</b>", nam); \
    GtkWidget *wid = gtk_label_new(NULL); \
    gtk_misc_set_alignment(GTK_MISC(wid), 0, .5); \
    gtk_label_set_markup(GTK_LABEL(wid), txt); \
    gtk_table_attach_defaults(GTK_TABLE(tab), wid, 0, 1, ii, ii + 1); \
    wid = gtk_label_new(val); \
    gtk_label_set_selectable(GTK_LABEL(wid), TRUE); \
    gtk_misc_set_alignment(GTK_MISC(wid), 0, .5); \
    gtk_table_attach_defaults(GTK_TABLE(tab), wid, 1, 2, ii, ii + 1); \
    ii++; \
    g_free(txt); \
  } while(0)

#define INFOLINEF(tab, ii, fmt, nam, val) \
  do { \
    char *buf = g_strdup_printf(fmt, val); \
    INFOLINE(tab, ii, nam, buf); \
    g_free(buf); \
  } while(0)

#define INFOLINEA(tab, ii, nam, val) \
  do { \
    char *buf = val; \
    INFOLINE(tab, ii, nam, buf); \
    g_free(buf); \
  } while(0)

#define INFOSEP(tab, ii) \
  do { \
    GtkWidget *wid = gtk_hseparator_new(); \
    gtk_table_attach_defaults(GTK_TABLE(tab), wid, 0, 2, ii, ii + 1); \
    ii++; \
  } while(0)

void
makeinfowind(GtkWindow *parent, TrTorrent *tor) {
  tr_stat_t *sb;
  tr_info_t *in;
  GtkWidget *wind, *label;
  int ii;
  char *str;
  const int rowcount = 15;
  GtkWidget *table = gtk_table_new(rowcount, 2, FALSE);

  /* XXX should use model and update this window regularly */

  sb = tr_torrent_stat(tor);
  in = tr_torrent_info(tor);
  str = g_strdup_printf(_("%s Properties"), in->name);
  wind = gtk_dialog_new_with_buttons(str, parent,
    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
    GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
  g_free(str);

  gtk_widget_set_name(wind, "TransmissionDialog");
  gtk_table_set_col_spacings(GTK_TABLE(table), 12);
  gtk_table_set_row_spacings(GTK_TABLE(table), 12);
  gtk_dialog_set_default_response(GTK_DIALOG(wind), GTK_RESPONSE_ACCEPT);
  gtk_container_set_border_width(GTK_CONTAINER(table), 6);
  gtk_window_set_resizable(GTK_WINDOW(wind), FALSE);

  label = gtk_label_new(NULL);
  gtk_label_set_selectable(GTK_LABEL(label), TRUE);
  str = g_markup_printf_escaped("<big>%s</big>", in->name);
  gtk_label_set_markup(GTK_LABEL(label), str);
  g_free(str);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 2, 0, 1);

  ii = 1;

  INFOSEP(table, ii);

  if(80 == sb->tracker->port)
    INFOLINEA(table, ii, _("Tracker:"), g_strdup_printf("http://%s",
              sb->tracker->address));
  else
    INFOLINEA(table, ii, _("Tracker:"), g_strdup_printf("http://%s:%i",
              sb->tracker->address, sb->tracker->port));
  INFOLINE(table, ii, _("Announce:"), sb->tracker->announce);
  INFOLINEA(table, ii, _("Piece Size:"), readablesize(in->pieceSize));
  INFOLINEF(table, ii, "%i", _("Pieces:"), in->pieceCount);
  INFOLINEA(table, ii, _("Total Size:"), readablesize(in->totalSize));
  if(0 > sb->seeders)
    INFOLINE(table, ii, _("Seeders:"), _("?"));
  else
    INFOLINEF(table, ii, "%i", _("Seeders:"), sb->seeders);
  if(0 > sb->leechers)
    INFOLINE(table, ii, _("Leechers:"), _("?"));
  else
    INFOLINEF(table, ii, "%i", _("Leechers:"), sb->leechers);
  if(0 > sb->completedFromTracker)
    INFOLINE(table, ii, _("Completed:"), _("?"));
  else
    INFOLINEF(table, ii, "%i", _("Completed:"), sb->completedFromTracker);

  INFOSEP(table, ii);

  INFOLINE(table, ii, _("Directory:"), tr_torrentGetFolder(tr_torrent_handle(tor)));
  INFOLINEA(table, ii, _("Downloaded:"), readablesize(sb->downloaded));
  INFOLINEA(table, ii, _("Uploaded:"), readablesize(sb->uploaded));

  INFOSEP(table, ii);

  g_assert(rowcount == ii);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(wind)->vbox), table);
  g_signal_connect(G_OBJECT(wind), "response",
                   G_CALLBACK(gtk_widget_destroy), NULL);
  gtk_widget_show_all(wind);
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

void
makefileswind( GtkWindow * parent, TrTorrent * tor )
{
    GType cols[] =
    {
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_FLOAT,
        G_TYPE_STRING, G_TYPE_INT, G_TYPE_UINT64
    };
    tr_info_t         * inf;
    GtkTreeStore      * store;
    int                 ii;
    GtkWidget         * view, * scroll, * frame, * wind;
    GtkCellRenderer   * rend;
    GtkTreeViewColumn * col;
    GtkTreeSelection  * sel;
    char              * label;
    struct fileswind  * fw;

    g_assert( ALEN( cols ) == FC__MAX );

    /* set up the model */
    inf   = tr_torrent_info( tor );
    store = gtk_tree_store_newv( FC__MAX, cols );
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
    gtk_tree_view_column_set_title( col, _("File") );
    /* add icon renderer */
    rend = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start( col, rend, FALSE );
    gtk_tree_view_column_add_attribute( col, rend, "stock-id", FC_STOCK );
    /* add text renderer */
    rend = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start( col, rend, TRUE );
    gtk_tree_view_column_add_attribute( col, rend, "markup", FC_LABEL );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    /* add progress column */
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title( col, _("Progress") );
    rend = tr_cell_renderer_progress_new();
    /* this string is only used to determing the size of the progress bar */
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

    /* create the window */
    label = g_strdup_printf( _("%s - Files for %s"),
                             g_get_application_name(), inf->name );
    wind = gtk_dialog_new_with_buttons( label, parent,
                                        GTK_DIALOG_DESTROY_WITH_PARENT |
                                        GTK_DIALOG_NO_SEPARATOR, 
                                        GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                        NULL );
    g_free( label );
    gtk_dialog_set_default_response( GTK_DIALOG( wind ), GTK_RESPONSE_ACCEPT );
    gtk_window_set_resizable( GTK_WINDOW( wind ), TRUE );
    gtk_box_pack_start_defaults( GTK_BOX( GTK_DIALOG( wind )->vbox ), frame );

    /* set up the callback data */
    fw         = g_new0( struct fileswind, 1 );
    fw->widget = wind;
    fw->tor    = tor;
    fw->model  = GTK_TREE_MODEL( store );
    fw->timer  = g_timeout_add( UPDATE_INTERVAL, fileswindupdate, fw );

    g_object_weak_ref( G_OBJECT( tor ), filestorclosed, fw );
    g_signal_connect( wind, "response", G_CALLBACK( fileswindresp ), fw );
    fileswindupdate( fw );

    /* show the window with a nice initial size */
    windowsizehack( wind, scroll, view, setscroll, scroll );
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
setscroll( void * arg )
{
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( arg ),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );
}

static void
fileswindresp( GtkWidget * widget SHUTUP, gint resp SHUTUP, gpointer data )
{
    struct fileswind * fw = data;

    g_object_weak_unref( G_OBJECT( fw->tor ), filestorclosed, fw );
    filestorclosed( fw, G_OBJECT( fw->tor ) );
}

static void
filestorclosed( gpointer data, GObject * tor SHUTUP )
{
    struct fileswind * fw = data;

    g_source_remove( fw->timer );
    g_object_unref( fw->model );
    gtk_widget_destroy( fw->widget );
    g_free( fw );
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
fileswindupdate( gpointer data )
{
    struct fileswind * fw;
    tr_info_t        * inf;
    float            * progress;

    fw       = data;
    inf      = tr_torrent_info( fw->tor );
    progress = tr_torrentCompletion( tr_torrent_handle( fw->tor ) );
    updateprogress( fw->model, NULL, inf->totalSize, progress );
    free( progress );

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
