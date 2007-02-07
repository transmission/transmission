/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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
#ifdef __GNUC__
#define SHUTUP                  __attribute__((unused))
#else
#define SHUTUP
#endif

typedef void (*add_torrents_func_t)(void*,void*,GList*,const char*,guint);

/* return number of items in array */
#define ALEN(a)                 (sizeof(a) / sizeof((a)[0]))

#define ISA(o, t)               (g_type_is_a(G_OBJECT_TYPE(G_OBJECT(o)), (t)))

/* used for a callback function with a data parameter */
typedef void (*callbackfunc_t)(void*);

/* try to interpret a string as a textual representation of a boolean */
/* note that this isn't localized */
gboolean
strbool(const char *str);

/* return a human-readable string for the size given in bytes.
   the string must be g_free()d */
char *
readablesize(guint64 size);

/* return a human-readable string for the time given in seconds.
   the string must be g_free()d */
char *
readabletime(int secs);

/* returns a string representing the download ratio.
   the string must be g_free()d */
char *
ratiostr(guint64 down, guint64 up);

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
urldecode(const char *str, int len);

/* return a list of cleaned-up paths, with invalid directories removed */
GList *
checkfilenames(int argc, char **argv);

/* returns the flag for an action string */
guint
addactionflag(const char *action);

/* returns the action string for a flag */
const char *
addactionname(guint flag);

/* turn a NULL-terminated list of void* arguments into a glist */
GList *
makeglist(void *ptr, ...);

#ifdef GTK_MAJOR_VERSION

/* if wind is NULL then you must call gtk_widget_show on the returned widget */

GtkWidget *
errmsg(GtkWindow *wind, const char *format, ...)
#ifdef __GNUC__
  __attribute__ ((format (printf, 2, 3)))
#endif
  ;

GtkWidget *
errmsg_full(GtkWindow *wind, callbackfunc_t func, void *data,
            const char *format, ...)
#ifdef __GNUC__
  __attribute__ ((format (printf, 4, 5)))
#endif
  ;

GtkWidget *
verrmsg(GtkWindow *wind, callbackfunc_t func, void *data,
        const char *format, va_list ap);

#endif /* GTK_MAJOR_VERSION */

#endif /* TG_UTIL_H */
