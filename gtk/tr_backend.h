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

#ifdef TR_WANT_BACKEND_PRIVATE
void
tr_backend_add_torrent(TrBackend *back, GObject *tor);
#endif

#endif
