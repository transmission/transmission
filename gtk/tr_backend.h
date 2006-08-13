/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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
