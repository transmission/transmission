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

#ifndef TR_TORRENT_H
#define TR_TORRENT_H

#include <glib-object.h>

#include "transmission.h"
#include "bencode.h"

/* boxed type for tr_tracker_info_t */
#define TR_TRACKER_BOXED_TYPE (tr_tracker_boxed_get_type ())
GType
tr_tracker_boxed_get_type( void );

#define TR_TORRENT_TYPE		  (tr_torrent_get_type ())
#define TR_TORRENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TR_TORRENT_TYPE, TrTorrent))
#define TR_TORRENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TR_TORRENT_TYPE, TrTorrentClass))
#define TR_IS_TORRENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TR_TORRENT_TYPE))
#define TR_IS_TORRENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TR_TORRENT_TYPE))
#define TR_TORRENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TR_TORRENT_TYPE, TrTorrentClass))

typedef struct _TrTorrent TrTorrent;
typedef struct _TrTorrentClass TrTorrentClass;

/* treat the contents of this structure as private */
struct _TrTorrent {
  GObject parent;
  tr_torrent_t *handle;
  char *dir;
  char *delfile;
  gboolean severed;
  gboolean disposed;
};

struct _TrTorrentClass {
  GObjectClass parent;
};

GType
tr_torrent_get_type(void);

tr_torrent_t *
tr_torrent_handle(TrTorrent *tor);

tr_stat_t *
tr_torrent_stat(TrTorrent *tor);

tr_info_t *
tr_torrent_info(TrTorrent *tor);

/* explicitly start the torrent running or paused */
#define TR_TORNEW_PAUSED        0x01
#define TR_TORNEW_RUNNING       0x02
/* load a saved torrent file, torrent param is hash instead of filename */
#define TR_TORNEW_LOAD_SAVED    0x04
/* save a private copy of the torrent file */
#define TR_TORNEW_SAVE_COPY     0x08
/* save a private copy of the torrent file and remove the original */
#define TR_TORNEW_SAVE_MOVE     0x10

#ifdef TR_WANT_TORRENT_PRIVATE

TrTorrent *
tr_torrent_new( tr_handle_t * handle, const char *torrent, const char *dir,
                guint flags, char **err);

TrTorrent *
tr_torrent_new_with_state( tr_handle_t * handle, benc_val_t * state,
                           guint flags, char ** err );

void
tr_torrent_get_state(TrTorrent *tor, benc_val_t *state);

void
tr_torrent_state_saved(TrTorrent *tor);

void
tr_torrent_sever( TrTorrent * tor );

#endif /* TR_WANT_TORRENT_PRIVATE */

#endif
