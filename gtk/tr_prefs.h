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

#ifndef TR_PREFS_H
#define TR_PREFS_H

#include <gtk/gtk.h>

#include "conf.h"

#define TR_PREFS_TYPE		  ( tr_prefs_get_type() )

#define TR_PREFS( obj ) \
  ( G_TYPE_CHECK_INSTANCE_CAST( (obj),   TR_PREFS_TYPE, TrPrefs ) )

#define TR_PREFS_CLASS( class ) \
  ( G_TYPE_CHECK_CLASS_CAST(    (class), TR_PREFS_TYPE, TrPrefsClass ) )

#define TR_IS_PREFS( obj ) \
  ( G_TYPE_CHECK_INSTANCE_TYPE( (obj),   TR_PREFS_TYPE ) )

#define TR_IS_PREFS_CLASS( class ) \
  ( G_TYPE_CHECK_CLASS_TYPE(    (class), TR_PREFS_TYPE ) )

#define TR_PREFS_GET_CLASS( obj ) \
  ( G_TYPE_INSTANCE_GET_CLASS(  (obj),   TR_PREFS_TYPE, TrPrefsClass ) )

typedef struct _TrPrefs TrPrefs;
typedef struct _TrPrefsClass TrPrefsClass;

/* treat the contents of this structure as private */
struct _TrPrefs
{
    GtkDialog      parent;
    GtkTreeModel * combomodel;
    gboolean       disposed;
};

struct _TrPrefsClass
{
    GtkDialogClass parent;
    int            changesig;
};

GType
tr_prefs_get_type( void );

TrPrefs *
tr_prefs_new( void );

TrPrefs *
tr_prefs_new_with_parent( GtkWindow * parent );

/* please keep this in sync with defs in tr_prefs.c */
enum
{
    PREF_ID_USEDOWNLIMIT = 0,
    PREF_ID_DOWNLIMIT,
    PREF_ID_USEUPLIMIT,
    PREF_ID_UPLIMIT,
    PREF_ID_ASKDIR,
    PREF_ID_DIR,
    PREF_ID_PORT,
    PREF_ID_NAT,
    PREF_ID_PEX,
    PREF_ID_ICON,
    PREF_ID_ASKQUIT,
    PREF_ID_ADDSTD,
    PREF_ID_ADDIPC,
    PREF_ID_MSGLEVEL,
    PREF_MAX_ID
};

const char *
tr_prefs_name( int id );

/* convenience macros and functions for reading pref by id */
#define tr_prefs_get( id )      cf_getpref( tr_prefs_name( (id) ) )

gboolean
tr_prefs_get_int( int id, int * val );

gboolean
tr_prefs_get_bool( int id, gboolean * val );

int
tr_prefs_get_int_with_default( int id );

gboolean
tr_prefs_get_bool_with_default( int id );

#endif
