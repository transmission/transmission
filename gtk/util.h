/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
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

/* macro to shut up "unused parameter" warnings */
#ifndef UNUSED
 #define UNUSED G_GNUC_UNUSED
#endif

/* return a human-readable string for the size given in bytes. */
char* tr_strlsize( char * buf, guint64  size, size_t buflen );

/* return a human-readable string for the transfer rate given in bytes. */
char* tr_strlspeed( char * buf, double KiBps, size_t buflen );

/* return a human-readable string for the given ratio. */
char* tr_strlratio( char * buf, double ratio, size_t buflen );

/* return a human-readable string for the time given in seconds. */
char* tr_strltime( char * buf, int secs, size_t buflen );

/* similar to asctime, but is utf8-clean */
char* gtr_localtime( time_t time );

/* similar to asctime, but is utf8-clean */
char* gtr_localtime2( char * buf, time_t time, size_t buflen );

/***
****
***/

/* create a copy of a GSList of strings, this dups the actual strings too */
GSList * dupstrlist( GSList * list );

/* joins a GSList of strings into one string using an optional separator */
char * joinstrlist( GSList *list, char *  sep );

/* free a GSList of strings */
void freestrlist( GSList *list );

/* decodes a string that has been urlencoded */
char * decode_uri( const char * uri );

/* return a list of cleaned-up paths, with invalid directories removed */
GSList * checkfilenames( int argc, char ** argv );

/***
****
***/

void        gtr_open_file( const char * path );

gboolean    gtr_dbus_add_torrent( const char * filename );

gboolean    gtr_dbus_present_window( void );

char*       gtr_get_help_url( void );

/***
****
***/

/* GTK-related utilities */
#ifdef GTK_MAJOR_VERSION

/* backwards-compatible wrapper around g_mkdir_with_parents() */
int gtr_mkdir_with_parents( const char *name, int mode );

/* backwards-compatible wrapper around gdk_threads_add_timeout_seconds() */
guint gtr_timeout_add_seconds( guint seconds, GSourceFunc func, gpointer data );

/* backwards-compatible wrapper around gdk_threads_add_idle() */
void gtr_idle_add( GSourceFunc  func, gpointer data );

/* backwards-compatible wrapper around gtk_orientable_set_orientation() */
void gtr_toolbar_set_orientation( GtkToolbar * tb, GtkOrientation orientation );

/* backwards-compatible wrapper around gtk_widget_set_tooltip_text() */
void gtr_widget_set_tooltip_text( GtkWidget * w, const char * tip );

/* backwards-compatible wrapper around g_object_ref_sink() */
gpointer tr_object_ref_sink( gpointer object );

/***
****
***/

/* create a button with the specified mnemonic and stock icon */
GtkWidget * gtr_button_new_from_stock( const char * stock,
                                       const char * mnemonic );

void addTorrentErrorDialog( GtkWidget  * window_or_child,
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
int tr_file_trash_or_remove( const char * filename );

#endif /* GTK_MAJOR_VERSION */

#endif /* GTR_UTIL_H */
