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

#include <string.h>

#include <glib-object.h>
#include <gtk/gtk.h>

#include <libtransmission/bencode.h>
#include <libtransmission/transmission.h>

#include "util.h"

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
    GObject           parent;
    GtkTreeModel    * model;
    tr_handle       * handle;
    int               nextid;
    gboolean          quitting;
    gboolean          disposed;
};

struct _TrCoreClass
{
    GObjectClass        parent;
    /* "error" signal:
       void handler( TrCore *, enum tr_core_err, const char *, gpointer ) */
    int                 errsig;
    /* "directory-prompt" signal:
       void handler( TrCore *, GList *, enum tr_torrent_action, gboolean,
                     gpointer ) */
    int                 promptsig;
    /* "directory-prompt-data" signal:
       void handler( TrCore *, uint8_t *, size_t, gboolean, gpointer ) */
    int                 promptdatasig;
    /* "quit" signal:
       void handler( TrCore *, gpointer ) */
    int                 quitsig;
    /* "prefs-changed" signal:
       void handler( TrCore *, int, gpointer ) */
    int                 prefsig;
};

enum tr_core_err
{
    TR_CORE_ERR_ADD_TORRENT,    /* adding a torrent failed */
    /* no more torrents to be added, used for grouping torrent add errors */
    TR_CORE_ERR_NO_MORE_TORRENTS,
    TR_CORE_ERR_SAVE_STATE      /* error saving state */
};

GType
tr_core_get_type( void );

TrCore *
tr_core_new( void );

/* Return the model used without incrementing the reference count */
GtkTreeModel *
tr_core_model( TrCore * self );

/* Returns the libtransmission handle */
tr_handle *
tr_core_handle( TrCore * self );

/* Load saved state, return number of torrents added. May trigger one
   or more "error" signals with TR_CORE_ERR_ADD_TORRENT */
int
tr_core_load( TrCore * self, gboolean forcepaused );

/* Any the tr_core_add functions below may trigger an "error" signal
   with TR_CORE_ERR_ADD_TORRENT */

/* Add the torrent at the given path */
gboolean
tr_core_add( TrCore * self, const char * path, enum tr_torrent_action act,
             gboolean paused );

/* Add the torrent at the given path with the given download directory */
gboolean
tr_core_add_dir( TrCore * self, const char * path, const char * dir,
                 enum tr_torrent_action act, gboolean paused );

/* Add a list of torrents with the given paths */
int
tr_core_add_list( TrCore * self, GList * paths, enum tr_torrent_action act,
                  gboolean paused );

/* Add the torrent data in the given buffer */
gboolean
tr_core_add_data( TrCore * self, uint8_t * data, size_t size, gboolean paused );

/* Add the torrent data in the given buffer with the given download directory */
gboolean
tr_core_add_data_dir( TrCore * self, uint8_t * data, size_t size,
                      const char * dir, gboolean paused );

/* Save state, update model, and signal the end of a torrent cluster */
void
tr_core_torrents_added( TrCore * self );

/* remove a torrent, waiting for it to pause if necessary */
void
tr_core_delete_torrent( TrCore * self, GtkTreeIter * iter /* XXX */ );

/* update the model with current torrent status */
void
tr_core_update( TrCore * self );

/* emit the "quit" signal */
void
tr_core_quit( TrCore * self );

/* Set a preference value, save the prefs file, and emit the
   "prefs-changed" signal */
void
tr_core_set_pref( TrCore * self, const char * key, const char * val );

gboolean
tr_core_toggle_pref_bool( TrCore * core, const char * key );
/* Set a boolean preference value, save the prefs file, and emit the
   "prefs-changed" signal */
void
tr_core_set_pref_bool( TrCore * self, const char * key, gboolean val );

/* Set an integer preference value, save the prefs file, and emit the
   "prefs-changed" signal */
void
tr_core_set_pref_int( TrCore * self, const char * key, int val );

void
tr_core_resort( TrCore * core );

/* column names for the model used to store torrent information */
/* keep this in sync with the type array in tr_core_init() in tr_core.c */
enum
{
    MC_NAME,
    MC_NAME_COLLATED,
    MC_HASH,
    MC_TORRENT,
    MC_TORRENT_RAW,
    MC_ID,
    MC_ROW_COUNT
};

#endif
