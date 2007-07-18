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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "io.h"
#include "util.h"

#define IO_BLOCKSIZE            (1024)

struct iosource {
  GSource source;
  GPollFD infd;
  GPollFD outfd;
  ioidfunc_t sent;
  iodatafunc_t received;
  ionewfunc_t accepted;
  iofunc_t closed;
  void *cbdata;
  char *inbuf;
  size_t inused;
  size_t inmax;
  GList *outbufs;
  unsigned int lastid;
};

struct iooutbuf {
  char *data;
  size_t len;
  size_t off;
  unsigned int id;
};

static gboolean
nonblock(int fd);
static struct iosource *
newsource(void);
static void
freeoutbuf(struct iooutbuf *buf);
static gboolean
io_prepare(GSource *source, gint *timeout_);
static gboolean
io_check(GSource *source);
static gboolean
io_dispatch(GSource *source, GSourceFunc callback, gpointer gdata);
static void
io_finalize(GSource *source);
static void
io_accept(struct iosource *io);
static void
io_read(struct iosource *io);
static void
io_write(struct iosource *io);
static void
io_disconnect(struct iosource *io, int err);

static GSourceFuncs sourcefuncs = {
  io_prepare,
  io_check,
  io_dispatch,
  io_finalize,
  NULL,
  NULL
};

GSource *
io_new(int fd, ioidfunc_t sent, iodatafunc_t received,
       iofunc_t closed, void *cbdata) {
  struct iosource *io;

  if(!nonblock(fd))
    return NULL;

  io = newsource();
  io->sent = sent;
  io->received = received;
  io->closed = closed;
  io->cbdata = cbdata;
  io->infd.fd = fd;
  io->infd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
  io->infd.revents = 0;
  io->outfd.fd = fd;
  io->outfd.events = G_IO_OUT | G_IO_ERR;
  io->outfd.revents = 0;

  g_source_add_poll((GSource*)io, &io->infd);
  g_source_attach((GSource*)io, NULL);

  return (GSource*)io;
}

GSource *
io_new_listening(int fd, socklen_t len, ionewfunc_t accepted,
                 iofunc_t closed, void *cbdata) {
  struct iosource *io;

  g_assert(NULL != accepted);

  if(!nonblock(fd))
    return NULL;

  io = newsource();
  io->accepted = accepted;
  io->closed = closed;
  io->cbdata = cbdata;
  io->infd.fd = fd;
  io->infd.events = G_IO_IN | G_IO_ERR;
  io->infd.revents = 0;
  io->inbuf = g_new(char, len);
  io->inmax = len;

  g_source_add_poll((GSource*)io, &io->infd);
  g_source_attach((GSource*)io, NULL);

  return (GSource*)io;
}

static gboolean
nonblock(int fd) {
  int flags;

  if(0 > (flags = fcntl(fd, F_GETFL)) ||
     0 > fcntl(fd, F_SETFL, flags | O_NONBLOCK))
    return FALSE;

  return TRUE;
}

static struct iosource *
newsource(void) {
  GSource *source = g_source_new(&sourcefuncs, sizeof(struct iosource));
  struct iosource *io = (struct iosource*)source;

  io->sent = NULL;
  io->received = NULL;
  io->accepted = NULL;
  io->closed = NULL;
  io->cbdata = NULL;
  memset(&io->infd, 0,  sizeof(io->infd));
  io->infd.fd = -1;
  memset(&io->outfd, 0,  sizeof(io->outfd));
  io->outfd.fd = -1;
  io->inbuf = NULL;
  io->inused = 0;
  io->inmax = 0;
  io->outbufs = NULL;
  io->lastid = 0;

  return io;
}

unsigned int
io_send(GSource *source, const void *data, size_t len) {
  char *new = g_new(char, len);

  memcpy(new, data, len);

  return io_send_keepdata(source, new, len);
}

unsigned int
io_send_keepdata(GSource *source, void *data, size_t len) {
  struct iosource *io = (struct iosource*)source;
  struct iooutbuf *buf = g_new(struct iooutbuf, 1);

  buf->data = data;
  buf->len = len;
  buf->off = 0;
  io->lastid++;
  buf->id = io->lastid;

  if(NULL != io->outbufs)
    io->outbufs = g_list_append(io->outbufs, buf);
  else {
    io->outbufs = g_list_append(io->outbufs, buf);
    g_source_add_poll(source, &io->outfd);
  }

  return io->lastid;
}

static void
freeoutbuf(struct iooutbuf *buf) {
  if(NULL != buf->data)
    g_free(buf->data);
  g_free(buf);
}

