/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#include <stdarg.h>
#include <stdlib.h> /* free() */
#include <string.h> /* strcmp() */

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libevent/evhttp.h>

#include <libtransmission/transmission.h> /* TR_RATIO_NA, TR_RATIO_INF */

#include "conf.h"
#include "tr-prefs.h"
#include "util.h"

int
tr_strcmp( const char * a, const char * b )
{
    if( a && b ) return strcmp( a, b );
    if( a ) return 1;
    if( b ) return -1;
    return 0;
}

char*
tr_strlratio( char * buf, double ratio, size_t buflen )
{
    if( (int)ratio == TR_RATIO_NA )
        g_strlcpy( buf, _( "None" ), buflen );
    else if( (int)ratio == TR_RATIO_INF )
        g_strlcpy( buf, "\xE2\x88\x9E", buflen );
    else if( ratio < 10.0 )
        g_snprintf( buf, buflen, "%.2f", ratio );
    else if( ratio < 100.0 )
        g_snprintf( buf, buflen, "%.1f", ratio );
    else
        g_snprintf( buf, buflen, "%.0f", ratio );
    return buf;
}

char*
tr_strlsize( char * buf, guint64 size, size_t buflen )
{
    if( !size )
        g_strlcpy( buf, _( "None" ), buflen );
    else {
        static const char *units[] = {
            N_("B"), N_("KiB"), N_("MiB"), N_("GiB"), N_("TiB"),
            N_("PiB"), N_("EiB"), N_("ZiB"), N_("YiB")
        };
        unsigned int i;
        double small = size;
        for( i=0; i<G_N_ELEMENTS(units) && (small>=1024.0); ++i )
            small /= 1024.0;
        if( i < 2 ) /* B & KiB */
            g_snprintf( buf, buflen, "%d %s", (int)small, _(units[i]) );
        else
            g_snprintf( buf, buflen, "%.1f %s", small, _(units[i]) );
    }
    return buf;
}

char*
tr_strlspeed( char * buf, double KiBps, size_t buflen )
{
    const guint64 bps = KiBps * 1024;
    if( !bps )
        g_strlcpy( buf, _( "None" ), buflen );
    else {
        char bbuf[64];
        tr_strlsize( bbuf, (guint64)(KiBps*1024), sizeof(bbuf) );
        g_snprintf( buf, buflen, _("%s/s"), bbuf );
    }
    return buf;
}

#define SECONDS(s)              ((s) % 60)
#define MINUTES(s)              ((s) / 60 % 60)
#define HOURS(s)                ((s) / 60 / 60 % 24)
#define DAYS(s)                 ((s) / 60 / 60 / 24 % 7)

char*
tr_strltime( char * buf, int secs, size_t buflen )
{
    if( secs < 60 )
    {
        g_snprintf( buf, buflen, _( "%i %s" ),
                    SECONDS(secs), ngettext("sec", "secs", SECONDS(secs)));
    }
    else if( secs < 60*60 )
    {
        g_snprintf( buf, buflen, _("%i %s %i %s"),
                    MINUTES(secs), ngettext("min", "mins", MINUTES(secs)),
                    SECONDS(secs), ngettext("sec", "secs", SECONDS(secs)));
    }
    else if( secs < 60*60*24 )
    {
        g_snprintf( buf, buflen, _("%i %s %i %s"),
                    HOURS(secs),   ngettext("hr", "hrs", HOURS(secs)),
                    MINUTES(secs), ngettext("min", "mins", MINUTES(secs)));
    }
    else
    {
        g_snprintf( buf, buflen, _("%i %s %i %s"),
                    DAYS(secs),  ngettext("day", "days", DAYS(secs)),
                    HOURS(secs), ngettext("hr", "hrs", HOURS(secs)));
    }

    return buf;
}


char *
rfc822date (guint64 epoch_msec)
{
    const time_t secs = epoch_msec / 1000;
    const struct tm tm = *localtime (&secs);
    char buf[128];
    strftime( buf, sizeof(buf), "%a, %d %b %Y %T %Z", &tm );
    return g_locale_to_utf8( buf, -1, NULL, NULL, NULL );
}

gboolean
mkdir_p( const char * path, mode_t mode )
{
#if GLIB_CHECK_VERSION( 2, 8, 0)
    return !g_mkdir_with_parents( path, mode );
#else
    return !tr_mkdirp( path, mode );
#endif
}

GList *
dupstrlist( GList * l )
{
    GList * ret = NULL;
    for( ; l!=NULL; l=l->next )
        ret = g_list_prepend( ret, g_strdup( l->data ) );
    return g_list_reverse( ret );
}

char *
joinstrlist(GList *list, char *sep)
{
  GList *l;
  GString *gstr = g_string_new (NULL);
  for (l=list; l!=NULL; l=l->next) {
    g_string_append (gstr, (char*)l->data);
    if (l->next != NULL)
      g_string_append (gstr, (sep));
  }
  return g_string_free (gstr, FALSE);
}

void
freestrlist(GList *list)
{
  g_list_foreach (list, (GFunc)g_free, NULL);
  g_list_free (list);
}

char *
decode_uri( const char * uri )
{
    char * filename = evhttp_decode_uri( uri );
    char * ret = g_strdup( filename );
    free( filename );
    return ret;
}

