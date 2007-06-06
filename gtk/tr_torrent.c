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

#include "transmission.h"
#include "bencode.h"

/* XXX */
#define TR_WANT_TORRENT_PRIVATE

#include "tr_prefs.h"
#include "tr_torrent.h"
#include "util.h"

enum {
  TR_TORRENT_HANDLE = 1,
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
  GParamSpec *pspec;

  gobject_class->set_property = tr_torrent_set_property;
  gobject_class->get_property = tr_torrent_get_property;
  gobject_class->dispose = tr_torrent_dispose;

  pspec = g_param_spec_pointer("torrent-handle", "Torrent handle",
                               "Torrent handle from libtransmission",
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
  g_object_class_install_property(gobject_class, TR_TORRENT_HANDLE, pspec);

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

#ifdef REFDBG
  fprintf( stderr, "torrent %p init\n", self );
#endif

  self->handle = NULL;
  self->dir = NULL;
  self->delfile = NULL;
  self->severed = FALSE;
  self->disposed = FALSE;
  self->ul_cap_enabled = FALSE;
  self->ul_cap = 0;
  self->dl_cap_enabled = FALSE;
  self->dl_cap = 0;
}

static void
tr_torrent_set_property(GObject *object, guint property_id,
                        const GValue *value, GParamSpec *pspec) {
  TrTorrent *self = (TrTorrent*)object;

  if(self->severed)
    return;

  switch(property_id) {
    case TR_TORRENT_HANDLE:
      g_assert(NULL == self->handle);
      self->handle = g_value_get_pointer(value);
      if(NULL != self->handle && NULL != self->dir)
        tr_torrent_set_folder(self);
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

  if(self->severed)
    return;

  switch(property_id) {
    case TR_TORRENT_HANDLE:
      g_value_set_pointer(value, self->handle);
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

#ifdef REFDBG
  fprintf( stderr, "torrent %p dispose\n", self );
#endif

  if( !self->severed )
  {
      tr_torrent_sever( self );
  }

  g_free (self->delfile);
  g_free (self->dir);

  /* Chain up to the parent class */
  parent->dispose(obj);
}

void
tr_torrent_sever( TrTorrent * self )
{
    g_return_if_fail (TR_IS_TORRENT( self ));

    if( self->severed )
    {
        return;
    }

#ifdef REFDBG
    fprintf( stderr, "torrent %p sever\n", self );
#endif

    if( NULL == self->handle )
    {
        self->severed = TRUE;
        return;
    }

    if( !tr_torrent_paused( self ) )
    {
        tr_torrentStop( self->handle );
    }
    tr_torrentClose( self->handle );
    self->severed = TRUE;
}

tr_torrent_t *
tr_torrent_handle(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->severed)
    return NULL;

  return tor->handle;
}

tr_stat_t *
tr_torrent_stat(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->severed)
    return NULL;

  return tr_torrentStat(tor->handle);
}

tr_info_t *
tr_torrent_info(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->severed)
    return NULL;

  return tr_torrentInfo(tor->handle);
}

void
tr_torrent_start( TrTorrent * self )
{
    TR_IS_TORRENT( self );

    if( self->severed || !tr_torrent_paused( self ) )
    {
        return;
    }

    tr_torrentStart( self->handle );
}

void
tr_torrent_stop( TrTorrent * self )
{
    TR_IS_TORRENT( self );

    if( self->severed || tr_torrent_paused( self ) )
    {
        return;
    }

    tr_torrentStop( self->handle );
}

static TrTorrent *
maketorrent( tr_torrent_t * handle,
             const char   * dir,
             gboolean       paused )
{
    TrTorrent * tor;

    tr_torrentDisablePex( handle,
                          !tr_prefs_get_bool_with_default( PREF_ID_PEX ) );

    tor = g_object_new( TR_TORRENT_TYPE,
                        "torrent-handle", handle,
                        "download-directory", dir,
                        NULL);
    g_object_set( tor, "paused", paused, NULL );

    return tor;
}

TrTorrent *
tr_torrent_new( tr_handle_t * back, const char *torrent, const char *dir,
                enum tr_torrent_action act, gboolean paused, char **err )
{
  TrTorrent *ret;
  tr_torrent_t *handle;
  int errcode, flag;

  g_assert(NULL != dir);

  *err = NULL;

  flag    = ( TR_TOR_COPY == act || TR_TOR_MOVE == act ? TR_FLAG_SAVE : 0 );
  errcode = -1;

  handle = tr_torrentInit( back, torrent, NULL, flag, &errcode );

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

  ret = maketorrent( handle, dir, paused );

  if( TR_TOR_MOVE == act )
    ret->delfile = g_strdup(torrent);

  return ret;
}

