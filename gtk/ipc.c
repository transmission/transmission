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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "transmission.h"
#include "bencode.h"
#include "ipcparse.h"

#include "conf.h"
#include "io.h"
#include "ipc.h"
#include "tr_core.h"
#include "tr_prefs.h"
#include "tr_torrent.h"
#include "util.h"

/* XXX error handling throught this file is pretty bogus */

enum contype { CON_SERV, CON_CLIENT };

struct constate_serv
{
    GtkWindow * wind;
    gpointer    core;
};

struct constate_client
{
    GMainLoop  * loop;
    enum ipc_msg msg;
    GList      * files;
    gboolean   * succeeded;
    unsigned int msgid;
};

struct constate
{
    GSource          * source;
    int                fd;
    enum contype       type;
    struct ipc_funcs * msgs;
    struct ipc_info    ipc;
    union
    {
        struct constate_serv   serv;
        struct constate_client client;
    } u;
};

static void
serv_bind(struct constate *con);
static void
rmsock(void);
static gboolean
client_connect(char *path, struct constate *con);
static void
srv_io_accept(GSource *source, int fd, struct sockaddr *sa, socklen_t len,
              void *vdata);
static unsigned int
srv_io_received(GSource *source, char *data, unsigned int len, void *vdata);
static unsigned int
cli_io_received(GSource *source, char *data, unsigned int len, void *vdata);
static void
client_sendmsg( struct constate * con );
static void
destroycon(struct constate *con);
static void
all_io_closed(GSource *source, void *vdata);
static void
cli_io_sent(GSource *source, unsigned int id, void *vdata);
static void
smsg_add( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static void
smsg_addone( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static void
smsg_quit( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static void
smsg_info( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static void
smsg_infoall( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static int
addinfo( TrTorrent * tor, enum ipc_msg msgid, int torid, int types,
         benc_val_t * val );
static void
smsg_look( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static void
smsg_tor( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static void
smsg_torall( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static void
all_default( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg );
static gboolean
simpleresp( struct constate * con, int64_t tag, enum ipc_msg id );
static TrTorrent *
findtorid( TrCore * core, int id, GtkTreeIter * iter );
static TrTorrent *
findtorhash( TrCore * core, const char * hash, int * id );

/* this is only used on the server */
static char *gl_sockpath = NULL;

void
ipc_socket_setup( GtkWindow * parent, TrCore * core )
{
  struct constate *con;

  con = g_new0(struct constate, 1);
  con->source = NULL;
  con->fd = -1;
  con->type = CON_SERV;

  con->msgs = ipc_initmsgs();
  if( NULL == con->msgs ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_ADDMANYFILES, smsg_add ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_ADDONEFILE,   smsg_addone ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_GETINFO,      smsg_info ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_GETINFOALL,   smsg_infoall ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_GETSTAT,      smsg_info ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_GETSTATALL,   smsg_infoall ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_LOOKUP,       smsg_look ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_REMOVE,       smsg_tor ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_REMOVEALL,    smsg_torall ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_START,        smsg_tor ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_STARTALL,     smsg_torall ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_STOP,         smsg_tor ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_STOPALL,      smsg_torall ) ||
      0 > ipc_addmsg( con->msgs, IPC_MSG_QUIT,         smsg_quit ) )
  {
      errmsg( con->u.serv.wind, _("Failed to set up IPC:\n%s"),
              strerror( errno ) );
      g_free( con );
      return;
  }

  ipc_setdefmsg( con->msgs, all_default );

  con->u.serv.wind = parent;
  con->u.serv.core = core;

  g_object_add_weak_pointer( G_OBJECT( core ), &con->u.serv.core );

  serv_bind(con);
}

static gboolean
blocking_client( enum ipc_msg msgid, GList * files )
{

  struct constate *con;
  char *path;
  gboolean ret = FALSE;

  con = g_new0(struct constate, 1);
  con->source = NULL;
  con->fd = -1;
  con->type = CON_CLIENT;

  con->msgs = ipc_initmsgs();
  if( NULL == con->msgs )
  {
      fprintf( stderr, _("failed to set up IPC: %s\n"), strerror( errno ) );
      g_free( con );
      return FALSE;
  }

  ipc_setdefmsg( con->msgs, all_default );
  ipc_newcon( &con->ipc, con->msgs );

  con->u.client.loop = g_main_loop_new(NULL, TRUE);
  con->u.client.msg = msgid;
  con->u.client.files = files;
  con->u.client.succeeded = &ret;
  con->u.client.msgid = 0;

  path = cf_sockname();
  if(!client_connect(path, con)) {
    g_free(path);
    destroycon(con);
    return FALSE;
  }

  g_main_loop_run(con->u.client.loop);

  return ret;
}

gboolean
ipc_sendfiles_blocking( GList * files )
{
    return blocking_client( IPC_MSG_ADDMANYFILES, files );
}

gboolean
ipc_sendquit_blocking( void )
{
    return blocking_client( IPC_MSG_QUIT, NULL );
}

/* open a local socket for clients connections */
static void
serv_bind(struct constate *con) {
  struct sockaddr_un sa;

  rmsock();
  gl_sockpath = cf_sockname();

  if(0 > (con->fd = socket(AF_LOCAL, SOCK_STREAM, 0)))
    goto fail;

  bzero(&sa, sizeof(sa));
  sa.sun_family = AF_LOCAL;
  strncpy(sa.sun_path, gl_sockpath, sizeof(sa.sun_path) - 1);

  /* unlink any existing socket file before trying to create ours */
  unlink(gl_sockpath);
  if(0 > bind(con->fd, (struct sockaddr *)&sa, SUN_LEN(&sa))) {
    /* bind may fail if there was already a socket, so try twice */
    unlink(gl_sockpath);
    if(0 > bind(con->fd, (struct sockaddr *)&sa, SUN_LEN(&sa)))
      goto fail;
  }

  if(0 > listen(con->fd, 5))
    goto fail;

  con->source = io_new_listening(con->fd, sizeof(struct sockaddr_un),
                                 srv_io_accept, all_io_closed, con);

  g_atexit(rmsock);

  return;

 fail:
  errmsg(con->u.serv.wind, _("Failed to set up socket:\n%s"),
         strerror(errno));
  if(0 <= con->fd)
    close(con->fd);
  con->fd = -1;
  rmsock();
}

static void
rmsock(void) {
  if(NULL != gl_sockpath) {
    unlink(gl_sockpath);
    g_free(gl_sockpath);
  }
}

static gboolean
client_connect(char *path, struct constate *con) {
  struct sockaddr_un addr;
  uint8_t          * buf;
  size_t             size;

  if(0 > (con->fd = socket(AF_UNIX, SOCK_STREAM, 0))) {
    fprintf(stderr, _("failed to create socket: %s\n"), strerror(errno));
    return FALSE;
  }

  bzero(&addr, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  if(0 > connect(con->fd, (struct sockaddr*)&addr, SUN_LEN(&addr))) {
    fprintf(stderr, _("failed to connect to %s: %s\n"), path, strerror(errno));
    return FALSE;
  }

  con->source = io_new(con->fd, cli_io_sent, cli_io_received,
                       all_io_closed, con);
  if( NULL == con->source )
  {
      close( con->fd );
      return FALSE;
  }

  buf = ipc_mkvers( &size );
  if( NULL == buf )
  {
      close( con->fd );
      return FALSE;
  }

  io_send_keepdata( con->source, buf, size );

  return TRUE;
}

static void
srv_io_accept(GSource *source SHUTUP, int fd, struct sockaddr *sa SHUTUP,
              socklen_t len SHUTUP, void *vdata) {
  struct constate *con = vdata;
  struct constate *newcon;
  uint8_t        * buf;
  size_t           size;

  newcon = g_new(struct constate, 1);
  memcpy(newcon, con, sizeof(*newcon));
  newcon->fd = fd;
  ipc_newcon( &newcon->ipc, con->msgs );
  newcon->source = io_new(fd, NULL, srv_io_received, all_io_closed, newcon);

  if( NULL == newcon->source )
  {
      g_free( newcon );
      close( fd );
      return;
  }

  buf = ipc_mkvers( &size );
  if( NULL == buf )
  {
      g_free( newcon );
      close( fd );
      return;
  }

  io_send_keepdata( newcon->source, buf, size );
}

static unsigned int
srv_io_received( GSource * source SHUTUP, char * data, unsigned int len,
                 void * vdata)
{
    struct constate      * con = vdata;
    struct constate_serv * srv = &con->u.serv;
    ssize_t                res;

    if( IPC_MIN_MSG_LEN > len )
    {
        return 0;
    }

    if( NULL == srv->core )
    {
        destroycon( con );
    }

    res = ipc_parse( &con->ipc, data, len, con );

    if( 0 > res )
    {
        switch( errno )
        {
            case EPERM:
                errmsg( con->u.serv.wind, _("bad IPC protocol version") );
                break;
            case EINVAL:
                errmsg( con->u.serv.wind, _("IPC protocol parse error") );
                break;
            default:
                errmsg( con->u.serv.wind, _("IPC parsing failed: %s"),
                        strerror( errno ) );
        }
        destroycon( con );
        return 0;
    }

    return res;
}

static unsigned int
cli_io_received( GSource * source SHUTUP, char * data, unsigned int len,
                 void * vdata )
{
    struct constate        * con = vdata;
    struct constate_client * cli = &con->u.client;
    ssize_t                  res;

    if( IPC_MIN_MSG_LEN > len )
    {
        return 0;
    }

    res = ipc_parse( &con->ipc, data, len, con );

    if( 0 > res )
    {
        switch( errno )
        {
            case EPERM:
                fprintf( stderr, _("bad IPC protocol version\n") );
                break;
            case EINVAL:
                fprintf( stderr, _("IPC protocol parse error\n") );
                break;
            default:
                fprintf( stderr, _("IPC parsing failed: %s\n"),
                         strerror( errno ) );
                break;
        }
        destroycon( con );
        return 0;
    }

    if( HASVERS( &con->ipc ) && 0 == cli->msgid )
    {
        client_sendmsg( con );
    }

    return res;
}

static void
client_sendmsg( struct constate * con )
{
    struct constate_client * cli = &con->u.client;
    GList                  * ii;
    uint8_t                * buf;
    size_t                   size;
    benc_val_t               packet, * val;
    int                      saved;

    switch( cli->msg )
    {
        case IPC_MSG_ADDMANYFILES:
            val = ipc_initval( &con->ipc, cli->msg, -1, &packet, TYPE_LIST );
            if( NULL == val ||
                tr_bencListReserve( val, g_list_length( cli->files ) ) )
            {
                perror( "malloc" );
                destroycon( con );
                return;
            }
            for( ii = cli->files; NULL != ii; ii = ii->next )
            {
                tr_bencInitStr( tr_bencListAdd( val ), ii->data, -1, 0 );
            }
            buf = ipc_mkval( &packet, &size );
            saved = errno;
            tr_bencFree( &packet );
            g_list_free( cli->files );
            cli->files = NULL;
            break;
        case IPC_MSG_QUIT:
            buf = ipc_mkempty( &con->ipc, &size, cli->msg, -1 );
            saved = errno;
            break;
        default:
            g_assert_not_reached();
            return;
    }

    if( NULL == buf )
    {
        errno = saved;
        perror( "malloc" );
        destroycon( con );
        return;
    }

    cli->msgid = io_send_keepdata( con->source, buf, size );
}

static void
destroycon(struct constate *con) {
  con->source = NULL;

  if(0 <= con->fd)
    close(con->fd);
  con->fd = -1;

  switch(con->type) {
    case CON_SERV:
      break;
    case CON_CLIENT:
      ipc_freemsgs( con->msgs );
      freestrlist(con->u.client.files);
      g_main_loop_quit(con->u.client.loop);
      break;
  }
}

static void
all_io_closed(GSource *source SHUTUP, void *vdata) {
  struct constate *con = vdata;

  destroycon(con);
}

static void
cli_io_sent(GSource *source SHUTUP, unsigned int id, void *vdata) {
  struct constate_client *cli = &((struct constate*)vdata)->u.client;

  if(0 < id && cli->msgid == id) {
    *(cli->succeeded) = TRUE;
    destroycon(vdata);
  }
}

static void
smsg_add( enum ipc_msg id SHUTUP, benc_val_t * val, int64_t tag, void * arg )
{
    struct constate      * con = arg;
    struct constate_serv * srv = &con->u.serv;
    enum tr_torrent_action action;
    benc_val_t           * path;
    int                    ii;

    if( NULL == val || TYPE_LIST != val->type )
    {
        simpleresp( con, tag, IPC_MSG_NOTSUP );
        return;
    }

    action = toraddaction( tr_prefs_get( PREF_ID_ADDIPC ) );
    for( ii = 0; ii < val->val.l.count; ii++ )
    {
        path = val->val.l.vals + ii;
        if( TYPE_STR == path->type &&
            /* XXX somehow escape invalid utf-8 */
            g_utf8_validate( path->val.s.s, path->val.s.i, NULL ) )
        {
            tr_core_add( TR_CORE( srv->core ), path->val.s.s, action, FALSE );
        }
    }
    tr_core_torrents_added( TR_CORE( srv->core ) );

    /* XXX should send info response back with torrent ids */
    simpleresp( con, tag, IPC_MSG_OK );
}

static void
smsg_addone( enum ipc_msg id SHUTUP, benc_val_t * val, int64_t tag,
             void * arg )
{
    struct constate      * con = arg;
    struct constate_serv * srv = &con->u.serv;
    enum tr_torrent_action action;
    benc_val_t           * file, * data, * dir, * start;
    gboolean               paused;

    if( NULL == val || TYPE_DICT != val->type )
    {
        simpleresp( con, tag, IPC_MSG_NOTSUP );
        return;
    }

    file  = tr_bencDictFind( val, "file" );
    data  = tr_bencDictFind( val, "data" );
    dir   = tr_bencDictFind( val, "directory" );
    start = tr_bencDictFind( val, "autostart" );

    if( ( NULL != file  && TYPE_STR != file->type  ) ||
        ( NULL != data  && TYPE_STR != data->type  ) ||
        ( NULL != dir   && TYPE_STR != dir->type   ) ||
        ( NULL != start && TYPE_INT != start->type ) )
    {
        simpleresp( con, tag, IPC_MSG_NOTSUP );
        return;
    }

    action = toraddaction( tr_prefs_get( PREF_ID_ADDIPC ) );
    paused = ( NULL == start || start->val.i ? FALSE : TRUE );
    if( NULL != file )
    {
        if( NULL == dir )
        {
            tr_core_add( srv->core, file->val.s.s, action, paused );
        }
        else
        {
            tr_core_add_dir( srv->core, file->val.s.s, dir->val.s.s,
                             action, paused );
        }
    }
    else
    {
        if( NULL == dir )
        {
            tr_core_add_data( srv->core, data->val.s.s, data->val.s.i,
                              paused );
        }
        else
        {
            tr_core_add_data_dir( srv->core, data->val.s.s, data->val.s.i,
                                  dir->val.s.s, paused );
        }
    }
    tr_core_torrents_added( TR_CORE( srv->core ) );

    /* XXX should send info response back with torrent ids */
    simpleresp( con, tag, IPC_MSG_OK );
}

static void
smsg_quit( enum ipc_msg id SHUTUP, benc_val_t * val SHUTUP, int64_t tag SHUTUP,
           void * arg SHUTUP )
{
    struct constate      * con = arg;
    struct constate_serv * srv = &con->u.serv;

    tr_core_quit( srv->core );
}

static void
smsg_info( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct constate      * con = arg;
    struct constate_serv * srv = &con->u.serv;
    enum ipc_msg           respid;
    benc_val_t           * ids, * types, * idval, packet, * pkval;
    int                    typeflags, ii;
    TrTorrent            * tor;
    uint8_t              * buf;
    size_t                 size;

    if( NULL == val || TYPE_DICT != val->type )
    {
        simpleresp( con, tag, IPC_MSG_NOTSUP );
        return;
    }

    respid = ( IPC_MSG_GETINFO == id ? IPC_MSG_INFO : IPC_MSG_STAT );
    ids    = tr_bencDictFind( val, "id" );
    types  = tr_bencDictFind( val, "types" );
    if( NULL == ids   || TYPE_LIST != ids->type ||
        NULL == types || TYPE_LIST != types->type )
    {
        simpleresp( con, tag, IPC_MSG_NOTSUP );
        return;
    }
    typeflags = ipc_infotypes( respid, types );

    pkval = ipc_initval( &con->ipc, respid, tag, &packet, TYPE_LIST );
    if( NULL == pkval )
    {
        simpleresp( con, tag, IPC_MSG_FAIL );
        return;
    }
    for( ii = 0; ids->val.l.count > ii; ii++ )
    {
        idval = &ids->val.l.vals[ii];
        if( TYPE_INT != idval->type || !TORRENT_ID_VALID( idval->val.i ) ||
            NULL == ( tor = findtorid( srv->core, idval->val.i, NULL ) ) )
        {
            continue;
        }
        if( 0 > addinfo( tor, respid, idval->val.i, typeflags, pkval ) )
        {
            tr_bencFree( &packet );
            simpleresp( con, tag, IPC_MSG_FAIL );
            return;
        }
    }

    buf = ipc_mkval( &packet, &size );
    tr_bencFree( &packet );
    if( NULL == buf )
    {
        simpleresp( con, tag, IPC_MSG_FAIL );
    }
    else
    {
        io_send_keepdata( con->source, buf, size );
    }
}

static void
smsg_infoall( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct constate      * con = arg;
    struct constate_serv * srv = &con->u.serv;
    enum ipc_msg           respid;
    benc_val_t             packet, * pkval;
    int                    typeflags;
    GtkTreeModel         * model;
    GtkTreeIter            iter;
    int                    rowid;
    TrTorrent            * tor;
    uint8_t              * buf;
    size_t                 size;

    if( NULL == val || TYPE_LIST != val->type )
    {
        simpleresp( con, tag, IPC_MSG_NOTSUP );
        return;
    }

    respid = ( IPC_MSG_GETINFOALL == id ? IPC_MSG_INFO : IPC_MSG_STAT );
    typeflags = ipc_infotypes( respid, val );

    pkval = ipc_initval( &con->ipc, respid, tag, &packet, TYPE_LIST );
    if( NULL == pkval )
    {
        simpleresp( con, tag, IPC_MSG_FAIL );
        return;
    }

    model = tr_core_model( srv->core );
    if( gtk_tree_model_get_iter_first( model, &iter ) )
    {
        do
        {
            gtk_tree_model_get( model, &iter, MC_ID, &rowid,
                                MC_TORRENT, &tor, -1 );
            g_object_unref( tor );
            if( 0 > addinfo( tor, respid, rowid, typeflags, pkval ) )
            {
                tr_bencFree( &packet );
                simpleresp( con, tag, IPC_MSG_FAIL );
                return;
            }
        }
        while( gtk_tree_model_iter_next( model, &iter ) );
    }

    buf = ipc_mkval( &packet, &size );
    tr_bencFree( &packet );
    if( NULL == buf )
    {
        simpleresp( con, tag, IPC_MSG_FAIL );
    }
    else
    {
        io_send_keepdata( con->source, buf, size );
    }
}

static int
addinfo( TrTorrent * tor, enum ipc_msg msgid, int torid, int types,
         benc_val_t * val )
{
    tr_info_t * inf;
    tr_stat_t * st;

    inf = tr_torrent_info( tor );
    if( IPC_MSG_INFO == msgid )
    {
        return ipc_addinfo( val, torid, inf, types );
    }
    else
    {
        st = tr_torrent_stat( tor );
        return ipc_addstat( val, torid, inf, st, types );
    }
}

static void
smsg_look( enum ipc_msg id SHUTUP, benc_val_t * val, int64_t tag,
             void * arg )
{
    struct constate      * con = arg;
    struct constate_serv * srv = &con->u.serv;
    benc_val_t             packet, * pkval, * hash;
    int                    ii, torid;
    TrTorrent            * tor;
    tr_info_t            * inf;
    uint8_t              * buf;
    size_t                 size;

    if( NULL == val || TYPE_LIST != val->type )
    {
        simpleresp( con, tag, IPC_MSG_NOTSUP );
        return;
    }

    pkval = ipc_initval( &con->ipc, IPC_MSG_INFO, tag, &packet, TYPE_LIST );
    if( NULL == pkval )
    {
        simpleresp( con, tag, IPC_MSG_FAIL );
        return;
    }

    for( ii = 0; val->val.l.count > ii; ii++ )
    {
        hash = &val->val.l.vals[ii];
        if( NULL == hash || TYPE_STR != hash->type ||
            SHA_DIGEST_LENGTH * 2 != hash->val.s.i ||
            NULL == ( tor = findtorhash( srv->core, hash->val.s.s, &torid ) ) )
        {
            continue;
        }
        inf = tr_torrent_info( tor );
        if( 0 > ipc_addinfo( pkval, torid, inf, IPC_INF_HASH ) )
        {
            tr_bencFree( &packet );
            simpleresp( con, tag, IPC_MSG_FAIL );
            return;
        }
    }

    buf = ipc_mkval( &packet, &size );
    tr_bencFree( &packet );
    if( NULL == buf )
    {
        simpleresp( con, tag, IPC_MSG_FAIL );
    }
    else
    {
        io_send_keepdata( con->source, buf, size );
    }
}

static void
smsg_tor( enum ipc_msg id, benc_val_t * val, int64_t tag, void * arg )
{
    struct constate      * con = arg;
    struct constate_serv * srv = &con->u.serv;
    benc_val_t           * idval;
    TrTorrent            * tor;
    GtkTreeIter            iter;
    int                    ii;

    if( NULL == val || TYPE_LIST != val->type )
    {
        simpleresp( con, tag, IPC_MSG_NOTSUP );
        return;
    }

    for( ii = 0; val->val.l.count > ii; ii++ )
    {
        idval = &val->val.l.vals[ii];
        if( TYPE_INT != idval->type || !TORRENT_ID_VALID( idval->val.i ) ||
            NULL == ( tor = findtorid( srv->core, idval->val.i, &iter ) ) )
        {
            continue;
        }
        switch( id )
        {
            case IPC_MSG_REMOVE:
                tr_core_delete_torrent( srv->core, &iter );
                break;
            case IPC_MSG_START:
                tr_torrent_start( tor );
                break;
            case IPC_MSG_STOP:
                tr_torrent_stop( tor );
                break;
            default:
                g_assert_not_reached();
                break;
        }
    }

    tr_core_update( srv->core );
    tr_core_save( srv->core );

    /* XXX this is a lie */
    simpleresp( con, tag, IPC_MSG_OK );
}

static void
smsg_torall( enum ipc_msg id, benc_val_t * val SHUTUP, int64_t tag,
             void * arg )
{
    struct constate      * con = arg;
    struct constate_serv * srv = &con->u.serv;
    TrTorrent            * tor;
    GtkTreeModel         * model;
    GtkTreeIter            iter;

    model = tr_core_model( srv->core );
    if( gtk_tree_model_get_iter_first( model, &iter ) )
    {
        do
        {
            gtk_tree_model_get( model, &iter, MC_TORRENT, &tor, -1 );
            switch( id )
            {
                case IPC_MSG_REMOVEALL:
                    tr_core_delete_torrent( srv->core, &iter );
                    break;
                case IPC_MSG_STARTALL:
                    tr_torrent_start( tor );
                    break;
                case IPC_MSG_STOPALL:
                    tr_torrent_stop( tor );
                    break;
                default:
                    g_assert_not_reached();
                    break;
            }
            g_object_unref( tor );
        }
        while( gtk_tree_model_iter_next( model, &iter ) );
    }

    tr_core_update( srv->core );
    tr_core_save( srv->core );

    /* XXX this is a lie */
    simpleresp( con, tag, IPC_MSG_OK );
}

static void
all_default( enum ipc_msg id, benc_val_t * val SHUTUP, int64_t tag, void * arg )
{
    switch( id )
    {
        case IPC_MSG_FAIL:
        case IPC_MSG_NOTSUP:
        case IPC_MSG_OK:
            break;
        case IPC_MSG_NOOP:
            simpleresp( arg, tag, IPC_MSG_OK );
            break;
        default:
            simpleresp( arg, tag, IPC_MSG_NOTSUP );
            break;
    }
}

static gboolean
simpleresp( struct constate * con, int64_t tag, enum ipc_msg id )
{
    uint8_t         * buf;
    size_t            size;

    buf = ipc_mkempty( &con->ipc, &size, id, tag );
    if( NULL == buf )
    {
        return FALSE;
    }

    io_send_keepdata( con->source, buf, size );

    return TRUE;
}

static TrTorrent *
findtorid( TrCore * core, int id, GtkTreeIter * iter )
{
    GtkTreeModel * model;
    GtkTreeIter    myiter;
    int            rowid;
    TrTorrent    * tor;

    if( NULL == iter )
    {
        iter = &myiter;
    }

    model = tr_core_model( core );
    if( gtk_tree_model_get_iter_first( model, iter ) )
    {
        do
        {
            gtk_tree_model_get( model, iter, MC_ID, &rowid, -1 );
            if( rowid == id )
            {
                gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
                g_object_unref( tor );
                return tor;
            }
        }
        while( gtk_tree_model_iter_next( model, iter ) );
    }

    return NULL;
}

static TrTorrent *
findtorhash( TrCore * core, const char * hash, int * torid )
{
    GtkTreeModel * model;
    GtkTreeIter    iter;
    char         * rowhash;
    TrTorrent    * tor;

    model = tr_core_model( core );
    if( gtk_tree_model_get_iter_first( model, &iter ) )
    {
        do
        {
            gtk_tree_model_get( model, &iter, MC_HASH, &rowhash, -1 );
            if( 0 == strcmp( hash, rowhash ) )
            {
                gtk_tree_model_get( model, &iter, MC_ID, torid,
                                    MC_TORRENT, &tor, -1 );
                g_object_unref( tor );
                return tor;
            }
        }
        while( gtk_tree_model_iter_next( model, &iter ) );
    }

    return NULL;
}
