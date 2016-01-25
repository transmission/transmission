/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* strcmp () */

#include <event2/event.h>
#include <event2/util.h>

#define __LIBTRANSMISSION_WATCHDIR_MODULE__

#include "transmission.h"
#include "error.h"
#include "error-types.h"
#include "file.h"
#include "log.h"
#include "ptrarray.h"
#include "utils.h"
#include "watchdir.h"
#include "watchdir-common.h"

/***
****
***/

#define log_debug(...) (!tr_logLevelIsActive (TR_LOG_DEBUG) ? (void) 0 : \
  tr_logAddMessage (__FILE__, __LINE__, TR_LOG_DEBUG, "watchdir", __VA_ARGS__))

#define log_error(...) (!tr_logLevelIsActive (TR_LOG_ERROR) ? (void) 0 : \
  tr_logAddMessage (__FILE__, __LINE__, TR_LOG_ERROR, "watchdir", __VA_ARGS__))

/***
****
***/

struct tr_watchdir
{
  char                * path;
  tr_watchdir_cb        callback;
  void                * callback_user_data;
  struct event_base   * event_base;
  tr_watchdir_backend * backend;
  tr_ptrArray           active_retries;
};

/***
****
***/

static bool
is_regular_file (const char * dir,
                 const char * name)
{
  char * const path = tr_buildPath (dir, name, NULL);
  tr_sys_path_info path_info;
  tr_error * error = NULL;
  bool ret;

  if ((ret = tr_sys_path_get_info (path, 0, &path_info, &error)))
    {
      ret = path_info.type == TR_SYS_PATH_IS_FILE;
    }
  else
    {
      if (!TR_ERROR_IS_ENOENT (error->code))
        log_error ("Failed to get type of \"%s\" (%d): %s", path, error->code,
                   error->message);
      tr_error_free (error);
    }

  tr_free (path);
  return ret;
}

static const char *
watchdir_status_to_string (tr_watchdir_status status)
{
  switch (status)
    {
      case TR_WATCHDIR_ACCEPT:
        return "accept";
      case TR_WATCHDIR_IGNORE:
        return "ignore";
      case TR_WATCHDIR_RETRY:
        return "retry";
      default:
        return "???";
    }
}

static tr_watchdir_status
tr_watchdir_process_impl (tr_watchdir_t   handle,
                          const char    * name)
{
  /* File may be gone while we're retrying */
  if (!is_regular_file (tr_watchdir_get_path (handle), name))
    return TR_WATCHDIR_IGNORE;

  const tr_watchdir_status ret = handle->callback (handle, name, handle->callback_user_data);

  assert (ret == TR_WATCHDIR_ACCEPT ||
          ret == TR_WATCHDIR_IGNORE ||
          ret == TR_WATCHDIR_RETRY);

  log_debug ("Callback decided to %s file \"%s\"", watchdir_status_to_string (ret), name);

  return ret;
}

/***
****
***/

typedef struct tr_watchdir_retry
{
  tr_watchdir_t    handle;
  char           * name;
  unsigned int     counter;
  struct event   * timer;
  struct timeval   interval;
}
tr_watchdir_retry;

/* Non-static and mutable for unit tests */
unsigned int   tr_watchdir_retry_limit          = 3;
struct timeval tr_watchdir_retry_start_interval = { 1, 0 };
struct timeval tr_watchdir_retry_max_interval   = { 10, 0 };

#define tr_watchdir_retries_init(r)      (void) 0
#define tr_watchdir_retries_destroy(r)   tr_ptrArrayDestruct ((r), (PtrArrayForeachFunc) &tr_watchdir_retry_free)
#define tr_watchdir_retries_insert(r, v) tr_ptrArrayInsertSorted ((r), (v), &compare_retry_names)
#define tr_watchdir_retries_remove(r, v) tr_ptrArrayRemoveSortedPointer ((r), (v), &compare_retry_names)
#define tr_watchdir_retries_find(r, v)   tr_ptrArrayFindSorted ((r), (v), &compare_retry_names)

static int
compare_retry_names (const void * a,
                     const void * b)
{
  return strcmp (((tr_watchdir_retry *) a)->name, ((tr_watchdir_retry *) b)->name);
}

static void
tr_watchdir_retry_free (tr_watchdir_retry * retry);

static void
tr_watchdir_on_retry_timer (evutil_socket_t   fd UNUSED,
                            short             type UNUSED,
                            void            * context)
{
  assert (context != NULL);

  tr_watchdir_retry * const retry = context;
  const tr_watchdir_t handle = retry->handle;

  if (tr_watchdir_process_impl (handle, retry->name) == TR_WATCHDIR_RETRY)
    {
      if (++retry->counter < tr_watchdir_retry_limit)
        {
          evutil_timeradd (&retry->interval, &retry->interval, &retry->interval);
          if (evutil_timercmp (&retry->interval, &tr_watchdir_retry_max_interval, >))
            retry->interval = tr_watchdir_retry_max_interval;

          evtimer_del (retry->timer);
          evtimer_add (retry->timer, &retry->interval);
          return;
        }

      log_error ("Failed to add (corrupted?) torrent file: %s", retry->name);
    }

  tr_watchdir_retries_remove (&handle->active_retries, retry);
  tr_watchdir_retry_free (retry);
}