TrTorrent *
tr_torrent_new_with_data( tr_handle_t * back, uint8_t * data, size_t size,
                          const char * dir, gboolean paused, char ** err )
{
    tr_torrent_t * handle;
    int            errcode;

    g_assert( NULL != dir );

    *err = NULL;

    errcode = -1;
    handle  = tr_torrentInitData( back, data, size, NULL, TR_FLAG_SAVE,
                                  &errcode );

    if( NULL == handle )
    {
        switch( errcode )
        {
            case TR_EINVALID:
                *err = g_strdup( _("not a valid torrent file") );
                break;
            case TR_EDUPLICATE:
                *err = g_strdup( _("torrent is already open") );
                break;
            default:
                *err = g_strdup( "" );
                break;
        }
        return NULL;
    }

    return maketorrent( handle, dir, paused );
}

TrTorrent *
tr_torrent_new_with_state( tr_handle_t * back, benc_val_t * state,
                           gboolean forcedpause, char ** err )
{
  TrTorrent * ret;
  tr_torrent_t * handle;
  int ii, errcode;
  benc_val_t *name, *data;
  char *torrent, *hash, *dir;
  gboolean paused = FALSE;
  gboolean ul_cap_enabled = FALSE;
  gboolean dl_cap_enabled = FALSE;
  gboolean seeding_cap_enabled = FALSE;
  gint ul_cap = 0;
  gint dl_cap = 0;
  gdouble seeding_cap = 0.0;

  *err = NULL;

  if(TYPE_DICT != state->type)
    return NULL;

  torrent = hash = dir = NULL;
  paused = FALSE;

  for(ii = 0; ii + 1 < state->val.l.count; ii += 2) {
    name = state->val.l.vals + ii;
    data = state->val.l.vals + ii + 1;
    if(TYPE_STR == name->type &&
       (TYPE_STR == data->type || TYPE_INT == data->type)) {
      char * key = name->val.s.s;
      char * val = data->val.s.s;
           if (!strcmp (key, "torrent")) torrent = val;
      else if (!strcmp (key, "hash")) hash = val;
      else if (!strcmp (key, "dir")) dir = val;
      else if (!strcmp (key, "paused")) paused = !!data->val.i;
      else if (!strcmp (key, "ul-cap-speed")) ul_cap = data->val.i;
      else if (!strcmp (key, "ul-cap-enabled")) ul_cap_enabled = !!data->val.i;
      else if (!strcmp (key, "dl-cap-speed")) dl_cap = data->val.i;
      else if (!strcmp (key, "dl-cap-enabled")) dl_cap_enabled = !!data->val.i;
      else if (!strcmp (key, "seeding-cap-ratio")) seeding_cap = (data->val.i / 100.0);
      else if (!strcmp (key, "seeding-cap-enabled")) seeding_cap_enabled = !!data->val.i;
    }
  }

  if((NULL != torrent && NULL != hash) ||
     (NULL == torrent && NULL == hash) || NULL == dir)
    return NULL;

  if( NULL != hash )
    handle = tr_torrentInitSaved(back, hash, 0, &errcode);
  else
    handle = tr_torrentInit(back, torrent, NULL, 0, &errcode);

  if(NULL == handle) {
    torrent = ( NULL == hash ? torrent : hash );
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

  ret = maketorrent( handle, dir, paused || forcedpause );
  ret->ul_cap = ul_cap; 
  ret->ul_cap_enabled = ul_cap_enabled; 
  ret->dl_cap = dl_cap;
  ret->dl_cap_enabled = dl_cap_enabled; 
  ret->seeding_cap = seeding_cap;
  ret->seeding_cap_enabled = seeding_cap_enabled;
  return ret;
}

gboolean
tr_torrent_get_state( TrTorrent * tor, benc_val_t * state )
{
    tr_info_t  * inf;

    TR_IS_TORRENT( tor );

    if( tor->severed )
    {
        return FALSE;
    }

    inf = tr_torrentInfo( tor->handle );

    tr_bencInit( state, TYPE_DICT );
    if( tr_bencDictReserve( state, 3 ) )
    {
        return FALSE;
    }

    if( TR_FLAG_SAVE & inf->flags )
    {
        tr_bencInitStr( tr_bencDictAdd( state, "hash" ),
                        inf->hashString, -1, 1 );
    }
    else
    {
        tr_bencInitStr( tr_bencDictAdd( state, "torrent" ),
                        inf->torrent, -1, 1 );
    }
    tr_bencInitStr( tr_bencDictAdd( state, "dir" ),
                    tr_torrentGetFolder( tor->handle ), -1, 1 );
    tr_bencInitInt( tr_bencDictAdd( state, "paused" ),
                    tr_torrent_paused( tor ) ? 1 : 0 );

    tr_bencInitInt( tr_bencDictAdd( state, "ul-cap-speed" ),
                    tor->ul_cap );
    tr_bencInitInt( tr_bencDictAdd( state, "ul-cap-enabled" ),
                    tor->ul_cap_enabled ? 1 : 0 );

    tr_bencInitInt( tr_bencDictAdd( state, "dl-cap-speed" ),
                    tor->dl_cap );
    tr_bencInitInt( tr_bencDictAdd( state, "dl-cap-enabled" ),
                    tor->dl_cap_enabled ? 1 : 0);

    tr_bencInitInt( tr_bencDictAdd( state, "seeding-cap-ratio" ),
                    (int)(tor->dl_cap * 100.0)); /* two decimal places */
    tr_bencInitInt( tr_bencDictAdd( state, "seeding-cap-enabled" ),
                    tor->seeding_cap_enabled ? 1 : 0);

    return TRUE;
}

/* XXX this should probably be done with a signal */
void
tr_torrent_state_saved(TrTorrent *tor) {
  TR_IS_TORRENT(tor);

  if(tor->severed)
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

extern void tr_setUseCustomUpload( tr_torrent_t * tor, int limit );
extern void tr_setUseCustomDownload( tr_torrent_t * tor, int limit );


static void refresh_upload_cap ( TrTorrent *gtor ) {
  const int cap = gtor->ul_cap_enabled ? gtor->ul_cap : -1;
  g_message ("setting upload cap to %d...", cap);
  tr_setUseCustomUpload( gtor->handle, gtor->ul_cap_enabled );
  tr_setUploadLimit( gtor->handle, cap );
}
void
tr_torrent_set_upload_cap_speed ( TrTorrent *gtor, int KiB_sec ) {
  gtor->ul_cap = KiB_sec;
  refresh_upload_cap ( gtor );
}
void
tr_torrent_set_upload_cap_enabled ( TrTorrent *gtor, gboolean b ) {
  gtor->ul_cap_enabled = b;
  refresh_upload_cap ( gtor );
}

static void refresh_download_cap ( TrTorrent *gtor ) {
  const int cap = gtor->dl_cap_enabled ? gtor->dl_cap : -1;
  tr_setUseCustomDownload( gtor->handle, gtor->dl_cap_enabled );
  tr_setDownloadLimit( gtor->handle, cap );
}
void
tr_torrent_set_download_cap_speed ( TrTorrent *gtor, int KiB_sec ) {
  gtor->dl_cap = KiB_sec;
  refresh_download_cap( gtor );
}
void
tr_torrent_set_download_cap_enabled ( TrTorrent *gtor, gboolean b ) {
  gtor->dl_cap_enabled = b;
  refresh_download_cap( gtor );
}

void
tr_torrent_check_seeding_cap ( TrTorrent *gtor) {
  tr_stat_t * st = tr_torrent_stat( gtor );
  if ((gtor->seeding_cap_enabled) && (st->ratio >= gtor->seeding_cap))
    tr_torrent_stop (gtor);
}
void
tr_torrent_set_seeding_cap_ratio ( TrTorrent *gtor, gdouble ratio ) {
  gtor->seeding_cap = ratio;
  tr_torrent_check_seeding_cap (gtor);
}
void
tr_torrent_set_seeding_cap_enabled ( TrTorrent *gtor, gboolean b ) {
  if ((gtor->seeding_cap_enabled = b))
    tr_torrent_check_seeding_cap (gtor);
}

char *
tr_torrent_status_str ( TrTorrent * gtor )
{
    char * top = 0;

    tr_stat_t * st = tr_torrent_stat( gtor );

    const int tpeers = MAX (st->peersTotal, 0);
    const int upeers = MAX (st->peersUploading, 0);
    const int eta = st->eta;
    const double prog = st->progress * 100.0; /* [0...100] */
    const int status = st->status;

    if( TR_STATUS_CHECK_WAIT & status )
    {
        top = g_strdup_printf( _("Waiting to check existing files (%.1f%%)"), prog );
    }
    else if( TR_STATUS_CHECK & status )
    {
        top = g_strdup_printf( _("Checking existing files (%.1f%%)"), prog );
    }
    else if( TR_STATUS_DOWNLOAD & status )
    {
        if( 0 > eta )
        {
            top = g_strdup_printf( _("Stalled (%.1f%%)"), prog );
        }
        else
        {
            char * timestr = readabletime(eta);
            top = g_strdup_printf( _("Finishing in %s (%.1f%%)"),
                                   timestr, prog );
            g_free(timestr);
        }
    }
    else if(TR_STATUS_SEED & status)
    {
        top = g_strdup_printf(
            ngettext( "Seeding, uploading to %d of %d peer",
                      "Seeding, uploading to %d of %d peers", tpeers ),
            upeers, tpeers );
    }
    else if( TR_STATUS_STOPPING & status )
    {
        top = g_strdup( _("Stopping...") );
    }
    else if( TR_STATUS_PAUSE & status )
    {
        top = g_strdup_printf( _("Stopped (%.1f%%)"), prog );
    }
    else
    {
        top = g_strdup( "" );
        g_assert_not_reached();
    }

    return top;
}
