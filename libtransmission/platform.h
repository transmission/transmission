/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005 Transmission authors and contributors
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
#ifndef TR_PLATFORM_H
#define TR_PLATFORM_H

typedef struct tr_lock_s   tr_lock_t;
typedef struct tr_cond_s   tr_cond_t;
typedef struct tr_thread_s tr_thread_t;

const char * tr_getHomeDirectory( void );
const char * tr_getCacheDirectory( void );
const char * tr_getTorrentsDirectory( void );

tr_thread_t*  tr_threadNew  ( void (*func)(void *), void * arg, const char * name );
void          tr_threadJoin ( tr_thread_t * );
int           tr_amInThread ( const tr_thread_t * );

tr_lock_t * tr_lockNew        ( void );
void        tr_lockFree       ( tr_lock_t * );
int         tr_lockTryLock    ( tr_lock_t * );
void        tr_lockLock       ( tr_lock_t * );
void        tr_lockUnlock     ( tr_lock_t * );

tr_cond_t * tr_condNew       ( void );
void        tr_condFree      ( tr_cond_t * );
void        tr_condSignal    ( tr_cond_t * );
void        tr_condBroadcast ( tr_cond_t * );
void        tr_condWait      ( tr_cond_t *, tr_lock_t * );

/***
**** RW lock:
**** The lock can be had by one writer or any number of readers.
***/

typedef struct tr_rwlock_s tr_rwlock_t;

tr_rwlock_t*  tr_rwNew           ( void );
void          tr_rwFree          ( tr_rwlock_t * );
void          tr_rwReaderLock    ( tr_rwlock_t * );
int           tr_rwReaderTrylock ( tr_rwlock_t * );
void          tr_rwReaderUnlock  ( tr_rwlock_t * );
void          tr_rwWriterLock    ( tr_rwlock_t * );
int           tr_rwWriterTrylock ( tr_rwlock_t * );
void          tr_rwWriterUnlock  ( tr_rwlock_t * );


struct in_addr; /* forward declaration to calm gcc down */
int
tr_getDefaultRoute( struct in_addr * addr );

#endif
