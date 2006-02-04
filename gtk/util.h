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

/* return number of items in array */
#define ALEN(a)                 (sizeof(a) / sizeof((a)[0]))

/* used for a callback function with a data parameter */
typedef void (*callbackfunc_t)(void*);

/* try to interpret a string as a textual representation of a boolean */
/* note that this isn't localized */
gboolean
strbool(const char *str);

/* return a human-readable string for the size given in bytes with the
   requested number of decimal places.  the string must be g_free()d */
char *
readablesize(guint64 size, int decimals);

/* create a directory and any missing parent directories */
gboolean
mkdir_p(const char *name, mode_t mode);

/* set up a handler for various fatal signals */
void
setuphandlers(callbackfunc_t func, void *data);

/* clear the handlers for fatal signals */
void
clearhandlers(void);

/* blocks and unblocks delivery of fatal signals. calls to these
   functions can be nested as long as unblocksigs() is called exactly
   as many times as blocksigs().  only the first blocksigs() will
   block signals and only the last unblocksigs() will unblock them. */
void
blocksigs(void);
void
unblocksigs(void);

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

#endif /* TG_UTIL_H */
