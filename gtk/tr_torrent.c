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

#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#define TR_WANT_BACKEND_PRIVATE

#include "transmission.h"
#include "bencode.h"

#include "tr_backend.h"
#include "tr_prefs.h"
#include "tr_torrent.h"
#include "util.h"

enum {
  TR_TORRENT_HANDLE = 1,
  TR_TORRENT_BACKEND,
  TR_TORRENT_DIR,
  TR_TORRENT_PAUSED,
};

static void
tr_torrent_init(GTypeInstance *instance, gpointer g_class);
static void
tr_torrent_set_property(GObject *object, guint property_id,
                        const GValue *value, GParamSpec *pspec);
static void
tr_torrent_get_property(GObject *object, guint property_id,
                        GValue *value, GParamSpec *pspec);
static void
tr_torrent_class_init(gpointer g_class, gpointer g_class_data);
static void
tr_torrent_dispose(GObject *obj);
static void
tr_torrent_set_folder(TrTorrent *tor);
static gboolean
tr_torrent_paused(TrTorrent *tor);

static gpointer
tracker_boxed_fake_copy( gpointer boxed )
{
    return boxed;
}

static void
tracker_boxed_fake_free( gpointer boxed SHUTUP )
{
}

GType
tr_tracker_boxed_get_type( void )
{
    static GType type = 0;

    if( 0 == type )
    {
        type = g_boxed_type_register_static( "TrTrackerBoxed",
                                             tracker_boxed_fake_copy,
                                             tracker_boxed_fake_free );
    }

    return type;
}

GType
tr_torrent_get_type(void) {
  static GType type = 0;

  if(0 == type) {
    static const GTypeInfo info = {
      sizeof (TrTorrentClass),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      tr_torrent_class_init,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      sizeof (TrTorrent),
      0,      /* n_preallocs */
      tr_torrent_init, /* instance_init */
      NULL,
    };
    type = g_type_register_static(G_TYPE_OBJECT, "TrTorrent", &info, 0);
  }
  return type;
}

static void
tr_torrent_class_init(gpointer g_class, gpointer g_class_data SHUTUP) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
  TrTorrentClass *klass = TR_TORRENT_CLASS(g_class);
  GParamSpec *pspec;

  gobject_class->set_property = tr_torrent_set_property;
  gobject_class->get_property = tr_torrent_get_property;
  gobject_class->dispose = tr_torrent_dispose;

  pspec = g_param_spec_pointer("torrent-handle", "Torrent handle",
                               "Torrent handle from libtransmission",
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_TORRENT_HANDLE, pspec);

  pspec = g_param_spec_object("backend", "Backend",
                              "Libtransmission backend object",
                              TR_BACKEND_TYPE,
                              G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_TORRENT_BACKEND, pspec);

  pspec = g_param_spec_string("download-directory", "Download directory",
                              "Directory to download files to", NULL,
                              G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_TORRENT_DIR, pspec);

  pspec = g_param_spec_boolean("paused", "Paused",
                               "Is the torrent paused or running", TRUE,
                               G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_TORRENT_PAUSED, pspec);

  klass->paused_signal_id = g_signal_newv("politely-stopped",
                                          G_TYPE_FROM_CLASS(g_class),
                                          G_SIGNAL_RUN_LAST, NULL, NULL, NULL,
                                          g_cclosure_marshal_VOID__VOID,
                                          G_TYPE_NONE, 0, NULL);
}

static void
tr_torrent_init(GTypeInstance *instance, gpointer g_class SHUTUP) {
  TrTorrent *self = (TrTorrent *)instance;

  self->handle = NULL;
  self->back = NULL;
  self->dir = NULL;
  self->closing = FALSE;
  self->delfile = NULL;
  self->disposed = FALSE;
}

