/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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

/* portability wrapper around g_warn_if_fail() for older versions of glib */
#ifdef g_warn_if_fail
 #define gtr_warn_if_fail(expr) g_warn_if_fail(expr)
#else
 #define gtr_warn_if_fail(expr) do { if G_LIKELY (expr) ; else \
                                       g_log (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "%s:%d func(): %s: invariant failed: %s", \
                                              __FILE__, __LINE__, G_STRFUNC, #expr ); } while(0)
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
const char * gtr_get_unicode_string( int );

/* return a percent formatted string of either x.xx, xx.x or xxx */
char* tr_strlpercent( char * buf, double x, size_t buflen );

/* return a human-readable string for the size given in bytes. */
char* tr_strlsize( char * buf, guint64  size, size_t buflen );

/* return a human-readable string for the given ratio. */
char* tr_strlratio( char * buf, double ratio, size_t buflen );

/* return a human-readable string for the time given in seconds. */
char* tr_strltime( char * buf, int secs, size_t buflen );

/* similar to asctime, but is utf8-clean */
char* gtr_localtime( time_t time );


int gtr_compare_double( const double a, const double b, int decimal_places );


/***
****
***/

/* http://www.legaltorrents.com/some/announce/url --> legaltorrents.com */
char* gtr_get_host_from_url( const char * url );

gboolean gtr_is_supported_url( const char * str );

gboolean gtr_is_magnet_link( const char * str );

gboolean gtr_is_hex_hashcode( const char * str );

/***
****
***/

typedef enum
{
    GTR_LOCKFILE_SUCCESS = 0,
    GTR_LOCKFILE_EOPEN,
    GTR_LOCKFILE_ELOCK
}
gtr_lockfile_state_t;

gtr_lockfile_state_t gtr_lockfile( const char * filename );

/***
****
***/

void        gtr_open_uri( const char * uri );

void        gtr_open_file( const char * path );

gboolean    gtr_dbus_add_torrent( const char * filename );

gboolean    gtr_dbus_present_window( void );

const char* gtr_get_help_uri( void );

/***
****
***/

/* backwards-compatible wrapper around g_mkdir_with_parents() */
int gtr_mkdir_with_parents( const char *name, int mode );

/* backwards-compatible wrapper around gdk_threads_add_timeout_seconds() */
guint gtr_timeout_add_seconds( guint seconds, GSourceFunc func, gpointer data );

/* backwards-compatible wrapper around gdk_threads_add_idle() */
guint gtr_idle_add( GSourceFunc  func, gpointer data );

/* backwards-compatible wrapper around gtk_widget_set_tooltip_text() */
void gtr_widget_set_tooltip_text( GtkWidget * w, const char * tip );

/* backwards-compatible wrapper around gtk_widget_get_window() */
GdkWindow* gtr_widget_get_window( GtkWidget * w );

/* backwards-compatible wrapper around gtk_widget_get_realized() */
gboolean gtr_widget_get_realized( GtkWidget * w );

/* backwards-compatible wrapper around gtk_widget_set_visible() */
void gtr_widget_set_visible( GtkWidget *, gboolean );

/* backwards-compatible wrapper around gtk_cell_renderer_get_padding() */
void gtr_cell_renderer_get_padding( GtkCellRenderer *, gint * xpad, gint * ypad );

/* backwards-compatible wrapper around g_object_ref_sink() */
gpointer gtr_object_ref_sink( gpointer object );

/* backwards-compatible wrapper around g_strcmp0() */
int gtr_strcmp0( const char * str1, const char * str2 );

/* backwards-compatible wrapper around g_dngettext() */
const gchar* gtr_ngettext( const gchar*, const gchar*, gulong );

void gtr_dialog_set_content( GtkDialog * dialog, GtkWidget * content );

/***
****
***/

GtkWidget * gtr_priority_combo_new( void );
#define gtr_priority_combo_get_value(w)     gtr_combo_box_get_active_enum(w)
#define gtr_priority_combo_set_value(w,val) gtr_combo_box_set_active_enum(w,val)

GtkWidget * gtr_combo_box_new_enum        ( const char * text_1, ... );
int         gtr_combo_box_get_active_enum ( GtkComboBox * );
void        gtr_combo_box_set_active_enum ( GtkComboBox *, int value );

/***
****
***/

void gtr_unrecognized_url_dialog( GtkWidget * parent, const char * url );

void gtr_http_failure_dialog( GtkWidget * parent, const char * url, long response_code );

void gtr_add_torrent_error_dialog( GtkWidget  * window_or_child,
                                   int          err,
                                   const char * filename );

/* pop up the context menu if a user right-clicks.
   if the row they right-click on isn't selected, select it. */
gboolean on_tree_view_button_pressed( GtkWidget      * view,
                                      GdkEventButton * event,
                                      gpointer         unused );

/* if the click didn't specify a row, clear the selection */
gboolean on_tree_view_button_released( GtkWidget      * view,
                                       GdkEventButton * event,
                                       gpointer         unused );


/* move a file to the trashcan if GIO is available; otherwise, delete it */
int gtr_file_trash_or_remove( const char * filename );

void gtr_paste_clipboard_url_into_entry( GtkWidget * entry );


#endif /* GTR_UTIL_H */
