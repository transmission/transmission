/*
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

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "conf.h"
#include "dialogs.h"
#include "transmission.h"
#include "util.h"

struct prefdata {
  GtkSpinButton *port;
  GtkCheckButton *uselimit;
  GtkSpinButton *limit;
  GtkFileChooser *dir;
  GtkWindow *parent;
  tr_handle_t *tr;
};

struct addcb {
  add_torrent_func_t addfunc;
  GtkWindow *parent;
  tr_handle_t *tr;
  torrents_added_func_t donefunc;
  void *donedata;
  gboolean autostart;
  gboolean usingaltdir;
  GtkFileChooser *altdir;
  GtkButtonBox *altbox;
};

static void
windclosed(GtkWidget *widget SHUTUP, gpointer gdata);
static void
clicklimitbox(GtkWidget *widget, gpointer gdata);
static void
clickdialog(GtkWidget *widget, int resp, gpointer gdata);
static void
autoclick(GtkWidget *widget, gpointer gdata);
static void
dirclick(GtkWidget *widget, gpointer gdata);
static void
addresp(GtkWidget *widget, gint resp, gpointer gdata);

void
makeprefwindow(GtkWindow *parent, tr_handle_t *tr, gboolean *opened) {
  char *title = g_strdup_printf(_("%s Preferences"), g_get_application_name());
  GtkWidget *wind = gtk_dialog_new_with_buttons(title, parent,
   GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
   GTK_STOCK_APPLY, GTK_RESPONSE_APPLY, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
   GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
  GtkWidget *table = gtk_table_new(4, 2, FALSE);
  GtkWidget *portnum = gtk_spin_button_new_with_range(1, 0xffff, 1);
  GtkWidget *limitbox = gtk_check_button_new_with_mnemonic(
    _("_Limit upload speed"));
  GtkWidget *limitnum = gtk_spin_button_new_with_range(0, G_MAXLONG, 1);
  GtkWidget *dirstr = gtk_file_chooser_button_new(
    _("Choose download directory"),
    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  GtkWidget *label;
  GtkWidget **array;
  const char *pref;
  struct prefdata *data = g_new0(struct prefdata, 1);

  *opened = TRUE;

  g_free(title);
  gtk_widget_set_name(wind, "TransmissionDialog");
  gtk_table_set_col_spacings(GTK_TABLE(table), 12);
  gtk_table_set_row_spacings(GTK_TABLE(table), 12);
  gtk_dialog_set_default_response(GTK_DIALOG(wind), GTK_RESPONSE_OK);
  gtk_container_set_border_width(GTK_CONTAINER(table), 6);
  gtk_window_set_resizable(GTK_WINDOW(wind), FALSE);

  data->port = GTK_SPIN_BUTTON(portnum);
  data->uselimit = GTK_CHECK_BUTTON(limitbox);
  data->limit = GTK_SPIN_BUTTON(limitnum);
  data->dir = GTK_FILE_CHOOSER(dirstr);
  data->parent = parent;
  data->tr = tr;

  /* limit checkbox */
  pref = cf_getpref(PREF_USELIMIT);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(limitbox),
    (NULL == pref ? TRUE : strbool(pref)));
  array = g_new(GtkWidget*, 2);
  g_signal_connect_data(limitbox, "clicked", G_CALLBACK(clicklimitbox),
                        array, (GClosureNotify)g_free, 0);
  gtk_table_attach_defaults(GTK_TABLE(table), limitbox,         0, 2, 0, 1);

  /* limit label and entry */
  label = gtk_label_new_with_mnemonic(_("Maximum _upload speed:"));
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), limitnum);
  gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
  pref = cf_getpref(PREF_LIMIT);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(limitnum), TRUE);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(limitnum),
    (NULL == pref ? DEFAULT_UPLIMIT : strtol(pref,NULL,10)));
  gtk_table_attach_defaults(GTK_TABLE(table), label,            0, 1, 1, 2);
  gtk_table_attach_defaults(GTK_TABLE(table), limitnum,         1, 2, 1, 2);
  array[0] = label;
  array[1] = limitnum;
  clicklimitbox(limitbox, array);

  /* directory label and chooser */
  label = gtk_label_new_with_mnemonic(_("_Download directory:"));
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), dirstr);
  gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
  if(NULL != (pref = cf_getpref(PREF_DIR)))
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dirstr), pref);
  gtk_table_attach_defaults(GTK_TABLE(table), label,            0, 1, 2, 3);
  gtk_table_attach_defaults(GTK_TABLE(table), dirstr,           1, 2, 2, 3);

  /* port label and entry */
  label = gtk_label_new_with_mnemonic(_("Listening _port:"));
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), portnum);
  gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
  pref = cf_getpref(PREF_PORT);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(portnum), TRUE);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(portnum),
    (NULL == pref ? TR_DEFAULT_PORT : strtol(pref, NULL, 10)));
  gtk_table_attach_defaults(GTK_TABLE(table), label,            0, 1, 3, 4);
  gtk_table_attach_defaults(GTK_TABLE(table), portnum,          1, 2, 3, 4);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(wind)->vbox), table);
  g_signal_connect_data(wind, "response", G_CALLBACK(clickdialog),
                        data, (GClosureNotify)g_free, 0);
  g_signal_connect(wind, "destroy", G_CALLBACK(windclosed), opened);
  gtk_widget_show_all(wind);
}

static void
windclosed(GtkWidget *widget SHUTUP, gpointer gdata) {
  gboolean *preachy_gcc = gdata;
  
  *preachy_gcc = FALSE;
}

