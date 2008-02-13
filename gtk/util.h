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

#ifndef TG_UTIL_H
#define TG_UTIL_H

#include <sys/types.h>
#include <stdarg.h>

/* macro to shut up "unused parameter" warnings */
#define UNUSED G_GNUC_UNUSED

/* NULL-safe version of strcmp */
int tr_strcmp( const char*, const char * );

/* return number of items in array */
#define ALEN(a) ((signed)G_N_ELEMENTS(a))

/* used for a callback function with a data parameter */
typedef void (*callbackfunc_t)(void*);

/* return a human-readable string for the size given in bytes. */
char* tr_strlsize( char * buf, guint64 size, size_t buflen );

/* return a human-readable string for the transfer rate given in bytes. */
char* tr_strlspeed (char * buf, double KiBps, size_t buflen );

/* return a human-readable string for the given ratio. */
char* tr_strlratio( char * buf, double ratio, size_t buflen );

/* return a human-readable string for the time given in seconds. */
char* tr_strltime( char * buf, int secs, size_t buflen );

char *
rfc822date (guint64 epoch_msec);

/* create a directory and any missing parent directories */
gboolean
mkdir_p(const char *name, mode_t mode);

/* create a copy of a GList of strings, this dups the actual strings too */
GList *
dupstrlist( GList * list );

/* joins a GList of strings into one string using an optional separator */
char *
joinstrlist(GList *list, char *sep);

/* free a GList of strings */
void
freestrlist(GList *list);

/* decodes a string that has been urlencoded */
char *
decode_uri( const char * uri );

/* return a list of cleaned-up paths, with invalid directories removed */
GList *
checkfilenames( int argc, char ** argv );

/* retrieve the global download directory */
char *
getdownloaddir( void );

#ifdef GTK_MAJOR_VERSION

/* here there be dragons */
void
sizingmagic( GtkWindow * wind, GtkScrolledWindow * scroll,
             GtkPolicyType hscroll, GtkPolicyType vscroll );

/* create an error dialog, if wind is NULL or mapped then show dialog now,
   otherwise show it when wind becomes mapped */
void
errmsg( GtkWindow * wind, const char * format, ... )
#ifdef __GNUC__
    __attribute__ (( format ( printf, 2, 3 ) ))
#endif
    ;

/* create an error dialog but do not gtk_widget_show() it,
   calls func( data ) when the dialog is closed */
GtkWidget *
errmsg_full( GtkWindow * wind, callbackfunc_t func, void * data,
             const char * format, ... )
#ifdef __GNUC__
    __attribute__ (( format ( printf, 4, 5 ) ))
#endif
    ;

/* varargs version of errmsg_full() */
GtkWidget *
verrmsg_full( GtkWindow * wind, callbackfunc_t func, void * data,
              const char * format, va_list ap );

/* pop up the context menu if a user right-clicks.
   if the row they right-click on isn't selected, select it. */
gboolean
on_tree_view_button_pressed (GtkWidget       * view,
                             GdkEventButton  * event,
                             gpointer          unused);

gpointer tr_object_ref_sink (gpointer object);

#endif /* GTK_MAJOR_VERSION */

#endif /* TG_UTIL_H */
