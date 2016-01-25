/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef TR_WATCHDIR_H
#define TR_WATCHDIR_H

#ifdef __cplusplus
extern "C" {
#endif

struct event_base;

typedef struct tr_watchdir * tr_watchdir_t;

typedef enum
{
  TR_WATCHDIR_ACCEPT,
  TR_WATCHDIR_IGNORE,
  TR_WATCHDIR_RETRY
}
tr_watchdir_status;

typedef tr_watchdir_status (* tr_watchdir_cb) (tr_watchdir_t   handle,
                                               const char    * name,
                                               void          * user_data);

/* ... */

tr_watchdir_t   tr_watchdir_new      (const char        * path,
                                      tr_watchdir_cb      callback,
                                      void              * callback_user_data,
                                      struct event_base * event_base,
                                      bool                force_generic);

void            tr_watchdir_free     (tr_watchdir_t       handle);

const char    * tr_watchdir_get_path (tr_watchdir_t       handle);

#ifdef __cplusplus
}
#endif

#endif /* TR_WATCHDIR_H */
