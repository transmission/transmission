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

#include "conf.h"
#include "io.h"
#include "ipc.h"
#include "tr_prefs.h"
#include "util.h"

/* IPC protocol version */
#define PROTO_VERS_MIN          ( 1 )
#define PROTO_VERS_MAX          ( 1 )

/* int, IPC protocol version */
#define MSG_VERSION             ("version")
/* list of strings, full paths to torrent files to load */
#define MSG_ADDFILES            ("addfiles")
/* request that the server quit */
#define MSG_QUIT                ("quit")

enum contype { CON_SERV, CON_CLIENT };

struct constate_serv {
  void *wind;
  add_torrents_func_t addfunc;
  callbackfunc_t quitfunc;
  void *cbdata;
};

enum client_cmd { CCMD_ADD, CCMD_QUIT };

struct constate_client {
  GMainLoop *loop;
  enum client_cmd cmd;
  GList *files;
  gboolean *succeeded;
  unsigned int msgid;
};

struct constate;
typedef void (*handler_func_t)(struct constate*, const char*, benc_val_t *);
struct handlerdef {char *name; handler_func_t func;};

struct constate {
  GSource *source;
  int fd;
  int vers;
  const struct handlerdef *funcs;
  enum contype type;
  union {
    struct constate_serv serv;
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
static int
send_msg(struct constate *con, const char *name, benc_val_t *val);
static int
send_msg_vers_new(struct constate *con);
static int
send_msg_vers_old(struct constate *con);
static unsigned int
all_io_received(GSource *source, char *data, unsigned int len, void *vdata);
static void
destroycon(struct constate *con);
static void
all_io_closed(GSource *source, void *vdata);
static void
srv_vers(struct constate *con, const char *name, benc_val_t *val);
static void
srv_addfile(struct constate *con, const char *name, benc_val_t *val);
static void
srv_quit( struct constate * con, const char * name, benc_val_t * val );
static void
afc_version(struct constate *con, const char *name, benc_val_t *val);
static void
afc_io_sent(GSource *source, unsigned int id, void *vdata);
static int
ipc_checkversion( benc_val_t * vers );
static int
getvers( benc_val_t * dict, const char * key );

static const struct handlerdef gl_funcs_serv[] = {
  {MSG_VERSION,  srv_vers},
  {MSG_ADDFILES, srv_addfile},
  {MSG_QUIT,     srv_quit},
  {NULL, NULL}
};

static const struct handlerdef gl_funcs_client[] = {
  {MSG_VERSION, afc_version},
  {NULL, NULL}
};

/* this is only used on the server */
static char *gl_sockpath = NULL;

void
ipc_socket_setup( void * parent, add_torrents_func_t addfunc,
                  callbackfunc_t quitfunc, void * cbdata )
{
  struct constate *con;

  con = g_new0(struct constate, 1);
  con->source = NULL;
  con->fd = -1;
  con->vers = -1;
  con->funcs = gl_funcs_serv;
  con->type = CON_SERV;
  con->u.serv.wind = parent;
  con->u.serv.addfunc = addfunc;
  con->u.serv.quitfunc = quitfunc;
  con->u.serv.cbdata = cbdata;
  
  serv_bind(con);
}

static gboolean
blocking_client( enum client_cmd cmd, GList * files )
{

  struct constate *con;
  char *path;
  gboolean ret = FALSE;

  con = g_new0(struct constate, 1);
  con->source = NULL;
  con->fd = -1;
  con->vers = -1;
  con->funcs = gl_funcs_client;
  con->type = CON_CLIENT;
  con->u.client.loop = g_main_loop_new(NULL, TRUE);
  con->u.client.cmd = cmd;
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
    return blocking_client( CCMD_ADD, files );
}

gboolean
ipc_sendquit_blocking( void )
{
    return blocking_client( CCMD_QUIT, NULL );
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

  con->source = io_new(con->fd, afc_io_sent, all_io_received,
                       all_io_closed, con);

  if( NULL != con->source )
  {
      send_msg_vers_new( con );
  }

  return TRUE;
}

static void
srv_io_accept(GSource *source SHUTUP, int fd, struct sockaddr *sa SHUTUP,
              socklen_t len SHUTUP, void *vdata) {
  struct constate *con = vdata;
  struct constate *newcon;

  newcon = g_new(struct constate, 1);
  memcpy(newcon, con, sizeof(*newcon));
  newcon->fd = fd;
  newcon->source = io_new(fd, NULL, all_io_received, all_io_closed, newcon);

  /* XXX need to check for incoming version from client */

  if(NULL != newcon->source)
    /* XXX need to switch to new version scheme after the next release */
    send_msg_vers_old( newcon );
  else {
    g_free(newcon);
    close(fd);
  }
}

static int
send_msg(struct constate *con, const char *name, benc_val_t *val) {
  char *buf;
  int used, total;
  benc_val_t dict;
  char stupid;

  /* construct a dictionary value */
  /* XXX I shouldn't be constructing benc_val_t's by hand */
  bzero(&dict, sizeof(dict));
  dict.type = TYPE_DICT;
  dict.val.l.alloc = 2;
  dict.val.l.count = 2;
  dict.val.l.vals = g_new0(benc_val_t, 2);
  dict.val.l.vals[0].type = TYPE_STR;
  dict.val.l.vals[0].val.s.i = strlen(name);
  dict.val.l.vals[0].val.s.s = (char*)name;
  dict.val.l.vals[1] = *val;

  /* bencode the dictionary, starting at offset 8 in the buffer */
  buf = malloc(9);
  g_assert(NULL != buf);
  total = 9;
  used = 8;
  if(tr_bencSave(&dict, &buf, &used, &total)) {
    g_assert_not_reached();
  }
  g_free(dict.val.l.vals);

  /* write the bencoded data length into the first 8 bytes of the buffer */
  stupid = buf[8];
  snprintf(buf, 9, "%08X", (unsigned int)used - 8);
  buf[8] = stupid;

  /* send the data */
  return io_send_keepdata(con->source, buf, used);
}

static int
send_msg_vers_new( struct constate * con )
{
    benc_val_t dict;

    /* XXX ugh, I need to merge the pex branch and use it's benc funcs */
    bzero( &dict, sizeof dict );
    dict.type                  = TYPE_DICT;
    dict.val.l.alloc           = 4;
    dict.val.l.count           = 4;
    dict.val.l.vals            = g_new0( benc_val_t, 4 );
    dict.val.l.vals[0].type    = TYPE_STR;
    dict.val.l.vals[0].val.s.i = 3;
    dict.val.l.vals[0].val.s.s = "min";
    dict.val.l.vals[1].type    = TYPE_INT;
    dict.val.l.vals[1].val.i   = PROTO_VERS_MIN;
    dict.val.l.vals[2].type    = TYPE_STR;
    dict.val.l.vals[2].val.s.i = 3;
    dict.val.l.vals[2].val.s.s = "max";
    dict.val.l.vals[3].type    = TYPE_INT;
    dict.val.l.vals[3].val.i   = PROTO_VERS_MAX;

    return send_msg( con, MSG_VERSION, &dict );
}

static int
send_msg_vers_old( struct constate * con )
{
    benc_val_t val;

    bzero( &val, sizeof val );
    val.type  = TYPE_INT;
    val.val.i = PROTO_VERS_MIN;

    return send_msg( con, MSG_VERSION, &val );
}

static unsigned int
all_io_received(GSource *source, char *data, unsigned int len, void *vdata) {
  struct constate *con = vdata;
  size_t size;
  char stupid;
  char *end;
  benc_val_t msgs;
  int ii, jj;

  if(9 > len)
    return 0;

  stupid = data[8];
  data[8] = '\0';
  size = strtoul(data, NULL, 16);
  data[8] = stupid;

  if(size + 8 > len)
    return 0;

  if(!tr_bencLoad(data + 8, size, &msgs, &end) && TYPE_DICT == msgs.type) {
    for(ii = 0; msgs.val.l.count > ii + 1; ii += 2)
      if(TYPE_STR == msgs.val.l.vals[ii].type)
        for(jj = 0; NULL != con->funcs[jj].name; jj++)
          if(0 == strcmp(msgs.val.l.vals[ii].val.s.s, con->funcs[jj].name)) {
            con->funcs[jj].func(con, msgs.val.l.vals[ii].val.s.s,
                                msgs.val.l.vals + ii + 1);
            break;
          }
    tr_bencFree(&msgs);
  }

  return size + 8 +
    all_io_received(source, data + size + 8, len - size - 8, con);
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
srv_vers( struct constate * con, const char * name SHUTUP, benc_val_t * val )
{
    if( 0 > con->vers )
    {
        con->vers = ipc_checkversion( val );
        if( 0 > con->vers )
        {
            fprintf( stderr, _("bad IPC protocol version\n") );
            destroycon( con );
            return;
        }
    }
}

static void
srv_addfile(struct constate *con, const char *name SHUTUP, benc_val_t *val) {
  struct constate_serv *srv = &con->u.serv;
  GList *files;
  int ii;

  if(TYPE_LIST == val->type) {
    files = NULL;
    for(ii = 0; ii < val->val.l.count; ii++)
      if(TYPE_STR == val->val.l.vals[ii].type &&
         /* XXX somehow escape invalid utf-8 */
         g_utf8_validate(val->val.l.vals[ii].val.s.s, -1, NULL))
        files = g_list_append(files, val->val.l.vals[ii].val.s.s);
    srv->addfunc( srv->cbdata, NULL, files, NULL,
                  toraddaction( tr_prefs_get( PREF_ID_ADDIPC ) ), FALSE );
    g_list_free(files);
  }
}

static void
srv_quit( struct constate * con, const char * name SHUTUP,
          benc_val_t * val SHUTUP )
{
    struct constate_serv * srv;

    srv = &con->u.serv;
    srv->quitfunc( srv->cbdata );
}

static void
afc_version(struct constate *con, const char *name SHUTUP, benc_val_t *val) {
  struct constate_client *afc = &con->u.client;
  GList *file;
  benc_val_t list, *str;

  if( 0 > con->vers )
  {
      con->vers = ipc_checkversion( val );
      if( 0 > con->vers )
      {
          fprintf( stderr, _("bad IPC protocol version\n") );
          destroycon( con );
          return;
      }
  }
  else
  {
      return;
  }

  /* XXX handle getting a non-version tag, invalid data,
     or nothing (read timeout) */
  switch( afc->cmd )
  {
      case CCMD_ADD:
          list.type = TYPE_LIST;
          list.val.l.alloc = g_list_length(afc->files);
          list.val.l.count = 0;
          list.val.l.vals = g_new0(benc_val_t, list.val.l.alloc);
          for(file = afc->files; NULL != file; file = file->next) {
              str = list.val.l.vals + list.val.l.count;
              str->type = TYPE_STR;
              str->val.s.i = strlen(file->data);
              str->val.s.s = file->data;
              list.val.l.count++;
          }
          g_list_free(afc->files);
          afc->files = NULL;
          afc->msgid = send_msg(con, MSG_ADDFILES, &list);
          tr_bencFree(&list);
          break;
      case CCMD_QUIT:
          bzero( &list, sizeof( list ) );
          list.type  = TYPE_STR;
          afc->msgid = send_msg( con, MSG_QUIT, &list );
          break;
  }
}

static void
afc_io_sent(GSource *source SHUTUP, unsigned int id, void *vdata) {
  struct constate_client *afc = &((struct constate*)vdata)->u.client;

  if(0 < id && afc->msgid == id) {
    *(afc->succeeded) = TRUE;
    destroycon(vdata);
  }
}

int
ipc_checkversion( benc_val_t * vers )
{
    int min, max;

    if( TYPE_INT == vers->type )
    {
        if( 0 > vers->val.i )
        {
            return -1;
        }
        min = max = vers->val.i;
    }
    else if( TYPE_DICT == vers->type )
    {
        min = getvers( vers, "min" );
        max = getvers( vers, "max" );
        if( 0 > min || 0 > max )
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    g_assert( PROTO_VERS_MIN <= PROTO_VERS_MAX );
    if( min > max || PROTO_VERS_MAX < min || PROTO_VERS_MIN > max )
    {
        return -1;
    }

    return MIN( PROTO_VERS_MAX, max );
}

int
getvers( benc_val_t * dict, const char * key )
{
    benc_val_t * val;

    val = tr_bencDictFind( dict, key );
    if( NULL == val || TYPE_INT != val->type || 0 > val->val.i )
    {
        return -1;
    }

    return val->val.i;
}
