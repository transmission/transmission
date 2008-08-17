/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#if defined(WIN32)
#define TR_PATH_DELIMITER '\\'
#define TR_PATH_DELIMITER_STR "\\"
#else
#define TR_PATH_DELIMITER '/'
#define TR_PATH_DELIMITER_STR "/"
#endif

typedef struct tr_lock   tr_lock;
typedef struct tr_thread tr_thread;

struct tr_handle;

void tr_setConfigDir  ( struct tr_handle * handle,
                        const char       * configDir );

const char * tr_getResumeDir  ( const struct tr_handle * );

const char * tr_getTorrentDir ( const struct tr_handle * );

const char * tr_getClutchDir  ( const struct tr_handle * );


tr_thread*   tr_threadNew  ( void (*func)(void *), void * arg );
int          tr_amInThread ( const tr_thread * );

tr_lock *    tr_lockNew        ( void );
void         tr_lockFree       ( tr_lock * );
void         tr_lockLock       ( tr_lock * );
void         tr_lockUnlock     ( tr_lock * );
int          tr_lockHave       ( const tr_lock * );

tr_lockfile_state_t tr_lockfile       ( const char * filename );

#endif
