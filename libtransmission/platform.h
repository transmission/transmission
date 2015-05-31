/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_PLATFORM_H
#define TR_PLATFORM_H

#define TR_PATH_DELIMITER '/'
#define TR_PATH_DELIMITER_STR "/"

/**
 * @addtogroup tr_session Session
 * @{
 */

/**
 * @brief invoked by tr_sessionInit () to set up the locations of the resume, torrent, and clutch directories.
 * @see tr_getResumeDir ()
 * @see tr_getTorrentDir ()
 * @see tr_getWebClientDir ()
 */
void tr_setConfigDir (tr_session * session, const char * configDir);

/** @brief return the directory where .resume files are stored */
const char * tr_getResumeDir (const tr_session *);

/** @brief return the directory where .torrent files are stored */
const char * tr_getTorrentDir (const tr_session *);

/** @brief return the directory where the Web Client's web ui files are kept */
const char * tr_getWebClientDir (const tr_session *);

/** @} */


/**
 * @addtogroup utils Utilities
 * @{
 */

typedef struct tr_thread tr_thread;

/** @brief Instantiate a new process thread */
tr_thread* tr_threadNew (void (*func)(void *), void * arg);

/** @brief Return nonzero if this function is being called from `thread'
    @param thread the thread being tested */
bool tr_amInThread (const tr_thread *);

/***
****
***/

typedef struct tr_lock tr_lock;

/** @brief Create a new thread mutex object */
tr_lock * tr_lockNew (void);

/** @brief Destroy a thread mutex object */
void tr_lockFree (tr_lock *);

/** @brief Attempt to lock a thread mutex object */
void tr_lockLock (tr_lock *);

/** @brief Unlock a thread mutex object */
void tr_lockUnlock (tr_lock *);

/** @brief return nonzero if the specified lock is locked */
bool tr_lockHave (const tr_lock *);

/* @} */

#endif
