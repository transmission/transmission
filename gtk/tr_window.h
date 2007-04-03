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

#ifndef TR_WINDOW_H
#define TR_WINDOW_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define TR_WINDOW_TYPE		  ( tr_window_get_type() )

#define TR_WINDOW( obj ) \
  ( G_TYPE_CHECK_INSTANCE_CAST( (obj),   TR_WINDOW_TYPE, TrWindow ) )

#define TR_WINDOW_CLASS( class ) \
  ( G_TYPE_CHECK_CLASS_CAST(    (class), TR_WINDOW_TYPE, TrWindowClass ) )

#define TR_IS_WINDOW( obj ) \
  ( G_TYPE_CHECK_INSTANCE_TYPE( (obj),   TR_WINDOW_TYPE ) )

#define TR_IS_WINDOW_CLASS( class ) \
  ( G_TYPE_CHECK_CLASS_TYPE(    (class), TR_WINDOW_TYPE ) )

#define TR_WINDOW_GET_CLASS( obj ) \
  ( G_TYPE_INSTANCE_GET_CLASS(  (obj),   TR_WINDOW_TYPE, TrWindowClass ) )

typedef struct _TrWindow TrWindow;
typedef struct _TrWindowClass TrWindowClass;

/* treat the contents of this structure as private */
struct _TrWindow
{
    GtkWindow           parent;
    GtkScrolledWindow * scroll;
    GtkTreeView       * view;
    GtkStatusbar      * status;
    GtkToolbar        * toolbar;
    GtkMenuShell      * menu;
    GObject           * namerend;
    int                 doubleclick;
    GList             * actions;
    GtkAccelGroup     * accel;
    GtkWidget         * stupidpopuphack;
    gboolean            disposed;
};

struct _TrWindowClass
{
  GtkWindowClass parent;
  int            actionsig;
};

GType
tr_window_get_type( void );

GtkWidget *
tr_window_new( void );

void
tr_window_action_add( TrWindow * wind, int id, int flags, const char * name,
                      const char * icon, const char * description, guint key );

void
tr_window_update( TrWindow * wind, float downspeed, float upspeed );

/* some magic to show the window with a nice initial size */
void
tr_window_show( TrWindow * wind );

#endif
