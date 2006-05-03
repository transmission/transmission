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
  GtkCheckButton *use_dlimit;
  GtkSpinButton *dlimit;
  GtkCheckButton *use_ulimit;
  GtkSpinButton *ulimit;
  GtkFileChooser *dir;
  GtkWindow *parent;
  TrBackend *back;
};

struct addcb {
  add_torrents_func_t addfunc;
  GtkWindow *parent;
  void *data;
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
freedata(gpointer gdata, GClosure *closure);
static void
clickdialog(GtkWidget *widget, int resp, gpointer gdata);
static void
autoclick(GtkWidget *widget, gpointer gdata);
static void
dirclick(GtkWidget *widget, gpointer gdata);
static void
addresp(GtkWidget *widget, gint resp, gpointer gdata);

void
makeprefwindow(GtkWindow *parent, TrBackend *back, gboolean *opened) {
  char *title = g_strdup_printf(_("%s Preferences"), g_get_application_name());
  GtkWidget *wind = gtk_dialog_new_with_buttons(title, parent,
   GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
   GTK_STOCK_APPLY, GTK_RESPONSE_APPLY, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
   GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
  const unsigned int rowcount = 6;
  GtkWidget *table = gtk_table_new(rowcount, 2, FALSE);
  GtkWidget *portnum = gtk_spin_button_new_with_range(1, 0xffff, 1);
  GtkWidget *dirstr = gtk_file_chooser_button_new(
    _("Choose a download directory"),
    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  GtkWidget *label;
  GtkWidget **array;
  const char *pref;
  struct prefdata *data = g_new0(struct prefdata, 1);
  struct { GtkWidget *on; GtkWidget *num; GtkWidget *label; gboolean defuse;
    const char *usepref; const char *numpref; long def; } lim[] = {
    { gtk_check_button_new_with_mnemonic(_("_Limit download speed")),
      gtk_spin_button_new_with_range(0, G_MAXLONG, 1),
      gtk_label_new_with_mnemonic(_("Maximum _download speed:")),
      DEF_USEDOWNLIMIT, PREF_USEDOWNLIMIT, PREF_DOWNLIMIT, DEF_DOWNLIMIT, },
    { gtk_check_button_new_with_mnemonic(_("Li_mit upload speed")),
      gtk_spin_button_new_with_range(0, G_MAXLONG, 1),
      gtk_label_new_with_mnemonic(_("Maximum _upload speed:")),
      DEF_USEUPLIMIT, PREF_USEUPLIMIT, PREF_UPLIMIT, DEF_UPLIMIT, },
  };
  unsigned int ii;

  *opened = TRUE;

  g_free(title);
  gtk_widget_set_name(wind, "TransmissionDialog");
  gtk_table_set_col_spacings(GTK_TABLE(table), 8);
  gtk_table_set_row_spacings(GTK_TABLE(table), 8);
  gtk_dialog_set_default_response(GTK_DIALOG(wind), GTK_RESPONSE_OK);
  gtk_container_set_border_width(GTK_CONTAINER(table), 6);
  gtk_window_set_resizable(GTK_WINDOW(wind), FALSE);

  data->port = GTK_SPIN_BUTTON(portnum);
  data->use_dlimit = GTK_CHECK_BUTTON(lim[0].on);
  data->dlimit = GTK_SPIN_BUTTON(lim[0].num);
  data->use_ulimit = GTK_CHECK_BUTTON(lim[1].on);
  data->ulimit = GTK_SPIN_BUTTON(lim[1].num);
  data->dir = GTK_FILE_CHOOSER(dirstr);
  data->parent = parent;
  data->back = back;
  g_object_ref(G_OBJECT(back));

#define RN(n) (n), (n) + 1

  for(ii = 0; ii < ALEN(lim); ii++) {
    /* limit checkbox */
    pref = cf_getpref(lim[ii].usepref);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lim[ii].on),
      (NULL == pref ? lim[ii].defuse : strbool(pref)));
    array = g_new(GtkWidget*, 2);
    g_signal_connect_data(lim[ii].on, "clicked", G_CALLBACK(clicklimitbox),
                          array, (GClosureNotify)g_free, 0);
    gtk_table_attach_defaults(GTK_TABLE(table), lim[ii].on,    0, 2, RN(ii*2));

    /* limit label and entry */
    gtk_label_set_mnemonic_widget(GTK_LABEL(lim[ii].label), lim[ii].num);
    gtk_misc_set_alignment(GTK_MISC(lim[ii].label), 0, .5);
    pref = cf_getpref(lim[ii].numpref);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(lim[ii].num), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lim[ii].num),
      (NULL == pref ? lim[ii].def : strtol(pref, NULL, 10)));
    gtk_table_attach_defaults(GTK_TABLE(table), lim[ii].label, 0,1,RN(ii*2+1));
    gtk_table_attach_defaults(GTK_TABLE(table), lim[ii].num,   1,2,RN(ii*2+1));
    array[0] = lim[ii].label;
    array[1] = lim[ii].num;
    clicklimitbox(lim[ii].on, array);
  }
  ii *= 2;

  /* directory label and chooser */
  label = gtk_label_new_with_mnemonic(_("Download di_rectory:"));
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), dirstr);
  gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
  if(NULL != (pref = cf_getpref(PREF_DIR)))
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dirstr), pref);
  gtk_table_attach_defaults(GTK_TABLE(table), label,           0, 1, RN(ii));
  gtk_table_attach_defaults(GTK_TABLE(table), dirstr,          1, 2, RN(ii));
  ii++;

  /* port label and entry */
  label = gtk_label_new_with_mnemonic(_("Listening _port:"));
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), portnum);
  gtk_misc_set_alignment(GTK_MISC(label), 0, .5);
  pref = cf_getpref(PREF_PORT);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(portnum), TRUE);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(portnum),
    (NULL == pref ? TR_DEFAULT_PORT : strtol(pref, NULL, 10)));
  gtk_table_attach_defaults(GTK_TABLE(table), label,           0, 1, RN(ii));
  gtk_table_attach_defaults(GTK_TABLE(table), portnum,         1, 2, RN(ii));
  ii++;

