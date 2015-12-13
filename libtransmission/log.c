/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include <event2/buffer.h>

#include "transmission.h"
#include "file.h"
#include "log.h"
#include "platform.h" /* tr_lock */
#include "utils.h"

tr_log_level __tr_message_level  = TR_LOG_ERROR;

static bool           myQueueEnabled = false;
static tr_log_message *  myQueue = NULL;
static tr_log_message ** myQueueTail = &myQueue;
static int            myQueueLength = 0;

#ifndef _WIN32
  /* make null versions of these win32 functions */
  static inline int IsDebuggerPresent (void) { return false; }
  static inline void OutputDebugStringA (const void * unused UNUSED) { }
#endif

/***
****
***/

tr_log_level
tr_logGetLevel (void)
{
  return __tr_message_level;
}

/***
****
***/

static tr_lock*
getMessageLock (void)
{
  static tr_lock * l = NULL;

  if (!l)
    l = tr_lockNew ();

  return l;
}

tr_sys_file_t
tr_logGetFile (void)
{
  static bool initialized = false;
  static tr_sys_file_t file = TR_BAD_SYS_FILE;

  if (!initialized)
    {
      const int fd = tr_env_get_int ("TR_DEBUG_FD", 0);

      switch (fd)
        {
          case 1:
            file = tr_sys_file_get_std (TR_STD_SYS_FILE_OUT, NULL);
            break;

          case 2:
            file = tr_sys_file_get_std (TR_STD_SYS_FILE_ERR, NULL);
            break;
        }

      initialized = true;
    }

  return file;
}

void
tr_logSetLevel (tr_log_level level)
{
    __tr_message_level = level;
}

void
tr_logSetQueueEnabled (bool isEnabled)
{
  assert (tr_isBool (isEnabled));

  myQueueEnabled = isEnabled;
}

bool
tr_logGetQueueEnabled (void)
{
  return myQueueEnabled;
}

tr_log_message *
tr_logGetQueue (void)
{
  tr_log_message * ret;
  tr_lockLock (getMessageLock ());

  ret = myQueue;
  myQueue = NULL;
  myQueueTail = &myQueue;
  myQueueLength = 0;

  tr_lockUnlock (getMessageLock ());
  return ret;
}

void
tr_logFreeQueue (tr_log_message * list)
{
  tr_log_message * next;

  while (NULL != list)
    {
      next = list->next;
      tr_free (list->message);
      tr_free (list->name);
      tr_free (list);
      list = next;
    }
}

/**
***
**/

char*
tr_logGetTimeStr (char * buf, size_t buflen)
{
  char tmp[64];
  struct tm now_tm;
  struct timeval tv;
  time_t seconds;
  int milliseconds;

  tr_gettimeofday (&tv);

  seconds = tv.tv_sec;
  tr_localtime_r (&seconds, &now_tm);
  strftime (tmp, sizeof (tmp), "%Y-%m-%d %H:%M:%S.%%03d", &now_tm);
  milliseconds = tv.tv_usec / 1000;
  tr_snprintf (buf, buflen, tmp, milliseconds);

  return buf;
}

bool
tr_logGetDeepEnabled (void)
{
  static int8_t deepLoggingIsActive = -1;

  if (deepLoggingIsActive < 0)
    deepLoggingIsActive = IsDebuggerPresent () || (tr_logGetFile () != TR_BAD_SYS_FILE);

  return deepLoggingIsActive != 0;
}

void
tr_logAddDeep (const char  * file,
               int           line,
               const char  * name,
               const char  * fmt,
               ...)
{
  const tr_sys_file_t fp = tr_logGetFile ();
  if (fp != TR_BAD_SYS_FILE || IsDebuggerPresent ())
    {
      va_list args;
      char timestr[64];
      char * message;
      size_t message_len;
      struct evbuffer * buf = evbuffer_new ();
      char * base = tr_sys_path_basename (file, NULL);

      evbuffer_add_printf (buf, "[%s] ",
                           tr_logGetTimeStr (timestr, sizeof (timestr)));
      if (name)
        evbuffer_add_printf (buf, "%s ", name);
      va_start (args, fmt);
      evbuffer_add_vprintf (buf, fmt, args);
      va_end (args);
      evbuffer_add_printf (buf, " (%s:%d)" TR_NATIVE_EOL_STR, base, line);
      /* FIXME (libevent2) ifdef this out for nonwindows platforms */
      message = evbuffer_free_to_str (buf, &message_len);
      OutputDebugStringA (message);
      if (fp != TR_BAD_SYS_FILE)
        tr_sys_file_write (fp, message, message_len, NULL, NULL);

      tr_free (message);
      tr_free (base);
    }
}

/***
****
***/

void
tr_logAddMessage (const char * file,
                  int line,
                  tr_log_level level,
                  const char * name,
                  const char * fmt,
                  ...)
{
  const int err = errno; /* message logging shouldn't affect errno */
  char buf[1024];
  int buf_len;
  va_list ap;
  tr_lockLock (getMessageLock ());

  /* build the text message */
  *buf = '\0';
  va_start (ap, fmt);
  buf_len = evutil_vsnprintf (buf, sizeof (buf), fmt, ap);
  va_end (ap);

  if (buf_len < 0)
    return;

#ifdef _WIN32
  if ((size_t) buf_len < sizeof (buf) - 3)
    {
      buf[buf_len + 0] = '\r';
      buf[buf_len + 1] = '\n';
      buf[buf_len + 2] = '\0';
      OutputDebugStringA (buf);
      buf[buf_len + 0] = '\0';
    }
  else
    {
      OutputDebugStringA (buf);
    }
#endif

  if (*buf)
    {
      if (tr_logGetQueueEnabled ())
        {
          tr_log_message * newmsg;
          newmsg = tr_new0 (tr_log_message, 1);
          newmsg->level = level;
          newmsg->when = tr_time ();
          newmsg->message = tr_strdup (buf);
          newmsg->file = file;
          newmsg->line = line;
          newmsg->name = tr_strdup (name);

          *myQueueTail = newmsg;
          myQueueTail = &newmsg->next;
          ++myQueueLength;

          if (myQueueLength > TR_LOG_MAX_QUEUE_LENGTH)
            {
              tr_log_message * old = myQueue;
              myQueue = old->next;
              old->next = NULL;
              tr_logFreeQueue (old);
              --myQueueLength;
              assert (myQueueLength == TR_LOG_MAX_QUEUE_LENGTH);
            }
        }
      else
        {
          tr_sys_file_t fp;
          char timestr[64];

          fp = tr_logGetFile ();
          if (fp == TR_BAD_SYS_FILE)
            fp = tr_sys_file_get_std (TR_STD_SYS_FILE_ERR, NULL);

          tr_logGetTimeStr (timestr, sizeof (timestr));

          if (name)
            tr_sys_file_write_fmt (fp, "[%s] %s: %s" TR_NATIVE_EOL_STR, NULL, timestr, name, buf);
          else
            tr_sys_file_write_fmt (fp, "[%s] %s" TR_NATIVE_EOL_STR, NULL, timestr, buf);
          tr_sys_file_flush (fp, NULL);
        }
    }

  tr_lockUnlock (getMessageLock ());
  errno = err;
}