static gboolean
io_prepare(GSource *source SHUTUP, gint *timeout_) {
  *timeout_ = -1;
  return FALSE;
}

static gboolean
io_check(GSource *source) {
  struct iosource *io = (struct iosource*)source;

  if(io->infd.revents)
    return TRUE;
  if(NULL != io->outbufs && io->outfd.revents)
    return TRUE;
  else
    return FALSE;
}

static gboolean
io_dispatch(GSource *source, GSourceFunc callback SHUTUP,
            gpointer gdata SHUTUP) {
  struct iosource *io = (struct iosource*)source;

  if(io->infd.revents & (G_IO_ERR | G_IO_HUP) ||
     io->outfd.revents & G_IO_ERR)
    io_disconnect(io, 0 /* XXX how do I get errors here? */ );
  else if(io->infd.revents & G_IO_IN)
    (NULL == io->accepted ? io_read : io_accept)(io);
  else if(io->outfd.revents & G_IO_OUT)
    io_write(io);
  else
    return FALSE;

  return TRUE;
}


static void
io_finalize(GSource *source SHUTUP) {
  struct iosource *io = (struct iosource*)source;

  if(NULL != io->outbufs) {
    g_list_foreach(io->outbufs, (GFunc)freeoutbuf, NULL);
    g_list_free(io->outbufs);
  }

  if(NULL != io->inbuf)
    g_free(io->inbuf);
}

static void
io_biggify(char **buf, size_t used, size_t *max) {
  if(used + IO_BLOCKSIZE > *max) {
    *max += IO_BLOCKSIZE;
    *buf = g_renew(char, *buf, *max);
  }
}

static void
io_accept(struct iosource *io) {
  int fd;
  socklen_t len;

  len = io->inmax;
  if(0 > (fd = accept(io->infd.fd, (struct sockaddr*)io->inbuf, &len))) {
    if(EAGAIN == errno || ECONNABORTED == errno || EWOULDBLOCK == errno)
      return;
    io_disconnect(io, errno);
  }

  io->accepted((GSource*)io, fd, (struct sockaddr*)io->inbuf, len, io->cbdata);
}

static void
io_read(struct iosource *io) {
  ssize_t res = 0;
  gboolean newdata = FALSE;
  size_t used;
  int err = 0;

  g_source_ref((GSource*)io);

  do {
    if(!newdata && 0 < res)
      newdata = TRUE;
    io->inused += res;
    io_biggify(&io->inbuf, io->inused, &io->inmax);
    errno = 0;
    res = read(io->infd.fd, io->inbuf + io->inused, io->inmax - io->inused);
    if(0 > res)
      err = errno;
  } while(0 < res);

  if(NULL == io->received)
    io->inused = 0;
  else if(newdata) {
    used = io->received((GSource*)io, io->inbuf, io->inused, io->cbdata);
    if(used > io->inused)
      used = io->inused;
    if(0 < used) {
      if(used < io->inused)
        memmove(io->inbuf, io->inbuf + used, io->inused - used);
      io->inused -= used;
    }
  }

  if(0 != err && EAGAIN != err)
    io_disconnect(io, err);
  else if(0 == res)
    io_disconnect(io, 0);
  g_source_unref((GSource*)io);
}

static void
io_write(struct iosource *io) {
  struct iooutbuf *buf;
  ssize_t res = 1;
  int err = 0;

  g_source_ref((GSource*)io);

  while(NULL != io->outbufs && 0 == err) {
    buf = io->outbufs->data;
    while(buf->off < buf->len && 0 < res) {
      errno = 0;
      res = write(io->outfd.fd, buf->data + buf->off, buf->len - buf->off);
      if(0 > res)
        err = errno;
      else
        buf->off += res;
    }

    if(buf->off >= buf->len) {
      io->outbufs = g_list_remove(io->outbufs, buf);
      if(NULL == io->outbufs)
        g_source_remove_poll((GSource*)io, &io->outfd);
      if(NULL != io->sent)
        io->sent((GSource*)io, buf->id, io->cbdata);
      freeoutbuf(buf);
    }
  }

  if(0 != err && EAGAIN != err)
    io_disconnect(io, err);

  g_source_unref((GSource*)io);
}

static void
io_disconnect(struct iosource *io, int err) {
  if(NULL != io->closed) {
    errno = err;
    io->closed((GSource*)io, io->cbdata);
  }

  if(NULL != io->outbufs)
    g_source_remove_poll((GSource*)io, &io->outfd);

  g_source_remove_poll((GSource*)io, &io->infd);
  g_source_remove(g_source_get_id((GSource*)io));
  g_source_unref((GSource*)io);
}
