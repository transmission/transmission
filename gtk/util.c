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

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "tr_prefs.h"
#include "tr_torrent.h"
#include "util.h"

#define BESTDECIMAL(d)          (10.0 > (d) ? 2 : (100.0 > (d) ? 1 : 0))

static void
errcb(GtkWidget *wind, int resp, gpointer data);

gboolean
strbool(const char *str) {
  switch(str[0]) {
    case 'y':
    case 'Y':
    case '1':
    case 'j':
    case 'e':
      return TRUE;
    default:
      if(0 == g_ascii_strcasecmp("on", str))
        return TRUE;
      break;
  }

  return FALSE;
}

static const char *sizestrs[] = {
  N_("B"), N_("KiB"), N_("MiB"), N_("GiB"), N_("TiB"), N_("PiB"), N_("EiB"),
};

char *
readablesize(guint64 size) {
  int ii;
  double small = size;

  for(ii = 0; ii + 1 < ALEN(sizestrs) && 1024.0 <= small / 1024.0; ii++)
    small /= 1024.0;

  if(1024.0 <= small) {
    small /= 1024.0;
    ii++;
  }

  return g_strdup_printf("%.*f %s", BESTDECIMAL(small), small,
                         gettext(sizestrs[ii]));
}

#define SECONDS(s)              ((s) % 60)
#define MINUTES(s)              ((s) / 60 % 60)
#define HOURS(s)                ((s) / 60 / 60 % 24)
#define DAYS(s)                 ((s) / 60 / 60 / 24 % 7)
#define WEEKS(s)                ((s) / 60 / 60 / 24 / 7)

char *
readabletime(int secs) {
  if(60 > secs)
    return g_strdup_printf(_("%i %s"),
      SECONDS(secs), ngettext("second", "seconds", SECONDS(secs)));
  else if(60 * 60 > secs)
    return g_strdup_printf(_("%i %s %i %s"),
      MINUTES(secs), ngettext("minute", "minutes", MINUTES(secs)),
      SECONDS(secs), ngettext("second", "seconds", SECONDS(secs)));
  else if(60 * 60 * 24 > secs)
    return g_strdup_printf(_("%i %s %i %s"),
      HOURS(secs),   ngettext("hour", "hours", HOURS(secs)),
      MINUTES(secs), ngettext("minute", "minutes", MINUTES(secs)));
  else if(60 * 60 * 24 * 7 > secs)
    return g_strdup_printf(_("%i %s %i %s"),
      DAYS(secs),    ngettext("day", "days", DAYS(secs)),
      HOURS(secs),   ngettext("hour", "hours", HOURS(secs)));
  else
    return g_strdup_printf(_("%i %s %i %s"),
      WEEKS(secs),   ngettext("week", "weeks", WEEKS(secs)),
      DAYS(secs),    ngettext("hour", "hours", DAYS(secs)));
}

char *
ratiostr(guint64 down, guint64 up) {
  double ratio;

  if(0 == up && 0 == down)
    return g_strdup(_("N/A"));

  if(0 == down)
    /* this is a UTF-8 infinity symbol */
    return g_strdup(_("\xE2\x88\x9E"));

  ratio = (double)up / (double)down;

  return g_strdup_printf("%.*f", BESTDECIMAL(ratio), ratio);
}

gboolean
mkdir_p(const char *name, mode_t mode) {
  struct stat sb;
  char *parent;
  gboolean ret;
  int oerrno;

  if(0 != stat(name, &sb)) {
    if(ENOENT != errno)
      return FALSE;
    parent = g_path_get_dirname(name);
    ret = mkdir_p(parent, mode);
    oerrno = errno;
    g_free(parent);
    errno = oerrno;
    return (ret ? (0 == mkdir(name, mode)) : FALSE);
  }

  if(!S_ISDIR(sb.st_mode)) {
    errno = ENOTDIR;
    return FALSE;
  }

  return TRUE;
}