GList *
checkfilenames( int argc, char **argv )
{
    int i;
    GList * ret = NULL;
    char * pwd = g_get_current_dir( );

    for( i=0; i<argc; ++i )
    {
        char * filename = g_path_is_absolute( argv[i] )
            ? g_strdup ( argv[i] )
            : g_build_filename( pwd, argv[i], NULL );

        if( g_file_test( filename, G_FILE_TEST_EXISTS ) )
            ret = g_list_append( ret, filename );
        else
            g_free( filename );
    }

    g_free( pwd );
    return ret;
}

char *
getdownloaddir( void )
{
    static char * wd = NULL;
    char * dir = pref_string_get( PREF_KEY_DIR_DEFAULT );
    if ( dir == NULL ) {
        if( wd == NULL )
            wd = g_get_current_dir();
        dir = g_strdup( wd );
    }
    return dir;
}

/**
 * don't use more than 50% the height of the screen, nor 80% the width.
 * but don't be too small, either -- set the minimums to 500 x 300
 */
void
sizingmagic( GtkWindow         * wind,
             GtkScrolledWindow * scroll,
             GtkPolicyType       hscroll,
             GtkPolicyType       vscroll )
{
    int width;
    int height;
    GtkRequisition req;

    GdkScreen * screen = gtk_widget_get_screen( GTK_WIDGET( wind ) );

    gtk_scrolled_window_set_policy( scroll, GTK_POLICY_NEVER,
                                            GTK_POLICY_NEVER );

    gtk_widget_size_request( GTK_WIDGET( wind ), &req );
    req.height = MAX( req.height, 300 );
    height = MIN( req.height, gdk_screen_get_height( screen ) / 5 * 4 );

    gtk_scrolled_window_set_policy( scroll, GTK_POLICY_NEVER, vscroll );
    gtk_widget_size_request( GTK_WIDGET( wind ), &req );
    req.width = MAX( req.width, 500 );
    width = MIN( req.width, gdk_screen_get_width( screen ) / 2 );

    gtk_window_set_default_size( wind, width, height );
    gtk_scrolled_window_set_policy( scroll, hscroll, vscroll );
}

static void
errcb(GtkWidget *widget, int resp UNUSED, gpointer data) {
  GList *funcdata;
  callbackfunc_t func;

  if(NULL != data) {
    funcdata = g_list_first(data);
    func = (callbackfunc_t) funcdata->data;
    data = funcdata->next->data;
    func(data);
    g_list_free(funcdata);
  }

  gtk_widget_destroy(widget);
}

static GtkWidget *
verrmsg_full( GtkWindow * wind, callbackfunc_t func, void * data,
              const char * format, va_list ap )
{
  GtkWidget *dialog;
  char *msg;
  GList *funcdata;

  msg = g_strdup_vprintf(format, ap);

  if(NULL == wind)
    dialog = gtk_message_dialog_new(
      NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
  else
    dialog = gtk_message_dialog_new(wind,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);

  if(NULL == func)
    funcdata = NULL;
  else
      funcdata = g_list_append(g_list_append(NULL, (void *) func), data);
  g_signal_connect(dialog, "response", G_CALLBACK(errcb), funcdata);
  g_free(msg);

  return dialog;
}

void
errmsg( GtkWindow * wind, const char * format, ... )
{
    GtkWidget * dialog;
    va_list     ap;

    va_start( ap, format );
    dialog = verrmsg_full( wind, NULL, NULL, format, ap );
    va_end( ap );

    if( NULL != wind && !GTK_WIDGET_MAPPED( GTK_WIDGET( wind ) ) )
    {
        g_signal_connect_swapped( wind, "map",
                                  G_CALLBACK( gtk_widget_show ), dialog );
    }
    else
    {
        gtk_widget_show( dialog );
    }
}

GtkWidget *
errmsg_full( GtkWindow * wind, callbackfunc_t func, void * data,
             const char * format, ... )
{
    GtkWidget * dialog;
    va_list     ap;

    va_start( ap, format );
    dialog = verrmsg_full( wind, func, data, format, ap );
    va_end( ap );

    return dialog;
}

typedef void (PopupFunc)(GtkWidget*, GdkEventButton*); 

/* pop up the context menu if a user right-clicks.
   if the row they right-click on isn't selected, select it. */

gboolean
on_tree_view_button_pressed (GtkWidget       * view,
                             GdkEventButton  * event,
                             gpointer          func)
{
  GtkTreeView * tv = GTK_TREE_VIEW( view );

  if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
  {
    GtkTreeSelection * selection = gtk_tree_view_get_selection(tv);
    GtkTreePath *path;
    if (gtk_tree_view_get_path_at_pos (tv,
                                       (gint) event->x,
                                       (gint) event->y,
                                       &path, NULL, NULL, NULL))
    {
      if (!gtk_tree_selection_path_is_selected (selection, path))
      {
        gtk_tree_selection_unselect_all (selection);
        gtk_tree_selection_select_path (selection, path);
      }
      gtk_tree_path_free(path);
    }
   
    ((PopupFunc*)func)(view, event);

    return TRUE;
  }

  return FALSE;
}

gpointer
tr_object_ref_sink( gpointer object )
{
#if GLIB_CHECK_VERSION(2,10,0)
    g_object_ref_sink( object );
#else
    g_object_ref( object );
    gtk_object_sink( GTK_OBJECT( object ) );
#endif
    return object;
}
