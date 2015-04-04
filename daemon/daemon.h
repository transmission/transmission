/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef DTR_DAEMON_H
#define DTR_DAEMON_H

struct tr_error;

typedef struct dtr_callbacks
{
  int  (*on_start)       (void * arg, bool foreground);
  void (*on_stop)        (void * arg);
  void (*on_reconfigure) (void * arg);
}
dtr_callbacks;

bool dtr_daemon (const dtr_callbacks  * cb,
                 void                 * cb_arg,
                 bool                   foreground,
                 int                  * exit_code,
                 struct tr_error     ** error);

#endif /* DTR_DAEMON_H */