static tr_watchdir_retry *
tr_watchdir_retry_new (tr_watchdir_t   handle,
                       const char    * name)
{
  tr_watchdir_retry * retry;

  retry = tr_new0 (tr_watchdir_retry, 1);
  retry->handle = handle;
  retry->name = tr_strdup (name);
  retry->timer = evtimer_new (handle->event_base, &tr_watchdir_on_retry_timer, retry);
  retry->interval = tr_watchdir_retry_start_interval;

  evtimer_add (retry->timer, &retry->interval);

  return retry;
}

static void
tr_watchdir_retry_free (tr_watchdir_retry * retry)
{
  if (retry == NULL)
    return;

  if (retry->timer != NULL)
    {
      evtimer_del (retry->timer);
      event_free (retry->timer);
    }

  tr_free (retry->name);
  tr_free (retry);
}

static void
tr_watchdir_retry_restart (tr_watchdir_retry * retry)
{
  assert (retry != NULL);

  evtimer_del (retry->timer);

  retry->counter = 0;
  retry->interval = tr_watchdir_retry_start_interval;

  evtimer_add (retry->timer, &retry->interval);
}

/***
****
***/

tr_watchdir_t
tr_watchdir_new (const char        * path,
                 tr_watchdir_cb      callback,
                 void              * callback_user_data,
                 struct event_base * event_base,
                 bool                force_generic)
{
  tr_watchdir_t handle;

  handle = tr_new0 (struct tr_watchdir, 1);
  handle->path = tr_strdup (path);
  handle->callback = callback;
  handle->callback_user_data = callback_user_data;
  handle->event_base = event_base;
  tr_watchdir_retries_init (&handle->active_retries);

  if (!force_generic)
    {
#ifdef WITH_INOTIFY
      if (handle->backend == NULL)
        handle->backend = tr_watchdir_inotify_new (handle);
#endif
#ifdef WITH_KQUEUE
      if (handle->backend == NULL)
        handle->backend = tr_watchdir_kqueue_new (handle);
#endif
#ifdef _WIN32
      if (handle->backend == NULL)
        handle->backend = tr_watchdir_win32_new (handle);
#endif
    }

  if (handle->backend == NULL)
    handle->backend = tr_watchdir_generic_new (handle);

  if (handle->backend == NULL)
    {
      tr_watchdir_free (handle);
      handle = NULL;
    }
  else
    {
      assert (handle->backend->free_func != NULL);
    }

  return handle;
}

void
tr_watchdir_free (tr_watchdir_t handle)
{
  if (handle == NULL)
    return;

  tr_watchdir_retries_destroy (&handle->active_retries);

  if (handle->backend != NULL)
    handle->backend->free_func (handle->backend);

  tr_free (handle->path);
  tr_free (handle);
}

const char *
tr_watchdir_get_path (tr_watchdir_t handle)
{
  assert (handle != NULL);
  return handle->path;
}

tr_watchdir_backend *
tr_watchdir_get_backend (tr_watchdir_t handle)
{
  assert (handle != NULL);
  return handle->backend;
}

struct event_base *
tr_watchdir_get_event_base (tr_watchdir_t handle)
{
  assert (handle != NULL);
  return handle->event_base;
}

/***
****
***/

void
tr_watchdir_process (tr_watchdir_t   handle,
                     const char    * name)
{
  const tr_watchdir_retry search_key = { .name = (char *) name };
  tr_watchdir_retry * existing_retry;

  assert (handle != NULL);

  if ((existing_retry = tr_watchdir_retries_find (&handle->active_retries, &search_key)) != NULL)
    {
      tr_watchdir_retry_restart (existing_retry);
      return;
    }

  if (tr_watchdir_process_impl (handle, name) == TR_WATCHDIR_RETRY)
    {
      tr_watchdir_retry * retry = tr_watchdir_retry_new (handle, name);
      tr_watchdir_retries_insert (&handle->active_retries, retry);
    }
}

void
tr_watchdir_scan (tr_watchdir_t   handle,
                  tr_ptrArray   * dir_entries)
{
  tr_sys_dir_t dir;
  const char * name;
  tr_ptrArray new_dir_entries = TR_PTR_ARRAY_INIT_STATIC;
  const PtrArrayCompareFunc name_compare_func = (PtrArrayCompareFunc) &strcmp;
  tr_error * error = NULL;

  if ((dir = tr_sys_dir_open (handle->path, &error)) == TR_BAD_SYS_DIR)
    {
      log_error ("Failed to open directory \"%s\" (%d): %s", handle->path,
                 error->code, error->message);
      tr_error_free (error);
      return;
    }

  while ((name = tr_sys_dir_read_name (dir, &error)) != NULL)
    {
      if (strcmp (name, ".") == 0 || strcmp (name, "..") == 0)
        continue;

      if (dir_entries != NULL)
        {
          tr_ptrArrayInsertSorted (&new_dir_entries, tr_strdup (name), name_compare_func);
          if (tr_ptrArrayFindSorted (dir_entries, name, name_compare_func) != NULL)
            continue;
        }

      tr_watchdir_process (handle, name);
    }

  if (error != NULL)
    {
      log_error ("Failed to read directory \"%s\" (%d): %s", handle->path,
                 error->code, error->message);
      tr_error_free (error);
    }

  tr_sys_dir_close (dir, NULL);

  if (dir_entries != NULL)
    {
      tr_ptrArrayDestruct (dir_entries, &tr_free);
      *dir_entries = new_dir_entries;
    }
}
