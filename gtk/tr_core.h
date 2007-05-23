/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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

#ifndef TR_CORE_H
#define TR_CORE_H

#include <glib-object.h>
#include <gtk/gtk.h>

#define TR_CORE_TYPE		( tr_core_get_type() )

#define TR_CORE( obj )                                                        \
  ( G_TYPE_CHECK_INSTANCE_CAST( (obj),   TR_CORE_TYPE, TrCore ) )

#define TR_CORE_CLASS( class )                                                \
  ( G_TYPE_CHECK_CLASS_CAST(    (class), TR_CORE_TYPE, TrCoreClass ) )

#define TR_IS_CORE( obj )                                                     \
  ( G_TYPE_CHECK_INSTANCE_TYPE( (obj),   TR_CORE_TYPE ) )

#define TR_IS_CORE_CLASS( class )                                             \
  ( G_TYPE_CHECK_CLASS_TYPE(    (class), TR_CORE_TYPE ) )

#define TR_CORE_GET_CLASS( obj )                                              \
  ( G_TYPE_INSTANCE_GET_CLASS(  (obj),   TR_CORE_TYPE, TrCoreClass ) )

typedef struct _TrCore TrCore;
typedef struct _TrCoreClass TrCoreClass;

/* treat the contents of this structure as private */
struct _TrCore
{
    GObject             parent;
    GtkTreeModel      * model;
    gboolean            disposed;
};

struct _TrCoreClass
{
  GObjectClass          parent;
};

GType
tr_core_get_type( void );

TrCore *
tr_core_new( void );

GtkTreeModel *
tr_core_model( TrCore * self );

/* column names for the model used to store torrent information */
/* keep this in sync with the type array in tr_core_init() in tr_core.c */
enum {
  MC_NAME, MC_SIZE, MC_STAT, MC_ERR, MC_TERR,
  MC_PROG, MC_DRATE, MC_URATE, MC_ETA, MC_PEERS,
  MC_UPEERS, MC_DPEERS, MC_SEED, MC_LEECH, MC_DONE,
  MC_DOWN, MC_UP, MC_LEFT, MC_TRACKER, MC_TORRENT, MC_ROW_COUNT,
};

#endif