GList *
dupstrlist( GList * list )
{
    GList * ii, * ret;

    ret = NULL;
    for( ii = g_list_first( list ); NULL != ii; ii = ii->next )
    {
        ret = g_list_append( ret, g_strdup( ii->data ) );
    }

    return ret;
}

char *
joinstrlist(GList *list, char *sep) {
  GList *ii;
  int len;
  char *ret, *dest;

  if(0 > (len = strlen(sep) * (g_list_length(list) - 1)))
    return NULL;

  for(ii = g_list_first(list); NULL != ii; ii = ii->next)
    len += strlen(ii->data);

  dest = ret = g_new(char, len + 1);

  for(ii = g_list_first(list); NULL != ii; ii = ii->next) {
    dest = g_stpcpy(dest, ii->data);
    if(NULL != ii->next)
      dest = g_stpcpy(dest, sep);
  }

  return ret;
}

void
freestrlist(GList *list) {
  GList *ii;

  if(NULL != list) {
    for(ii = g_list_first(list); NULL != ii; ii = ii->next)
      g_free(ii->data);
    g_list_free(list);
  }
}

char *
urldecode(const char *str, int len) {
  int ii, jj;
  char *ret;
  char buf[3];

  if(0 >= len)
    len = strlen(str);

  for(ii = jj = 0; ii < len; ii++, jj++)
    if('%' == str[ii])
      ii += 2;

  ret = g_new(char, jj + 1);

  buf[2] = '\0';
  for(ii = jj = 0; ii < len; ii++, jj++) {
    switch(str[ii]) {
      case '%':
        if(ii + 2 < len) {
          buf[0] = str[ii+1];
          buf[1] = str[ii+2];
          ret[jj] = g_ascii_strtoull(buf, NULL, 16);
        }
        ii += 2;
        break;
      case '+':
        ret[jj] = ' ';
      default:
        ret[jj] = str[ii];
    }
  }
  ret[jj] = '\0';

  return ret;
}

GList *
checkfilenames(int argc, char **argv) {
  char *pwd = g_get_current_dir();
  int ii, cd;
  char *dirstr, *filestr;
  GList *ret = NULL;

  for(ii = 0; ii < argc; ii++) {
    dirstr = g_path_get_dirname(argv[ii]);
    if(!g_path_is_absolute(argv[ii])) {
      filestr = g_build_filename(pwd, dirstr, NULL);
      g_free(dirstr);
      dirstr = filestr;
    }
    cd = chdir(dirstr);
    g_free(dirstr);
    if(0 > cd)
      continue;
    dirstr = g_get_current_dir();
    filestr = g_path_get_basename(argv[ii]);
    ret = g_list_append(ret, g_build_filename(dirstr, filestr, NULL));
    g_free(dirstr);
    g_free(filestr);
  }

  chdir(pwd);
  g_free(pwd);

  return ret;
}

enum tr_torrent_action
toraddaction( const char * action )
{
    if( NULL == action || 0 == strcmp( "copy", action ) )
    {
        return TR_TOR_COPY;
    }
    else if( 0 == strcmp( "move", action ) )
    {
        return TR_TOR_MOVE;
    }
    else
    {
        return TR_TOR_LEAVE;
    }
}

const char *
toractionname( enum tr_torrent_action action )
{
    switch( action )
    {
        case TR_TOR_COPY:
            return "copy";
        case TR_TOR_MOVE:
            return "move";
        default:
            return "leave";
    }
}

const char *
getdownloaddir( void )
{
    static char * wd = NULL;
    const char  * dir;

    dir = tr_prefs_get( PREF_ID_DIR );
    if( NULL == dir )
    {
        if( NULL == wd )
        {
            wd = g_new( char, MAX_PATH_LENGTH + 1 );
            if( NULL == getcwd( wd, MAX_PATH_LENGTH + 1 ) )
            {
                snprintf( wd, MAX_PATH_LENGTH + 1, "." );
            }
        }
        dir = wd;
    }

    return dir;
}

