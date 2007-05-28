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
#define TR_PLATFORM_H 1

#ifdef SYS_BEOS
  #include <kernel/OS.h>
  typedef thread_id tr_thread_id_t;
  typedef sem_id    tr_lock_t;
  typedef int       tr_cond_t;
#else
  #include <pthread.h>
  typedef pthread_t       tr_thread_id_t;
  typedef pthread_mutex_t tr_lock_t;
  typedef pthread_cond_t  tr_cond_t;
#endif
typedef struct tr_thread_s
{
    void          (* func ) ( void * );
    void           * arg;
    char           * name;
    tr_thread_id_t thread;;
}
tr_thread_t;

char * tr_getCacheDirectory();
char * tr_getTorrentsDirectory();

/**
 * When instantiating a thread with a deferred call to tr_threadCreate(),
 * initializing it to THREAD_EMPTY makes calls tr_threadJoin() safe.
 */ 
const tr_thread_t THREAD_EMPTY;

void tr_threadCreate ( tr_thread_t *, void (*func)(void *),
                       void * arg, char * name );
void tr_threadJoin   ( tr_thread_t * );
void tr_lockInit     ( tr_lock_t * );
void tr_lockClose    ( tr_lock_t * );

static inline void tr_lockLock( tr_lock_t * l )
{
#ifdef SYS_BEOS
    acquire_sem( *l );
#else
    pthread_mutex_lock( l );
#endif
}

static inline void tr_lockUnlock( tr_lock_t * l )
{
#ifdef SYS_BEOS
    release_sem( *l );
#else
    pthread_mutex_unlock( l );
#endif
}

void tr_condInit( tr_cond_t * );
void tr_condWait( tr_cond_t *, tr_lock_t * );
void tr_condSignal( tr_cond_t * );
void tr_condClose( tr_cond_t * );

int
tr_getDefaultRoute( struct in_addr * addr );

#endif
