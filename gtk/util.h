/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef GTR_UTIL_H
#define GTR_UTIL_H

#include <sys/types.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>

extern const int mem_K;
extern const char * mem_K_str;
extern const char * mem_M_str;
extern const char * mem_G_str;
extern const char * mem_T_str;

extern const int disk_K;
extern const char * disk_K_str;
extern const char * disk_M_str;
extern const char * disk_G_str;
extern const char * disk_T_str;

extern const int speed_K;
extern const char * speed_K_str;
extern const char * speed_M_str;
extern const char * speed_G_str;
extern const char * speed_T_str;

#if GLIB_CHECK_VERSION(2,33,12)
 #define TR_DEFINE_QUARK G_DEFINE_QUARK
#else
 #define TR_DEFINE_QUARK(QN, q_n)                                        \
 GQuark                                                                  \
 q_n##_quark (void)                                                      \
 {                                                                       \
   static GQuark q;                                                      \
                                                                         \
   if G_UNLIKELY (q == 0)                                                \
     q = g_quark_from_static_string (#QN);                               \
                                                                         \
  return q;                                                             \
 }
#endif


/* macro to shut up "unused parameter" warnings */
#ifndef UNUSED
 #define UNUSED G_GNUC_UNUSED
#endif

enum
{
    GTR_UNICODE_UP,
    GTR_UNICODE_DOWN,
    GTR_UNICODE_INF,
    GTR_UNICODE_BULLET
};
const char * gtr_get_unicode_string (int);

/* return a percent formatted string of either x.xx, xx.x or xxx */
char* tr_strlpercent (char * buf, double x, size_t buflen);

/* return a human-readable string for the size given in bytes. */
char* tr_strlsize (char * buf, guint64  size, size_t buflen);

/* return a human-readable string for the given ratio. */
char* tr_strlratio (char * buf, double ratio, size_t buflen);

/* return a human-readable string for the time given in seconds. */
char* tr_strltime (char * buf, int secs, size_t buflen);

/***
****
***/

/* http://www.legaltorrents.com/some/announce/url --> legaltorrents.com */
void gtr_get_host_from_url (char * buf, size_t buflen, const char * url);

gboolean gtr_is_magnet_link (const char * str);

gboolean gtr_is_hex_hashcode (const char * str);

/***
****
***/

void        gtr_open_uri (const char * uri);

void        gtr_open_file (const char * path);

const char* gtr_get_help_uri (void);

/***
****
***/

/* backwards-compatible wrapper around gtk_widget_set_visible () */
void gtr_widget_set_visible (GtkWidget *, gboolean);

void gtr_dialog_set_content (GtkDialog * dialog, GtkWidget * content);

/***
****
***/

GtkWidget * gtr_priority_combo_new (void);
#define gtr_priority_combo_get_value(w)     gtr_combo_box_get_active_enum (w)
#define gtr_priority_combo_set_value(w,val) gtr_combo_box_set_active_enum (w,val)

GtkWidget * gtr_combo_box_new_enum      (const char * text_1, ...);
int         gtr_combo_box_get_active_enum (GtkComboBox *);
void        gtr_combo_box_set_active_enum (GtkComboBox *, int value);

/***
****
***/

struct _TrCore;

GtkWidget * gtr_freespace_label_new (struct _TrCore * core, const char * dir);

void gtr_freespace_label_set_dir (GtkWidget * label, const char * dir);

/***
****
***/

void gtr_unrecognized_url_dialog (GtkWidget * parent, const char * url);

void gtr_http_failure_dialog (GtkWidget * parent, const char * url, long response_code);

void gtr_add_torrent_error_dialog (GtkWidget  * window_or_child,
                                   int          err,
                                   tr_torrent * duplicate_torrent,
                                   const char * filename);

/* pop up the context menu if a user right-clicks.
   if the row they right-click on isn't selected, select it. */
gboolean on_tree_view_button_pressed (GtkWidget      * view,
                                      GdkEventButton * event,
                                      gpointer         unused);

/* if the click didn't specify a row, clear the selection */
gboolean on_tree_view_button_released (GtkWidget      * view,
                                       GdkEventButton * event,
                                       gpointer         unused);


/* move a file to the trashcan if GIO is available; otherwise, delete it */
bool gtr_file_trash_or_remove (const char * filename, struct tr_error ** error);

void gtr_paste_clipboard_url_into_entry (GtkWidget * entry);

/* Only call gtk_label_set_text () if the new text differs from the old.
 * This prevents the label from having to recalculate its size
 * and prevents selected text in the label from being deselected */
void gtr_label_set_text (GtkLabel * lb, const char * text);

#endif /* GTR_UTIL_H */