void
sizingmagic( GtkWindow * wind, GtkScrolledWindow * scroll,
             GtkPolicyType hscroll, GtkPolicyType vscroll )
{
    GtkRequisition req;
    GdkScreen    * screen;
    int            width, height;

    screen = gtk_widget_get_screen( GTK_WIDGET( wind ) );

    gtk_scrolled_window_set_policy( scroll, GTK_POLICY_NEVER,
                                    GTK_POLICY_NEVER );

    gtk_widget_size_request( GTK_WIDGET( wind ), &req );
    height = MIN( req.height, gdk_screen_get_height( screen ) / 5 * 4 );

    gtk_scrolled_window_set_policy( scroll, GTK_POLICY_NEVER, vscroll );

    gtk_widget_size_request( GTK_WIDGET( wind ), &req );
    width = MIN( req.width, gdk_screen_get_width( screen ) / 2 );

    gtk_window_set_default_size( wind, width, height );

    gtk_scrolled_window_set_policy( scroll, hscroll, vscroll );
}

struct action *
action_new( int id, int flags, const char * label, const char * stock )
{
    struct action * act;

    act        = g_new0( struct action, 1 );
    act->id    = id;
    act->flags = flags;
    act->label = g_strdup( label );
    act->stock = g_strdup( stock );
    act->tool  = NULL;
    act->menu  = NULL;

    return act;
}

void
action_free( struct action * act )
{
    g_free( act->label );
    g_free( act->stock );
    g_free( act );
}

GtkWidget *
action_maketool( struct action * act, const char * key,
                 GCallback func, gpointer data )
{
    GtkToolItem * item;

    item = gtk_tool_button_new_from_stock( act->stock );
    if( NULL != act->label )
    {
        gtk_tool_button_set_label( GTK_TOOL_BUTTON( item ), act->label );
    }
    g_object_set_data( G_OBJECT( item ), key, act );
    g_signal_connect( item, "clicked", func, data );
    gtk_widget_show( GTK_WIDGET( item ) );

    return GTK_WIDGET( item );
}

GtkWidget *
action_makemenu( struct action * act, const char * actkey,
                 GtkAccelGroup * accel, const char * path, guint keyval,
                 GCallback func, gpointer data )
{
    GtkWidget  * item, * label;
    GdkModifierType mod;
    GtkStockItem stock;
    const char * name;
    char       * joined;

    mod = GDK_CONTROL_MASK;
    name = act->label;
    if( NULL == act->stock )
    {
        item = gtk_menu_item_new_with_label( act->label );
    }
    else
    {
        item = gtk_image_menu_item_new_from_stock( act->stock, NULL );
        if( NULL == act->label )
        {
            if( gtk_stock_lookup( act->stock, &stock ) )
            {
                name = stock.label;
                if( 0 == keyval )
                {
                    keyval = stock.keyval;
                    mod    = stock.modifier;
                }
            }
        }
        else
        {
            label = gtk_bin_get_child( GTK_BIN( item ) );
            gtk_label_set_text( GTK_LABEL( label ), act->label );
            
        }
    }

    if( NULL != accel && 0 < keyval && NULL != name )
    {
        joined = g_strjoin( "/", path, name, NULL );
        gtk_accel_map_add_entry( joined, keyval, mod );
        gtk_widget_set_accel_path( item, joined, accel );
        g_free( joined );
    }
    g_object_set_data( G_OBJECT( item ), actkey, act );
    g_signal_connect( item, "activate", func, data );
    gtk_widget_show( item );

    return item;
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

GtkWidget *
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
    funcdata = g_list_append(g_list_append(NULL, func), data);
  g_signal_connect(dialog, "response", G_CALLBACK(errcb), funcdata);
  g_free(msg);

  return dialog;
}

static void
errcb(GtkWidget *widget, int resp SHUTUP, gpointer data) {
  GList *funcdata;
  callbackfunc_t func;

  if(NULL != data) {
    funcdata = g_list_first(data);
    func = funcdata->data;
    data = funcdata->next->data;
    func(data);
    g_list_free(funcdata);
  }

  gtk_widget_destroy(widget);
}