static void
clicklimitbox(GtkWidget *widget, gpointer gdata) {
  GtkWidget **widgets = gdata;
  int ii;

  for(ii = 0; 2 > ii; ii++)
    gtk_widget_set_sensitive(widgets[ii],
      gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}

static void
clickdialog(GtkWidget *widget, int resp, gpointer gdata) {
  struct prefdata *data = gdata;
  int intval;
  const char *strval;
  char *strnum, *errstr;
  gboolean boolval;

  if(GTK_RESPONSE_APPLY == resp || GTK_RESPONSE_OK == resp) {
    /* check directory */
    if(NULL != (strval = gtk_file_chooser_get_current_folder(data->dir))) {
      if(!mkdir_p(strval, 0777)) {
        errmsg(data->parent,
               _("Failed to create the directory %s:\n%s"),
               strval, strerror(errno));
        return;
      }

      /* save dir pref */
      cf_setpref(PREF_DIR, strval);
    }

    /* save port pref */
    strnum = g_strdup_printf("%i",
      gtk_spin_button_get_value_as_int(data->port));
    cf_setpref(PREF_PORT, strnum);
    g_free(strnum);

    /* save uselimit pref */
    boolval = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->uselimit));
    cf_setpref(PREF_USELIMIT, (boolval ? "yes" : "no"));

    /* save limit pref */
    intval = gtk_spin_button_get_value_as_int(data->limit);
    strnum = g_strdup_printf("%i", intval);
    cf_setpref(PREF_LIMIT, strnum);

    /* save prefs */
    if(!cf_saveprefs(&errstr)) {
      errmsg(data->parent, "%s", errstr);
      g_free(strnum);
      g_free(errstr);
    }

    tr_setBindPort(data->tr, gtk_spin_button_get_value_as_int(data->port));
    setlimit(data->tr);
  }

  if(GTK_RESPONSE_APPLY != resp)
    gtk_widget_destroy(widget);
}

void
setlimit(tr_handle_t *tr) {
  const char *pref;

  if(NULL != (pref = cf_getpref(PREF_USELIMIT)) && !strbool(pref))
    tr_setUploadLimit(tr, -1);
  else if(NULL != (pref = cf_getpref(PREF_LIMIT)))
    tr_setUploadLimit(tr, strtol(pref, NULL, 10));
  else
    tr_setUploadLimit(tr, DEFAULT_UPLIMIT);
}

void
makeaddwind(add_torrent_func_t addfunc, GtkWindow *parent, tr_handle_t *tr,
            torrents_added_func_t donefunc, void *donedata) {
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
  const char *pref;

  data->addfunc = addfunc;
  data->parent = parent;
  data->tr = tr;
  data->donefunc = donefunc;
  data->donedata = donedata;
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
  if(NULL != (pref = cf_getpref(PREF_DIR)))
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(getdir), pref);

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
  gboolean added = FALSE;
  char *dir = NULL;

  if(GTK_RESPONSE_ACCEPT == resp) {
    if(data->usingaltdir)
      dir = gtk_file_chooser_get_filename(data->altdir);
    files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(widget));
    for(ii = files; NULL != ii; ii = ii->next)
      if(data->addfunc(data->tr, data->parent, ii->data, dir,
                       !data->autostart, NULL))
        added = TRUE;
    if(added)
      data->donefunc(data->donedata);
    if(NULL != dir)
      g_free(dir);
  }

  gtk_widget_destroy(widget);
}

#define INFOLINE(tab, ii, nam, val) \
  do { \
    char *txt = g_markup_printf_escaped("<b>%s</b>", nam); \
    GtkWidget *wid = gtk_label_new(NULL); \
    gtk_misc_set_alignment(GTK_MISC(wid), 1, .5); \
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
makeinfowind(GtkWindow *parent, tr_handle_t *tr, int id) {
  tr_stat_t *sb;
  GtkWidget *wind, *label;
  int ii;
  char *str;
  const int rowcount = 12;
  GtkWidget *table = gtk_table_new(rowcount, 2, FALSE);

  /* XXX would be nice to be able to stat just one */
  if(id >= tr_torrentStat(tr, &sb))
    assert(!"XXX ");
  str = g_strdup_printf(_("%s Properties"), sb[id].info.name);
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
  str = g_markup_printf_escaped("<big>%s</big>", sb[id].info.name);
  gtk_label_set_markup(GTK_LABEL(label), str);
  g_free(str);
  gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 2, 0, 1);

  ii = 1;

  INFOSEP(table, ii);

  if(80 == sb[id].info.trackerPort)
    INFOLINEA(table, ii, _("Tracker:"), g_strdup_printf("http://%s",
              sb[id].info.trackerAddress));
  else
    INFOLINEA(table, ii, _("Tracker:"), g_strdup_printf("http://%s:%i",
              sb[id].info.trackerAddress, sb[id].info.trackerPort));
  INFOLINE(table, ii, _("Announce:"), sb[id].info.trackerAnnounce);
  INFOLINEA(table, ii, _("Piece Size:"), readablesize(sb[id].info.pieceSize, 1));
  INFOLINEF(table, ii, "%i", _("Pieces:"), sb[id].info.pieceCount);
  INFOLINEA(table, ii, _("Total Size:"), readablesize(sb[id].info.totalSize, 1));

  INFOSEP(table, ii);

  INFOLINE(table, ii, _("Directory:"), sb[id].folder);
  INFOLINEA(table, ii, _("Downloaded:"), readablesize(sb[id].downloaded, 1));
  INFOLINEA(table, ii, _("Uploaded:"), readablesize(sb[id].uploaded, 1));

  INFOSEP(table, ii);

  assert(rowcount == ii);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(wind)->vbox), table);
  g_signal_connect(G_OBJECT(wind), "response",
                   G_CALLBACK(gtk_widget_destroy), NULL);
  gtk_widget_show_all(wind);
  free(sb);
}
