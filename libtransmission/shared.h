/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#ifndef SHARED_H
#define SHARED_H 1

#include "transmission.h"

typedef struct tr_shared tr_shared;

/***********************************************************************
 * tr_sharedInit, tr_sharedClose
 ***********************************************************************
 * Starts / stops a thread to handle running things that are shared
 * among the torrents: NAT-PMP/UPnP, incoming connections, peer choking
 **********************************************************************/
tr_shared * tr_sharedInit           ( tr_handle * );
void        tr_sharedClose          ( tr_shared * );

/***********************************************************************
 * tr_sharedLock, tr_sharedUnlock
 ***********************************************************************
 * Gets / releases exclusive access to ressources used by the shared
 * thread
 **********************************************************************/
void          tr_sharedLock           ( tr_shared * );
void          tr_sharedUnlock         ( tr_shared * );

/***********************************************************************
 * tr_sharedSetPort
 ***********************************************************************
 * Changes the port for incoming connections.  tr_sharedGetPublicPort
 * should be called with the shared lock held.
 **********************************************************************/
void         tr_sharedSetPort         ( tr_shared *, int port );
int          tr_sharedGetPublicPort   ( tr_shared * s );

/***********************************************************************
 * tr_sharedTraversalEnable, tr_sharedTraversalStatus
 ***********************************************************************
 * Enables/disables and retrieves the status of NAT traversal.  Should
 * be called with the shared lock held.
 **********************************************************************/
void         tr_sharedTraversalEnable ( tr_shared *, int enable );
int          tr_sharedTraversalStatus ( tr_shared * );


#endif

