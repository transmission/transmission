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

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#define TR_WANT_TORRENT_PRIVATE

#include "transmission.h"
#include "bencode.h"

#include "conf.h"
#include "tr_backend.h"
#include "tr_torrent.h"
#include "util.h"

/*
enum {
  TR_BACKEND_HANDLE = 1,
};
*/

static void
tr_backend_init(GTypeInstance *instance, gpointer g_class);
static void
tr_backend_set_property(GObject *object, guint property_id,
                        const GValue *value, GParamSpec *pspec);
static void
tr_backend_get_property(GObject *object, guint property_id,
                        GValue *value, GParamSpec *pspec);
static void
tr_backend_class_init(gpointer g_class, gpointer g_class_data);
static void
tr_backend_dispose(GObject *obj);
static void
tr_backend_finalize(GObject *obj);
static void
tr_backend_torrent_finalized(gpointer gdata, GObject *tor);

GType
tr_backend_get_type(void) {
  static GType type = 0;

  if(0 == type) {
    static const GTypeInfo info = {
      sizeof (TrBackendClass),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      tr_backend_class_init,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      sizeof (TrBackend),
      0,      /* n_preallocs */
      tr_backend_init, /* instance_init */
      NULL,
    };
    type = g_type_register_static(G_TYPE_OBJECT, "TrBackendType", &info, 0);
  }
  return type;
}

static void
tr_backend_class_init(gpointer g_class, gpointer g_class_data SHUTUP) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
  //GParamSpec *pspec;

  gobject_class->set_property = tr_backend_set_property;
  gobject_class->get_property = tr_backend_get_property;
  gobject_class->dispose = tr_backend_dispose;
  gobject_class->finalize = tr_backend_finalize;

/*
  pspec = g_param_spec_pointer("backend-handle", _("Backend handle"),
                               _("Backend handle from libtransmission"),
                               G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_BACKEND_HANDLE, pspec);
*/
}

static void
tr_backend_init(GTypeInstance *instance, gpointer g_class SHUTUP) {
  TrBackend *self = (TrBackend *)instance;

  self->handle = tr_init();
  self->disposed = FALSE;
}

static void
tr_backend_set_property(GObject *object, guint property_id,
                        const GValue *value SHUTUP, GParamSpec *pspec) {
  TrBackend *self = (TrBackend*)object;

  if(self->disposed)
    return;

  switch(property_id) {
/*
    case TR_BACKEND_HANDLE:
      g_assert(NULL == self->handle);
      self->handle = g_value_get_pointer(value);
      break;
*/
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
tr_backend_get_property(GObject *object, guint property_id,
                        GValue *value SHUTUP, GParamSpec *pspec) {
  TrBackend *self = (TrBackend*)object;

  if(self->disposed)
    return;

  switch(property_id) {
/*
    case TR_BACKEND_HANDLE:
      g_value_set_pointer(value, self->handle);
      break;
*/
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
tr_backend_dispose(GObject *obj) {
  GObjectClass *parent = g_type_class_peek(g_type_parent(TR_BACKEND_TYPE));
  TrBackend *self = (TrBackend*)obj;
  GList *ii;

  if(self->disposed)
    return;
  self->disposed = TRUE;

  if(NULL != self->torrents) {
    for(ii = self->torrents; NULL != ii; ii = ii->next)
      g_object_weak_unref(ii->data, tr_backend_torrent_finalized, self);
    g_list_free(self->torrents);
    self->torrents = NULL;
  }

  /* Chain up to the parent class */
  parent->dispose(obj);
}

static void
tr_backend_finalize(GObject *obj) {
  GObjectClass *parent = g_type_class_peek(g_type_parent(TR_BACKEND_TYPE));
  TrBackend *self = (TrBackend *)obj;

  if(NULL != self->handle)
    tr_close(self->handle);

  /* Chain up to the parent class */
  parent->finalize(obj);
}

TrBackend *
tr_backend_new(void) {
  return g_object_new(TR_BACKEND_TYPE, NULL);
}

tr_handle_t *
tr_backend_handle(TrBackend *back) {
  TR_IS_BACKEND(back);

  return back->handle;
}

void
tr_backend_save_state(TrBackend *back, char **errstr) {
  benc_val_t state;
  int ii;
  GList *jj;

  TR_IS_BACKEND(back);

  bzero(&state, sizeof(state));
  state.type = TYPE_LIST;
  state.val.l.alloc = state.val.l.count = g_list_length(back->torrents);
  state.val.l.vals = g_new0(benc_val_t, state.val.l.alloc);

  for(ii = 0, jj = back->torrents; NULL != jj; ii++, jj = jj->next)
    tr_torrent_get_state(jj->data, state.val.l.vals + ii);

  cf_savestate(&state, errstr);
  tr_bencFree(&state);
}

GList *
tr_backend_load_state(TrBackend *back, benc_val_t *state, GList **errors) {
  GList *ret = NULL;
  int ii;
  TrTorrent *tor;
  char *errstr;

  TR_IS_BACKEND(back);

  if(TYPE_LIST != state->type)
    return NULL;

  for(ii = 0; ii < state->val.l.count; ii++) {
    errstr = NULL;
    tor = tr_torrent_new_with_state(G_OBJECT(back), state->val.l.vals + ii,
                                    &errstr);
    if(NULL != errstr)
      *errors = g_list_append(*errors, errstr);
    if(NULL != tor)
      ret = g_list_append(ret, tor);
  }

  return ret;
}

void
tr_backend_add_torrent(TrBackend *back, GObject *tor) {
  TR_IS_BACKEND(back);
  TR_IS_TORRENT(tor);

  g_object_weak_ref(tor, tr_backend_torrent_finalized, back);
  back->torrents = g_list_append(back->torrents, tor);
}

static void
tr_backend_torrent_finalized(gpointer gdata, GObject *tor) {
  TrBackend *back = gdata;

  TR_IS_BACKEND(back);

  back->torrents = g_list_remove(back->torrents, tor);
}

void
tr_backend_stop_torrents(TrBackend *back) {
  GList *ii;

  TR_IS_BACKEND(back);

  for(ii = back->torrents; NULL != ii; ii = ii->next)
    if(TR_STATUS_ACTIVE & tr_torrent_stat(ii->data)->status)
      tr_torrentStop(tr_torrent_handle(ii->data));
}

gboolean
tr_backend_torrents_stopped(TrBackend *back) {
  GList *ii;

  TR_IS_BACKEND(back);

  for(ii = back->torrents; NULL != ii; ii = ii->next)
    if(TR_STATUS_ACTIVE & tr_torrent_stat(ii->data)->status)
      return FALSE;

  return TRUE;
}
