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

#ifndef TR_BACKEND_H
#define TR_BACKEND_H

#include <glib-object.h>

#include "transmission.h"
#include "bencode.h"

#define TR_BACKEND_TYPE		  (tr_backend_get_type ())
#define TR_BACKEND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TR_BACKEND_TYPE, TrBackend))
#define TR_BACKEND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TR_BACKEND_TYPE, TrBackendClass))
#define TR_IS_BACKEND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TR_BACKEND_TYPE))
#define TR_IS_BACKEND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TR_BACKEND_TYPE))
#define TR_BACKEND_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TR_BACKEND_TYPE, TrBackendClass))

typedef struct _TrBackend TrBackend;
typedef struct _TrBackendClass TrBackendClass;

/* treat the contents of this structure as private */
struct _TrBackend {
  GObject parent;
  tr_handle_t *handle;
  GList *torrents;
  gboolean disposed;
};

struct _TrBackendClass {
  GObjectClass parent;
};

GType
tr_backend_get_type(void);

TrBackend *
tr_backend_new(void);

tr_handle_t *
tr_backend_handle(TrBackend *back);

void
tr_backend_save_state(TrBackend *back, char **errstr);

GList *
tr_backend_load_state(TrBackend *back, benc_val_t *state, GList **errors);

void
tr_backend_stop_torrents(TrBackend *back);

gboolean
tr_backend_torrents_stopped(TrBackend *back);

#ifdef TR_WANT_BACKEND_PRIVATE
void
tr_backend_add_torrent(TrBackend *back, GObject *tor);
#endif

#endif
