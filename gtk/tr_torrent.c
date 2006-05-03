#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#define TR_WANT_BACKEND_PRIVATE

#include "transmission.h"
#include "bencode.h"

#include "tr_backend.h"
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
tr_torrent_finalize(GObject *obj);
static void
tr_torrent_set_folder(TrTorrent *tor);
static gboolean
tr_torrent_paused(TrTorrent *tor);

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
    type = g_type_register_static(G_TYPE_OBJECT, "TrTorrentType", &info, 0);
  }
  return type;
}

static void
tr_torrent_class_init(gpointer g_class, gpointer g_class_data SHUTUP) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
  //TrTorrentClass *klass = TR_TORRENT_CLASS(g_class);
  GParamSpec *pspec;

  gobject_class->set_property = tr_torrent_set_property;
  gobject_class->get_property = tr_torrent_get_property;
  gobject_class->dispose = tr_torrent_dispose;
  gobject_class->finalize = tr_torrent_finalize;

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
}

static void
tr_torrent_init(GTypeInstance *instance, gpointer g_class SHUTUP) {
  TrTorrent *self = (TrTorrent *)instance;

  self->handle = NULL;
  self->back = NULL;
  self->dir = NULL;
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

  fprintf(stderr, "tor dispose %p\n", self);

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

  /* Chain up to the parent class */
  parent->dispose(obj);
}

static void
tr_torrent_finalize(GObject *obj) {
  GObjectClass *parent = g_type_class_peek(g_type_parent(TR_TORRENT_TYPE));
  TrTorrent *self = (TrTorrent *)obj;

  fprintf(stderr, "tor finalize %p\n", self);

  /* Chain up to the parent class */
  parent->finalize(obj);
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
               gboolean *paused, char **err) {
  TrTorrent *ret;
  tr_torrent_t *handle;
  int errcode;

  TR_IS_BACKEND(backend);
  g_assert(NULL != dir);

  *err = NULL;

  errcode = -1;
  handle = tr_torrentInit(tr_backend_handle(TR_BACKEND(backend)),
                          torrent, &errcode);
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

  ret = g_object_new(TR_TORRENT_TYPE, "torrent-handle", handle,
                     "backend", backend, "download-directory", dir, NULL);
  tr_backend_add_torrent(TR_BACKEND(backend), G_OBJECT(ret));

  g_object_set(ret, "paused", (NULL == paused ? FALSE : *paused), NULL);

  return ret;
}

TrTorrent *
tr_torrent_new_with_state(GObject *backend, benc_val_t *state, char **err) {
  int ii;
  benc_val_t *name, *data;
  char *torrent, *dir;
  gboolean hadpaused, paused;

  *err = NULL;

  if(TYPE_DICT != state->type)
    return NULL;

  torrent = dir = NULL;
  hadpaused = FALSE;

  for(ii = 0; ii + 1 < state->val.l.count; ii += 2) {
    name = state->val.l.vals + ii;
    data = state->val.l.vals + ii + 1;
    if(TYPE_STR == name->type &&
       (TYPE_STR == data->type || TYPE_INT == data->type)) {
      if(0 == strcmp("torrent", name->val.s.s))
        torrent = data->val.s.s;
      else if(0 == strcmp("dir", name->val.s.s))
        dir = data->val.s.s;
      else if(0 == strcmp("paused", name->val.s.s)) {
        hadpaused = TRUE;
        paused = (data->val.i ? TRUE : FALSE);
      }
    }
  }

  if(NULL == torrent || NULL == dir)
    return NULL;

  return tr_torrent_new(backend, torrent, dir,
                        (hadpaused ? &paused : NULL), err);
}

void
tr_torrent_get_state(TrTorrent *tor, benc_val_t *state) {
  tr_info_t *in = tr_torrentInfo(tor->handle);
  const char *strs[] = {
    "torrent", in->torrent, "dir", tr_torrentGetFolder(tor->handle), "paused", 
  };
  unsigned int ii;
  const unsigned int len = 6;

  TR_IS_TORRENT(tor);

  if(tor->disposed)
    return;

  state->type = TYPE_DICT;
  state->val.l.vals = g_new0(benc_val_t, len);
  state->val.l.alloc = state->val.l.count = len;

  g_assert(len > ALEN(strs));
  for(ii = 0; ii < ALEN(strs); ii++) {
    state->val.l.vals[ii].type = TYPE_STR;
    state->val.l.vals[ii].val.s.s = g_strdup(strs[ii]);
    state->val.l.vals[ii].val.s.i = strlen(strs[ii]);
  }

  state->val.l.vals[ii].type = TYPE_INT;
  state->val.l.vals[ii].val.i = tr_torrent_paused(tor);
  ii++;

  g_assert(len == ii);
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
