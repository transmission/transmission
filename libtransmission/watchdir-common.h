/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef TR_WATCHDIR_COMMON_H
#define TR_WATCHDIR_COMMON_H

#ifndef __LIBTRANSMISSION_WATCHDIR_MODULE__
 #error only the libtransmission watchdir module should #include this header.
#endif

struct tr_ptrArray;

typedef struct tr_watchdir_backend
{
  void (* free_func) (struct tr_watchdir_backend *);
}
tr_watchdir_backend;

#define BACKEND_DOWNCAST(b) ((tr_watchdir_backend *) (b))

/* ... */

tr_watchdir_backend * tr_watchdir_get_backend    (tr_watchdir_t        handle);

struct event_base   * tr_watchdir_get_event_base (tr_watchdir_t        handle);

/* ... */

void                  tr_watchdir_process        (tr_watchdir_t        handle,
                                                  const char         * name);

void                  tr_watchdir_scan           (tr_watchdir_t        handle,
                                                  struct tr_ptrArray * dir_entries);

/* ... */

tr_watchdir_backend * tr_watchdir_generic_new    (tr_watchdir_t        handle);

#ifdef WITH_INOTIFY
tr_watchdir_backend * tr_watchdir_inotify_new    (tr_watchdir_t        handle);
#endif
#ifdef WITH_KQUEUE
tr_watchdir_backend * tr_watchdir_kqueue_new     (tr_watchdir_t        handle);
#endif
#ifdef _WIN32
tr_watchdir_backend * tr_watchdir_win32_new      (tr_watchdir_t        handle);
#endif

#endif /* TR_WATCHDIR_COMMON_H */
