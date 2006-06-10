/*
  $Id$

  Copyright (c) 2005-2006 Joshua Elsasser. All rights reserved.
   
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   
  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

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
  unsigned int ii;
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

guint
addactionflag(const char *action) {
  if(NULL == action)
    return TR_TORNEW_SAVE_COPY;
  else if(0 == strcmp("copy", action))
    return TR_TORNEW_SAVE_COPY;
  else if(0 == strcmp("move", action))
    return TR_TORNEW_SAVE_MOVE;
  else
    return 0;
}

const char *
addactionname(guint flag) {
  static char name[6];

  if(TR_TORNEW_SAVE_COPY & flag)
    strcpy(name, "copy");
  else if(TR_TORNEW_SAVE_MOVE & flag)
    strcpy(name, "move");
  else
    strcpy(name, "leave");

  return name;
}

GList *
makeglist(void *ptr, ...) {
  va_list ap;
  GList *ret;

  ret = g_list_append(NULL, ptr);

  va_start(ap, ptr);  
  while(NULL != (ptr = va_arg(ap, void*)))
    ret = g_list_append(ret, ptr);
  va_end(ap);

  return ret;
}

GtkWidget *
errmsg(GtkWindow *wind, const char *format, ...) {
  GtkWidget *dialog;
  va_list ap;

  va_start(ap, format);
  dialog = verrmsg(wind, NULL, NULL, format, ap);
  va_end(ap);

  return dialog;
}

GtkWidget *
errmsg_full(GtkWindow *wind, callbackfunc_t func, void *data,
            const char *format, ...) {
  GtkWidget *dialog;
  va_list ap;

  va_start(ap, format);
  dialog = verrmsg(wind, func, data, format, ap);
  va_end(ap);

  return dialog;
}

GtkWidget *
verrmsg(GtkWindow *wind, callbackfunc_t func, void *data,
        const char *format, va_list ap) {
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
  if(NULL != wind)
    gtk_widget_show(dialog);
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
