/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef DTR_WATCH_H
#define DTR_WATCH_H

typedef struct dtr_watchdir dtr_watchdir;

typedef void (dtr_watchdir_callback)(tr_session * session, const char * dir, const char * file);

dtr_watchdir* dtr_watchdir_new (tr_session * session, const char * dir, dtr_watchdir_callback cb);

void dtr_watchdir_update (dtr_watchdir * w);

void dtr_watchdir_free (dtr_watchdir * w);

#endif
