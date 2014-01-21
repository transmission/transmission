/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id: list.c 14224 2014-01-18 20:56:57Z jordan $
 */

#ifndef TR_LOG_H
#define TR_LOG_H 1

#include <stddef.h> /* size_t */
#include "utils.h" /* TR_GNUC_PRINTF, TR_GNUC_NONNULL */

#ifdef __cplusplus
extern "C" {
#endif

#define TR_LOG_MAX_QUEUE_LENGTH 10000

tr_log_level tr_logGetLevel (void);

static inline bool
tr_logLevelIsActive (tr_log_level level)
{
  return tr_logGetLevel () >= level;
}

void tr_logAddMessage (const char   * file,
                       int            line,
                       tr_log_level   level,
                       const char   * torrent,
                       const char   * fmt, ...) TR_GNUC_PRINTF (5, 6);

#define tr_logAddNamedError(n, ...) \
  do\
    { \
      if (tr_logLevelIsActive (TR_LOG_ERROR)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_ERROR, n, __VA_ARGS__); \
    } \
  while (0)

#define tr_logAddNamedInfo(n, ...) \
  do \
    { \
      if (tr_logLevelIsActive (TR_LOG_INFO)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_INFO, n, __VA_ARGS__); \
    } \
  while (0)

#define tr_logAddNamedDbg(n, ...) \
  do \
    { \
      if (tr_logLevelIsActive (TR_LOG_DEBUG)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_DEBUG, n, __VA_ARGS__); \
    } \
  while (0)

#define tr_logAddTorErr(tor, ...) \
  do \
    { \
      if (tr_logLevelIsActive (TR_LOG_ERROR)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_ERROR, tr_torrentName (tor), __VA_ARGS__); \
    } \
  while (0)

#define tr_logAddTorInfo(tor, ...) \
  do \
    { \
      if (tr_logLevelIsActive (TR_LOG_INFO)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_INFO, tr_torrentName (tor), __VA_ARGS__); \
    } \
  while (0)

#define tr_logAddTorDbg(tor, ...) \
  do \
    { \
      if (tr_logLevelIsActive (TR_LOG_DEBUG)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_DEBUG, tr_torrentName (tor), __VA_ARGS__); \
    } \
  while (0)

#define tr_logAddError(...) \
  do \
    { \
      if (tr_logLevelIsActive (TR_LOG_ERROR)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_ERROR, NULL, __VA_ARGS__); \
    } \
  while (0)

#define tr_logAddInfo(...) \
  do \
    { \
      if (tr_logLevelIsActive (TR_LOG_INFO)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_INFO, NULL, __VA_ARGS__); \
    } \
  while (0)

#define tr_logAddDebug(...) \
  do \
    { \
      if (tr_logLevelIsActive (TR_LOG_DEBUG)) \
        tr_logAddMessage (__FILE__, __LINE__, TR_LOG_DEBUG, NULL, __VA_ARGS__); \
    } \
  while (0)



void* tr_logGetFile (void);

/** @brief return true if deep logging has been enabled by the user; false otherwise */
bool tr_logGetDeepEnabled (void);

void tr_logAddDeep (const char * file,
                    int          line,
                    const char * name,
                    const char * fmt,
                    ...) TR_GNUC_PRINTF (4, 5) TR_GNUC_NONNULL (1,4);

/** @brief set the buffer with the current time formatted for deep logging. */
char* tr_logGetTimeStr (char * buf, int buflen) TR_GNUC_NONNULL (1);

#ifdef __cplusplus
}
#endif

/** @} */

#endif