static void
tr_torrent_set_property(GObject *object, guint property_id,
                        const GValue *value, GParamSpec *pspec) {
  TrTorrent *self = (TrTorrent*)object;

  if(self->disposed)
    return;

  switch(property_id) {
    case TR_TORRENT_HANDLE:
      g_assert(NULL == self->handle);
      self->handle = g_value_get_pointer(value);
      if(NULL != self->handle && NULL != self->dir)
        tr_torrent_set_folder(self);
      break;
    case TR_TORRENT_BACKEND:
      g_assert(NULL == self->back);
      self->back = g_object_ref(g_value_get_object(value));
      break;
    case TR_TORRENT_DIR:
      g_assert(NULL == self->dir);
      self->dir = g_value_dup_string(value);
      if(NULL != self->handle && NULL != self->dir)
        tr_torrent_set_folder(self);
      break;
    case TR_TORRENT_PAUSED:
      g_assert(NULL != self->handle);
      if(tr_torrent_paused(self) != g_value_get_boolean(value))
        (g_value_get_boolean(value) ? tr_torrentStop : tr_torrentStart)
          (self->handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
tr_torrent_get_property(GObject *object, guint property_id,
                        GValue *value, GParamSpec *pspec) {
  TrTorrent *self = (TrTorrent*)object;

  if(self->disposed)
    return;

  switch(property_id) {
    case TR_TORRENT_HANDLE:
      g_value_set_pointer(value, self->handle);
      break;
    case TR_TORRENT_BACKEND:
      g_value_set_object(value, self->back);
      break;
    case TR_TORRENT_DIR:
      g_value_set_string(value, (NULL != self->dir ? self->dir :
                                 tr_torrentGetFolder(self->handle)));
      break;
    case TR_TORRENT_PAUSED:
      g_value_set_boolean(value, tr_torrent_paused(self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
tr_torrent_dispose(GObject *obj) {
  GObjectClass *parent = g_type_class_peek(g_type_parent(TR_TORRENT_TYPE));
  TrTorrent *self = (TrTorrent*)obj;

  if(self->disposed)
    return;
  self->disposed = TRUE;

  if(NULL != self->handle) {
    if(!tr_torrent_paused(self))
      tr_torrentStop(self->handle);
    tr_torrentClose(tr_backend_handle(TR_BACKEND(self->back)), self->handle);
    self->handle = NULL;
  }

  if(NULL != self->back) {
    g_object_unref(self->back);
    self->back = NULL;
  }

  if(NULL != self->delfile)
    g_free(self->delfile);

  /* Chain up to the parent class */
  parent->dispose(obj);
}

tr_torrent_t *
tr_torrent_handle(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->disposed)
    return NULL;

  return tor->handle;
}

tr_stat_t *
tr_torrent_stat(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->disposed)
    return NULL;

  return tr_torrentStat(tor->handle);
}

tr_info_t *
tr_torrent_info(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->disposed)
    return NULL;

  return tr_torrentInfo(tor->handle);
}

TrTorrent *
tr_torrent_new(GObject *backend, const char *torrent, const char *dir,
               guint flags, char **err) {
  TrTorrent *ret;
  tr_torrent_t *handle;
  tr_handle_t *back;
  int errcode, trflags;
  gboolean boolval;

  TR_IS_BACKEND(backend);
  g_assert(NULL != dir);

  *err = NULL;

  back = tr_backend_handle(TR_BACKEND(backend));
  trflags = 0;
  if((TR_TORNEW_SAVE_COPY|TR_TORNEW_SAVE_MOVE) & flags)
    trflags |= TR_FLAG_SAVE;
  errcode = -1;

  if(TR_TORNEW_LOAD_SAVED & flags)
    handle = tr_torrentInitSaved(back, torrent, 0, &errcode);
  else
    handle = tr_torrentInit(back, torrent, NULL, trflags, &errcode);

  if(NULL == handle) {
    switch(errcode) {
      case TR_EINVALID:
        *err = g_strdup_printf(_("%s: not a valid torrent file"), torrent);
        break;
      case TR_EDUPLICATE:
        *err = g_strdup_printf(_("%s: torrent is already open"), torrent);
        break;
      default:
        *err = g_strdup(torrent);
        break;
    }
    return NULL;
  }

  /* I should probably add a property for this but I've had enough
     with adding useless gtk glue to this program */
  boolval = tr_prefs_get_bool_with_default( PREF_ID_PEX );
  tr_torrentDisablePex( handle, !boolval );

  ret = g_object_new(TR_TORRENT_TYPE, "torrent-handle", handle,
                     "backend", backend, "download-directory", dir, NULL);
  tr_backend_add_torrent(TR_BACKEND(backend), G_OBJECT(ret));
  
  g_object_set(ret, "paused", (TR_TORNEW_PAUSED & flags ? TRUE : FALSE), NULL);

  if(TR_TORNEW_SAVE_MOVE & flags)
    ret->delfile = g_strdup(torrent);

  return ret;
}

TrTorrent *
tr_torrent_new_with_state( GObject * backend, benc_val_t * state,
                           guint forcedflags, char ** err)
{
  int ii;
  benc_val_t *name, *data;
  char *torrent, *hash, *dir;
  gboolean hadpaused, paused;
  guint flags;

  *err = NULL;

  if(TYPE_DICT != state->type)
    return NULL;

  torrent = hash = dir = NULL;
  hadpaused = FALSE;
  paused = FALSE;               /* silence stupid compiler warning */

  for(ii = 0; ii + 1 < state->val.l.count; ii += 2) {
    name = state->val.l.vals + ii;
    data = state->val.l.vals + ii + 1;
    if(TYPE_STR == name->type &&
       (TYPE_STR == data->type || TYPE_INT == data->type)) {
      if(0 == strcmp("torrent", name->val.s.s))
        torrent = data->val.s.s;
      if(0 == strcmp("hash", name->val.s.s))
        hash = data->val.s.s;
      else if(0 == strcmp("dir", name->val.s.s))
        dir = data->val.s.s;
      else if(0 == strcmp("paused", name->val.s.s)) {
        hadpaused = TRUE;
        paused = (data->val.i ? TRUE : FALSE);
      }
    }
  }

  if((NULL != torrent && NULL != hash) ||
     (NULL == torrent && NULL == hash) || NULL == dir)
    return NULL;

  flags = 0;
  if(hadpaused)
    flags |= (paused ? TR_TORNEW_PAUSED : TR_TORNEW_RUNNING);
  if(NULL != hash) {
    flags |= TR_TORNEW_LOAD_SAVED;
    torrent = hash;
  }
  forcedflags &= TR_TORNEW_PAUSED | TR_TORNEW_RUNNING;
  if( forcedflags )
  {
      flags &= ~( TR_TORNEW_PAUSED | TR_TORNEW_RUNNING );
      flags |= forcedflags;
  }

  return tr_torrent_new(backend, torrent, dir, flags, err);
}

#define SETSTRVAL(vv, ss) \
  do { \
    (vv)->type = TYPE_STR; \
    (vv)->val.s.s = g_strdup((ss)); \
    (vv)->val.s.i = strlen((ss)); \
  } while(0)

void
tr_torrent_get_state(TrTorrent *tor, benc_val_t *state) {
  tr_info_t *in = tr_torrentInfo(tor->handle);

  TR_IS_TORRENT(tor);

  if(tor->disposed)
    return;

  if(tor->closing)
    return;

  state->type = TYPE_DICT;
  state->val.l.vals = g_new0(benc_val_t, 6);
  state->val.l.alloc = state->val.l.count = 6;

  if(TR_FLAG_SAVE & in->flags) {
    SETSTRVAL(state->val.l.vals + 0, "hash");
    SETSTRVAL(state->val.l.vals + 1, in->hashString);
  } else {
    SETSTRVAL(state->val.l.vals + 0, "torrent");
    SETSTRVAL(state->val.l.vals + 1, in->torrent);
  }
  SETSTRVAL(state->val.l.vals + 2, "dir");
  SETSTRVAL(state->val.l.vals + 3, tr_torrentGetFolder(tor->handle));
  SETSTRVAL(state->val.l.vals + 4, "paused");
  state->val.l.vals[5].type = TYPE_INT;
  state->val.l.vals[5].val.i = tr_torrent_paused(tor);
}

/* XXX this should probably be done with a signal */
void
tr_torrent_state_saved(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->disposed)
    return;

  if(NULL != tor->delfile) {
    unlink(tor->delfile);
    g_free(tor->delfile);
    tor->delfile = NULL;
  }
}

static void
tr_torrent_set_folder(TrTorrent *tor) {
  char *wd;

  if(NULL != tor->dir)
    tr_torrentSetFolder(tor->handle, tor->dir);
  else {
    wd = g_new(char, MAX_PATH_LENGTH + 1);
    tr_torrentSetFolder(tor->handle,
                        (NULL == getcwd(wd, MAX_PATH_LENGTH + 1) ? "." : wd));
    g_free(wd);
  }
}

static gboolean
tr_torrent_paused(TrTorrent *tor) {
  tr_stat_t *st = tr_torrentStat(tor->handle);

  return (TR_STATUS_INACTIVE & st->status ? TRUE : FALSE);
}

void
tr_torrent_stop_politely(TrTorrent *tor) {
  tr_stat_t *st;

  TR_IS_TORRENT(tor);

  if(tor->disposed)
    return;

  if(!tor->closing) {
    st = tr_torrent_stat(tor);
    tor->closing = TRUE;
    if(TR_STATUS_ACTIVE & st->status)
      tr_torrentStop(tor->handle);
  }
}

tr_stat_t *
tr_torrent_stat_polite( TrTorrent * tor, gboolean timeout )
{
    TrTorrentClass * klass;
    tr_stat_t      * st;

    TR_IS_TORRENT( tor );

    if( tor->disposed )
    {
        return NULL;
    }

    st = tr_torrentStat( tor->handle );
    if( tor->closing && ( TR_STATUS_PAUSE & st->status || timeout ) )
    {
        tor->closing = FALSE;
        klass = g_type_class_peek( TR_TORRENT_TYPE );
        g_signal_emit( tor, klass->paused_signal_id, 0, NULL );
        return tr_torrent_stat_polite( tor, FALSE );
    }

    return st;
}
