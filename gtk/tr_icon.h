/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

#ifndef TR_ICON_H
#define TR_ICON_H

#include <gtk/gtk.h>

#if GTK_MAJOR_VERSION > 2 || \
    ( GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION >= 10 )
#define TR_ICON_SUPPORTED
#define tr_icon_supported()     (TRUE)
#else
#define tr_icon_supported()     (FALSE)
#endif

#define TR_ICON_TYPE		  ( tr_icon_get_type() )

#define TR_ICON( obj ) \
  ( G_TYPE_CHECK_INSTANCE_CAST( (obj),   TR_ICON_TYPE, TrIcon ) )

#define TR_ICON_CLASS( class ) \
  ( G_TYPE_CHECK_CLASS_CAST(    (class), TR_ICON_TYPE, TrIconClass ) )

#define TR_IS_ICON( obj ) \
  ( G_TYPE_CHECK_INSTANCE_TYPE( (obj),   TR_ICON_TYPE ) )

#define TR_IS_ICON_CLASS( class ) \
  ( G_TYPE_CHECK_CLASS_TYPE(    (class), TR_ICON_TYPE ) )

#define TR_ICON_GET_CLASS( obj ) \
  ( G_TYPE_INSTANCE_GET_CLASS(  (obj),   TR_ICON_TYPE, TrIconClass ) )

typedef struct _TrIcon TrIcon;
typedef struct _TrIconClass TrIconClass;

/* treat the contents of this structure as private */
struct _TrIcon
{
#ifdef TR_ICON_SUPPORTED
    GtkStatusIcon parent;
#else
    GObject       parent;
#endif
    int           clickact;
    gboolean      disposed;
};

struct _TrIconClass
{
#ifdef TR_ICON_SUPPORTED
    GtkStatusIconClass parent;
#else
    GObjectClass       parent;
#endif
    int                actionsig;
};

GType
tr_icon_get_type( void );

TrIcon *
tr_icon_new( void );

gboolean
tr_icon_docked( TrIcon * icon );

#endif
