/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_EVENT_H
#define TR_EVENT_H

/**
**/

void   tr_eventInit (tr_session *);

void   tr_eventClose (tr_session *);

bool   tr_amInEventThread (const tr_session *);

void   tr_runInEventThread (tr_session *, void func (void*), void * user_data);

#endif
