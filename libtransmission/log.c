/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id: utils.c 13863 2013-01-24 23:59:52Z jordan $
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* getenv() */

#include <event2/buffer.h>

#include "transmission.h"
#include "log.h"
#include "platform.h" /* tr_lock */
#include "utils.h"

tr_log_level __tr_message_level  = TR_LOG_ERROR;

static bool           myQueueEnabled = false;
static tr_log_message *  myQueue = NULL;
static tr_log_message ** myQueueTail = &myQueue;
static int            myQueueLength = 0;

#ifndef WIN32
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

void*
tr_logGetFile (void)
{
  static bool initialized = false;
  static FILE * file = NULL;

  if (!initialized)
    {
      int fd = 0;
      const char * str = getenv ("TR_DEBUG_FD");

      if (str && *str)
        fd = atoi (str);

      switch (fd)
        {
          case 1:
            file = stdout;
            break;

          case 2:
            file = stderr;
            break;

          default:
            file = NULL;
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
      free (list->message);
      free (list->name);
      free (list);
      list = next;
    }
}

/**
***
**/

char*
tr_logGetTimeStr (char * buf, int buflen)
{
  char tmp[64];
  struct tm now_tm;
  struct timeval tv;
  time_t seconds;
  int milliseconds;

  tr_gettimeofday (&tv);

  seconds = tv.tv_sec;
  tr_localtime_r (&seconds, &now_tm);
  strftime (tmp, sizeof (tmp), "%Y-%m-%d %H:%M:%S.%%03d %Z", &now_tm); 
  milliseconds = tv.tv_usec / 1000;
  tr_snprintf (buf, buflen, tmp, milliseconds);

  return buf;
}

bool
tr_logGetDeepEnabled (void)
{
  static int8_t deepLoggingIsActive = -1;

  if (deepLoggingIsActive < 0)
    deepLoggingIsActive = IsDebuggerPresent () || (tr_logGetFile ()!=NULL);

  return deepLoggingIsActive != 0;
}

void
tr_logAddDeep (const char  * file,
               int           line,
               const char  * name,
               const char  * fmt,
               ...)
{
  FILE * fp = tr_logGetFile ();
  if (fp || IsDebuggerPresent ())
    {
      va_list args;
      char timestr[64];
      char * message;
      struct evbuffer * buf = evbuffer_new ();
      char * base = tr_basename (file);

      evbuffer_add_printf (buf, "[%s] ",
                           tr_logGetTimeStr (timestr, sizeof (timestr)));
      if (name)
        evbuffer_add_printf (buf, "%s ", name);
      va_start (args, fmt);
      evbuffer_add_vprintf (buf, fmt, args);
      va_end (args);
      evbuffer_add_printf (buf, " (%s:%d)\n", base, line);
      /* FIXME (libevent2) ifdef this out for nonwindows platforms */
      message = evbuffer_free_to_str (buf);
      OutputDebugStringA (message);
      if (fp)
        fputs (message, fp);

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
  va_list ap;
  tr_lockLock (getMessageLock ());

  /* build the text message */
  *buf = '\0';
  va_start (ap, fmt);
  evutil_vsnprintf (buf, sizeof (buf), fmt, ap);
  va_end (ap);

  OutputDebugStringA (buf);

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
          FILE * fp;
          char timestr[64];

          fp = tr_logGetFile ();
          if (fp == NULL)
            fp = stderr;

          tr_logGetTimeStr (timestr, sizeof (timestr));

          if (name)
            fprintf (fp, "[%s] %s: %s\n", timestr, name, buf);
          else
            fprintf (fp, "[%s] %s\n", timestr, buf);
          fflush (fp);
        }
    }

  tr_lockUnlock (getMessageLock ());
  errno = err;
}