#undef RN
  assert(rowcount == ii);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(wind)->vbox), table);
  g_signal_connect_data(wind, "response", G_CALLBACK(clickdialog),
                        data, freedata, 0);
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
freedata(gpointer gdata, GClosure *closure SHUTUP) {
  struct prefdata *data = gdata;

  g_object_unref(G_OBJECT(data->back));
  g_free(data);
}

static void
clickdialog(GtkWidget *widget, int resp, gpointer gdata) {
  struct prefdata *data = gdata;
  int intval;
  const char *strval;
  char *strnum, *errstr;
  gboolean bval;

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

    /* save usedownlimit pref */
    bval = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->use_dlimit));
    cf_setpref(PREF_USEDOWNLIMIT, (bval ? "yes" : "no"));

    /* save downlimit pref */
    intval = gtk_spin_button_get_value_as_int(data->dlimit);
    strnum = g_strdup_printf("%i", intval);
    cf_setpref(PREF_DOWNLIMIT, strnum);

    /* save useuplimit pref */
    bval = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->use_ulimit));
    cf_setpref(PREF_USEUPLIMIT, (bval ? "yes" : "no"));

    /* save downlimit pref */
    intval = gtk_spin_button_get_value_as_int(data->ulimit);
    strnum = g_strdup_printf("%i", intval);
    cf_setpref(PREF_UPLIMIT, strnum);

    /* save prefs */
    cf_saveprefs(&errstr);
    if(NULL != errstr) {
      errmsg(data->parent, "%s", errstr);
      g_free(strnum);
      g_free(errstr);
    }

    /* XXX would be nice to have errno strings, are they printed to stdout? */
    tr_setBindPort(tr_backend_handle(data->back),
                   gtk_spin_button_get_value_as_int(data->port));
    setlimit(data->back);
  }

  if(GTK_RESPONSE_APPLY != resp)
    gtk_widget_destroy(widget);
}

void
setlimit(TrBackend *back) {
  struct { void (*func)(tr_handle_t*, int);
    const char *use; const char *num; gboolean defuse; long def; } lim[] = {
    {tr_setDownloadLimit, PREF_USEDOWNLIMIT, PREF_DOWNLIMIT,
                          DEF_USEDOWNLIMIT,  DEF_DOWNLIMIT},
    {tr_setUploadLimit,   PREF_USEUPLIMIT,   PREF_UPLIMIT,
                          DEF_USEUPLIMIT,    DEF_UPLIMIT},
  };
  const char *pref;
  unsigned int ii;
  tr_handle_t *tr = tr_backend_handle(back);

  for(ii = 0; ii < ALEN(lim); ii++) {
    pref = cf_getpref(lim[ii].use);
    if(!(NULL == pref ? lim[ii].defuse : strbool(pref)))
      lim[ii].func(tr, -1);
    else {
      pref = cf_getpref(lim[ii].num);
      lim[ii].func(tr, (NULL == pref ? lim[ii].def : strtol(pref, NULL, 10)));
    }
  }
}

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
  const char *pref;

  data->addfunc = addfunc;
  data->parent = parent;
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
  GList *stupidgtk;
  gboolean paused;
  char *dir;

  if(GTK_RESPONSE_ACCEPT == resp) {
    dir = NULL;
    if(data->usingaltdir)
      dir = gtk_file_chooser_get_filename(data->altdir);
    files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(widget));
    stupidgtk = NULL;
    for(ii = files; NULL != ii; ii = ii->next)
      stupidgtk = g_list_append(stupidgtk, ii->data);
    paused = !data->autostart;
    data->addfunc(data->data, NULL, stupidgtk, dir, &paused);
    if(NULL != dir)
      g_free(dir);
    g_slist_free(files);
    freestrlist(stupidgtk);
  }

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
  const int rowcount = 14;
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

  if(80 == in->trackerPort)
    INFOLINEA(table, ii, _("Tracker:"), g_strdup_printf("http://%s",
              in->trackerAddress));
  else
    INFOLINEA(table, ii, _("Tracker:"), g_strdup_printf("http://%s:%i",
              in->trackerAddress, in->trackerPort));
  INFOLINE(table, ii, _("Announce:"), in->trackerAnnounce);
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

  INFOSEP(table, ii);

  INFOLINE(table, ii, _("Directory:"), tr_torrentGetFolder(tr_torrent_handle(tor)));
  INFOLINEA(table, ii, _("Downloaded:"), readablesize(sb->downloaded));
  INFOLINEA(table, ii, _("Uploaded:"), readablesize(sb->uploaded));

  INFOSEP(table, ii);

  assert(rowcount == ii);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(wind)->vbox), table);
  g_signal_connect(G_OBJECT(wind), "response",
                   G_CALLBACK(gtk_widget_destroy), NULL);
  gtk_widget_show_all(wind);
}
