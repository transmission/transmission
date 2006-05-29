/*
  $Id$

  Copyright (c) 2006 Joshua Elsasser. All rights reserved.
   
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

#ifndef TR_TORRENT_H
#define TR_TORRENT_H

#include <glib-object.h>

#include "transmission.h"
#include "bencode.h"

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
  GObject *back;
  char *dir;
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

TrTorrent *
tr_torrent_new(GObject *backend, const char *torrent, const char *dir,
               gboolean *paused, char **err);

TrTorrent *
tr_torrent_new_with_state(GObject *backend, benc_val_t *state, char **err);

#ifdef TR_WANT_TORRENT_PRIVATE
void
tr_torrent_get_state(TrTorrent *tor, benc_val_t *state);
#endif

#endif
