/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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
#ifndef TRCOMPAT_H
#define TRCOMPAT_H

#include <stdarg.h>
#include <stddef.h> /* for size_t */

#ifndef HAVE_STRLCPY
size_t
strlcpy(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_STRLCAT
size_t
strlcat(char *dst, const char *src, size_t siz);
#endif

#ifndef HAVE_ASPRINTF
int asprintf( char **, const char *, ... );
int vasprintf( char **, const char *, va_list );
#endif

#if defined(HAVE_DIRNAME) || defined(HAVE_BASENAME)
    #include <libgen.h>
#endif
#ifndef HAVE_DIRNAME
    char* dirname(const char *path);
#endif
#ifndef HAVE_BASENAME
    char* basename(const char *path);
#endif

#endif /* TRCOMPAT_H */
