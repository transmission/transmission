/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <limits.h> /* NAME_MAX */
#include <stdlib.h> /* realloc () */

#include <unistd.h> /* close () */

#include <sys/inotify.h>

#include <event2/bufferevent.h>
#include <event2/event.h>

#define __LIBTRANSMISSION_WATCHDIR_MODULE__

#include "transmission.h"
#include "log.h"
#include "utils.h"
#include "watchdir.h"
#include "watchdir-common.h"

/***
****
***/

#define log_error(...) (!tr_logLevelIsActive (TR_LOG_ERROR) ? (void) 0 : \
  tr_logAddMessage (__FILE__, __LINE__, TR_LOG_ERROR, "watchdir:inotify", __VA_ARGS__))

/***
****
***/

typedef struct tr_watchdir_inotify
{
  tr_watchdir_backend   base;

  int                   infd;
  int                   inwd;
  struct bufferevent  * event;
}
tr_watchdir_inotify;

#define BACKEND_UPCAST(b) ((tr_watchdir_inotify *) (b))

#define INOTIFY_WATCH_MASK (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)

/***
****
***/

static void
tr_watchdir_inotify_on_first_scan (evutil_socket_t   fd UNUSED,
                                   short             type UNUSED,
                                   void            * context)
{
  const tr_watchdir_t handle = context;

  tr_watchdir_scan (handle, NULL);
}

static void
tr_watchdir_inotify_on_event (struct bufferevent * event,
                              void               * context)
{
  assert (context != NULL);

  const tr_watchdir_t handle = context;
  tr_watchdir_inotify * const backend = BACKEND_UPCAST (tr_watchdir_get_backend (handle));
  struct inotify_event ev;
  size_t nread;
  size_t name_size = NAME_MAX + 1;
  char * name = tr_new (char, name_size);

  /* Read the size of the struct excluding name into buf. Guaranteed to have at
     least sizeof (ev) available */
  while ((nread = bufferevent_read (event, &ev, sizeof (ev))) != 0)
    {
      if (nread == (size_t) -1)
        {
          log_error ("Failed to read inotify event: %s", tr_strerror (errno));
          break;
        }

      if (nread != sizeof (ev))
        {
          log_error ("Failed to read inotify event: expected %zu, got %zu bytes.",
                     sizeof (ev), nread);
          break;
        }

      assert (ev.wd == backend->inwd);
      assert ((ev.mask & INOTIFY_WATCH_MASK) != 0);
      assert (ev.len > 0);

      if (ev.len > name_size)
        {
          name_size = ev.len;
          name = tr_renew (char, name, name_size);
        }

      /* Consume entire name into buffer */
      if ((nread = bufferevent_read (event, name, ev.len)) == (size_t) -1)
        {
          log_error ("Failed to read inotify name: %s", tr_strerror (errno));
          break;
        }

      if (nread != ev.len)
        {
          log_error ("Failed to read inotify name: expected %" PRIu32 ", got %zu bytes.",
                     ev.len, nread);
          break;
        }

      tr_watchdir_process (handle, name);
    }

  tr_free (name);
}

static void
tr_watchdir_inotify_free (tr_watchdir_backend * backend_base)
{
  tr_watchdir_inotify * const backend = BACKEND_UPCAST (backend_base);

  if (backend == NULL)
    return;

  assert (backend->base.free_func == &tr_watchdir_inotify_free);

  if (backend->event != NULL)
    {
       bufferevent_disable (backend->event, EV_READ);
       bufferevent_free (backend->event);
    }

  if (backend->infd != -1)
    {
      if (backend->inwd != -1)
        inotify_rm_watch (backend->infd, backend->inwd);
      close (backend->infd);
    }

  tr_free (backend);
}

tr_watchdir_backend *
tr_watchdir_inotify_new (tr_watchdir_t handle)
{
  const char * const path = tr_watchdir_get_path (handle);
  tr_watchdir_inotify * backend;

  backend = tr_new0 (tr_watchdir_inotify, 1);
  backend->base.free_func = &tr_watchdir_inotify_free;
  backend->infd = -1;
  backend->inwd = -1;

  if ((backend->infd = inotify_init ()) == -1)
    {
      log_error ("Unable to inotify_init: %s", tr_strerror (errno));
      goto fail;
    }

  if ((backend->inwd = inotify_add_watch (backend->infd, path,
                                          INOTIFY_WATCH_MASK | IN_ONLYDIR)) == -1)
    {
      log_error ("Failed to setup watchdir \"%s\": %s (%d)", path,
                 tr_strerror (errno), errno);
      goto fail;
    }

  if ((backend->event = bufferevent_socket_new (tr_watchdir_get_event_base (handle),
                                                backend->infd, 0)) == NULL)
    {
      log_error ("Failed to create event buffer: %s", tr_strerror (errno));
      goto fail;
    }

  /* Guarantees at least the sizeof an inotify event will be available in the
     event buffer */
  bufferevent_setwatermark (backend->event, EV_READ, sizeof (struct inotify_event), 0);
  bufferevent_setcb (backend->event, &tr_watchdir_inotify_on_event, NULL, NULL, handle);
  bufferevent_enable (backend->event, EV_READ);

  /* Perform an initial scan on the directory */
  if (event_base_once (tr_watchdir_get_event_base (handle), -1, EV_TIMEOUT,
                       &tr_watchdir_inotify_on_first_scan, handle, NULL) == -1)
    log_error ("Failed to perform initial scan: %s", tr_strerror (errno));

  return BACKEND_DOWNCAST (backend);

fail:
  tr_watchdir_inotify_free (BACKEND_DOWNCAST (backend));
  return NULL;
}
