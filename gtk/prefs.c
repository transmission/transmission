/*
  Copyright (c) 2005 Joshua Elsasser. All rights reserved.
   
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "conf.h"
#include "prefs.h"
#include "transmission.h"
#include "util.h"

struct prefdata {
  GtkSpinButton *port;
  GtkCheckButton *uselimit;
  GtkSpinButton *limit;
  GtkEntry *dir;
  GtkWindow *parent;
  tr_handle_t *tr;
};

static void
clicklimitbox(GtkWidget *widget, gpointer gdata);
static void
clickdialog(GtkWidget *widget, int resp, gpointer gdata);

void
makeprefwindow(GtkWindow *parent, tr_handle_t *tr) {
  GtkWidget *wind = gtk_dialog_new_with_buttons("Preferences", parent,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK,
    GTK_RESPONSE_OK, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
  GtkWidget *table = gtk_table_new(4, 2, FALSE);
  GtkWidget *portnum = gtk_spin_button_new_with_range(1, 0xffff, 1);
  GtkWidget *limitbox = gtk_check_button_new_with_label("Limit upload speed");
  GtkWidget *limitnum = gtk_spin_button_new_with_range(0, G_MAXLONG, 1);
  GtkWidget *dirstr = gtk_entry_new();
  GtkWidget *label;
  const char *pref;
  struct prefdata *data = g_new0(struct prefdata, 1);

  data->port = GTK_SPIN_BUTTON(portnum);
  data->uselimit = GTK_CHECK_BUTTON(limitbox);
  data->limit = GTK_SPIN_BUTTON(limitnum);
  data->dir = GTK_ENTRY(dirstr);
  data->parent = parent;
  data->tr = tr;

  /* port label and entry */
  label = gtk_label_new("Listening port");
  if(NULL != (pref = cf_getpref(PREF_PORT)))
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(portnum), strtol(pref,NULL,10));
  else
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(portnum), TR_DEFAULT_PORT);
  gtk_table_attach_defaults(GTK_TABLE(table), label,            0, 1, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(table), portnum,          1, 2, 0, 1);

  /* limit checkbox */
  pref = cf_getpref(PREF_USELIMIT);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(limitbox),
    (NULL == pref ? FALSE : strbool(pref)));
  gtk_widget_set_sensitive(limitnum,
    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(limitbox)));
  g_signal_connect(G_OBJECT(limitbox), "clicked",
                   G_CALLBACK(clicklimitbox), limitnum);
  gtk_table_attach_defaults(GTK_TABLE(table), limitbox,         0, 2, 1, 2);

  /* limit label and entry */
  label = gtk_label_new("Maximum upload speed");
  if(NULL != (pref = cf_getpref(PREF_LIMIT)))
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(limitnum), strtol(pref,NULL,10));
  gtk_table_attach_defaults(GTK_TABLE(table), label,            0, 1, 2, 3);
  gtk_table_attach_defaults(GTK_TABLE(table), limitnum,         1, 2, 2, 3);

  /* directory label and entry */
  label = gtk_label_new("Download directory");
  if(NULL != (pref = cf_getpref(PREF_DIR)))
    gtk_entry_set_text(GTK_ENTRY(dirstr), pref);
  gtk_table_attach_defaults(GTK_TABLE(table), label,            0, 1, 3, 4);
  gtk_table_attach_defaults(GTK_TABLE(table), dirstr,           1, 2, 3, 4);

  gtk_box_pack_start_defaults(GTK_BOX(GTK_DIALOG(wind)->vbox), table);
  g_signal_connect(G_OBJECT(wind), "response", G_CALLBACK(clickdialog), data);
  gtk_widget_show_all(wind);
}

static void
clicklimitbox(GtkWidget *widget, gpointer gdata) {
  GtkWidget *entry = gdata;

  gtk_widget_set_sensitive(entry,
    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
}

static void
clickdialog(GtkWidget *widget, int resp, gpointer gdata) {
  struct prefdata *data = gdata;
  int intval;
  const char *strval;
  char *strnum;
  gboolean boolval;

  if(GTK_RESPONSE_OK == resp) {
    /* check directory */
    strval = gtk_entry_get_text(data->dir);
    if(!mkdir_p(strval, 0777)) {
      errmsg(data->parent, "Failed to create directory %s:\n%s",
             strval, strerror(errno));
      return;
    }

    /* save port pref */
    strnum = g_strdup_printf("%i",
      gtk_spin_button_get_value_as_int(data->port));
    cf_setpref(PREF_PORT, strnum, NULL);
    g_free(strnum);
    /* XXX should I change the port here?  is it even possible? */

    /* save uselimit pref */
    boolval = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->uselimit));
    cf_setpref(PREF_USELIMIT, (boolval ? "yes" : "no"), NULL);

    /* save limit pref */
    intval = gtk_spin_button_get_value_as_int(data->limit);
    strnum = g_strdup_printf("%i", intval);
    cf_setpref(PREF_LIMIT, strnum, NULL);
    g_free(strnum);

    setlimit(data->tr);

    /*
      note that prefs aren't written to disk unless we pass a pointer
      to an error string, so do this for the last call to cf_setpref()
    */
    /* save dir pref */
    if(!cf_setpref(PREF_DIR, gtk_entry_get_text(data->dir), &strnum)) {
      errmsg(data->parent, "%s", strnum);
      g_free(strnum);
      return;
    }
  }

  gtk_widget_destroy(widget);
  g_free(data);
}

void
setlimit(tr_handle_t *tr) {
  const char *pref;

  if(NULL == (pref = cf_getpref(PREF_USELIMIT)) || !strbool(pref))
    tr_setUploadLimit(tr, -1);
  else if(NULL != (pref = cf_getpref(PREF_LIMIT)))
    tr_setUploadLimit(tr, strtol(pref, NULL, 10));
  else
    tr_setUploadLimit(tr, -1);
}
