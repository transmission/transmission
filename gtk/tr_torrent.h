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
