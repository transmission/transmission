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

#include <libtransmission/transmission.h>

#include "tr_prefs.h"
#include "tr_torrent.h"
#include "conf.h"
#include "util.h"

static void
tr_torrent_init(GTypeInstance *instance, gpointer g_class UNUSED )
{
  TrTorrent *self = (TrTorrent *)instance;

#ifdef REFDBG
  fprintf( stderr, "torrent %p init\n", self );
#endif

  self->handle = NULL;
  self->lastStatTime = 0;
  self->delfile = NULL;
  self->severed = FALSE;
  self->disposed = FALSE;
}

static void
tr_torrent_dispose(GObject *obj)
{
  GObjectClass *parent = g_type_class_peek(g_type_parent(TR_TORRENT_TYPE));
  TrTorrent *self = (TrTorrent*)obj;

  if(self->disposed)
    return;
  self->disposed = TRUE;

#ifdef REFDBG
  fprintf( stderr, "torrent %p dispose\n", self );
#endif

  if( !self->severed )
      tr_torrent_sever( self );

  g_free (self->delfile);

  /* Chain up to the parent class */
  parent->dispose(obj);
}

static void
tr_torrent_class_init(gpointer g_class, gpointer g_class_data UNUSED )
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
  gobject_class->dispose = tr_torrent_dispose;
}

GType
tr_torrent_get_type(void)
{
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

void
tr_torrent_sever( TrTorrent * self )
{
    g_return_if_fail (TR_IS_TORRENT( self ));

    if( !self->severed )
    {
        self->severed = TRUE;

        if( self->handle )
            tr_torrentClose( self->handle );
    }
}

tr_torrent *
tr_torrent_handle(TrTorrent *tor)
{
    g_assert( TR_IS_TORRENT(tor) );

    return tor->severed ? NULL : tor->handle;
}

static tr_stat*
refreshStat( TrTorrent * tor )
{
    tor->lastStatTime= time( NULL );
    tor->stat = *tr_torrentStat( tor->handle );
    return &tor->stat;
}

const tr_stat *
tr_torrent_stat(TrTorrent *tor)
{
    g_assert( TR_IS_TORRENT(tor) );

    if( !tor->severed && tor->lastStatTime!=time(NULL) )
        refreshStat( tor );

    return &tor->stat;
}

const tr_info *
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

    if( !self->severed )
        tr_torrentStart( self->handle );
}

void
tr_torrent_stop( TrTorrent * self )
{
    TR_IS_TORRENT( self );

    if( !self->severed )
        tr_torrentStop( self->handle );
}

static TrTorrent *
maketorrent( tr_torrent * handle )
{
    tr_torrentDisablePex( handle, !pref_flag_get( PREF_KEY_PEX ) );
    TrTorrent * tor = g_object_new( TR_TORRENT_TYPE, NULL );
    tor->handle = handle;
    return tor;
}

TrTorrent*
tr_torrent_new_preexisting( tr_torrent * tor )
{
    return maketorrent( tor );
}


TrTorrent *
tr_torrent_new( tr_handle * back, const char *torrent, const char *dir,
                enum tr_torrent_action act, gboolean paused, char **err )
{
  TrTorrent *ret;
  tr_torrent *handle;
  int errcode;

  g_assert(NULL != dir);

  *err = NULL;

  errcode = -1;

  handle = tr_torrentInit( back, torrent, dir, paused, &errcode );

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

  ret = maketorrent( handle );

  if( TR_TOR_MOVE == act )
    ret->delfile = g_strdup(torrent);

  return ret;
}

TrTorrent *
tr_torrent_new_with_data( tr_handle * back, uint8_t * data, size_t size,
                          const char * dir, gboolean paused, char ** err )
{
    tr_torrent * handle;
    int          errcode;

    g_assert( NULL != dir );

    *err = NULL;

    errcode = -1;
    handle  = tr_torrentInitData( back, data, size, dir, paused, &errcode );

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

    return maketorrent( handle );
}

void
tr_torrent_check_seeding_cap ( TrTorrent *gtor)
{
  const tr_stat * st = tr_torrent_stat( gtor );
  if ((gtor->seeding_cap_enabled) && (st->ratio >= gtor->seeding_cap))
    tr_torrent_stop (gtor);
}
void
tr_torrent_set_seeding_cap_ratio ( TrTorrent *gtor, gdouble ratio )
{
  gtor->seeding_cap = ratio;
  tr_torrent_check_seeding_cap (gtor);
}
void
tr_torrent_set_seeding_cap_enabled ( TrTorrent *gtor, gboolean b )
{
  if ((gtor->seeding_cap_enabled = b))
    tr_torrent_check_seeding_cap (gtor);
}

char *
tr_torrent_status_str ( TrTorrent * gtor )
{
    char * top = 0;

    const tr_stat * st = tr_torrent_stat( gtor );

    const int tpeers = MAX (st->peersConnected, 0);
    const int upeers = MAX (st->peersGettingFromUs, 0);
    const int eta = st->eta;
    double prog = st->percentDone * 100.0; /* [0...100] */

    switch( st->status )
    {
        case TR_STATUS_CHECK_WAIT:
            prog = st->recheckProgress * 100.0; /* [0...100] */
            top = g_strdup_printf( _("Waiting to verify local files (%.1f%% tested)"), prog );
            break;

        case TR_STATUS_CHECK:
            prog = st->recheckProgress * 100.0; /* [0...100] */
            top = g_strdup_printf( _("Verifying local files (%.1f%% tested)"), prog );
            break;

        case TR_STATUS_DOWNLOAD:
            if( eta < 0 )
                top = g_strdup_printf( _("Stalled (%.1f%%)"), prog );
            else {
                char * timestr = readabletime(eta);
                top = g_strdup_printf( _("%s remaining (%.1f%%)"), timestr, prog );
                g_free(timestr);
            }
            break;

        case TR_STATUS_DONE:
            top = g_strdup_printf(
                ngettext( "Uploading to %d of %d peer",
                          "Uploading to %d of %d peers", tpeers ),
                          upeers, tpeers );
            break;

        case TR_STATUS_SEED:
            top = g_strdup_printf(
                ngettext( "Seeding to %d of %d peer",
                          "Seeding to %d of %d peers", tpeers ),
                          upeers, tpeers );
            break;

        case TR_STATUS_STOPPING:
            top = g_strdup( _("Stopping...") );
            break;

        case TR_STATUS_STOPPED:
            top = g_strdup_printf( _("Stopped (%.1f%%)"), prog );
            break;

        default:
            top = g_strdup_printf( _("Unrecognized state: %d"), st->status );
            break;

    }

    return top;
}
