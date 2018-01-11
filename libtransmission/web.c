/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* strlen (), strstr () */

#ifdef _WIN32
  #include <ws2tcpip.h>
#else
  #include <sys/select.h>
#endif

#include <curl/curl.h>

#include <event2/buffer.h>

#include "transmission.h"
#include "file.h"
#include "list.h"
#include "log.h"
#include "net.h" /* tr_address */
#include "torrent.h"
#include "platform.h" /* mutex */
#include "session.h"
#include "trevent.h" /* tr_runInEventThread () */
#include "utils.h"
#include "version.h" /* User-Agent */
#include "web.h"

#if LIBCURL_VERSION_NUM >= 0x070F06 /* CURLOPT_SOCKOPT* was added in 7.15.6 */
 #define USE_LIBCURL_SOCKOPT
#endif

enum
{
  THREADFUNC_MAX_SLEEP_MSEC = 200,
};

#if 0
#define dbgmsg(...) \
  do { \
    fprintf (stderr, __VA_ARGS__); \
    fprintf (stderr, "\n"); \
  } while (0)
#else
#define dbgmsg(...) \
  do { \
    if (tr_logGetDeepEnabled ()) \
      tr_logAddDeep (__FILE__, __LINE__, "web", __VA_ARGS__); \
  } while (0)
#endif

/***
****
***/

struct tr_web_task
{
  int torrentId;
  long code;
  long timeout_secs;
  bool did_connect;
  bool did_timeout;
  struct evbuffer * response;
  struct evbuffer * freebuf;
  char * url;
  char * range;
  char * cookies;
  tr_session * session;
  tr_web_done_func done_func;
  void * done_func_user_data;
  CURL * curl_easy;
  struct tr_web_task * next;
};

static void
task_free (struct tr_web_task * task)
{
  if (task->freebuf)
    evbuffer_free (task->freebuf);
  tr_free (task->cookies);
  tr_free (task->range);
  tr_free (task->url);
  tr_free (task);
}

/***
****
***/

static tr_list  * paused_easy_handles = NULL;

struct tr_web
{
  bool curl_verbose;
  bool curl_ssl_verify;
  char * curl_ca_bundle;
  int close_mode;
  struct tr_web_task * tasks;
  tr_lock * taskLock;
  char * cookie_filename;
};

/***
****
***/

static size_t
writeFunc (void * ptr, size_t size, size_t nmemb, void * vtask)
{
  const size_t byteCount = size * nmemb;
  struct tr_web_task * task = vtask;

  /* webseed downloads should be speed limited */
  if (task->torrentId != -1)
    {
      tr_torrent * tor = tr_torrentFindFromId (task->session, task->torrentId);

      if (tor && !tr_bandwidthClamp (&tor->bandwidth, TR_DOWN, nmemb))
        {
          tr_list_append (&paused_easy_handles, task->curl_easy);
          return CURL_WRITEFUNC_PAUSE;
        }
    }

  evbuffer_add (task->response, ptr, byteCount);
  dbgmsg ("wrote %zu bytes to task %p's buffer", byteCount, (void*)task);
  return byteCount;
}

#ifdef USE_LIBCURL_SOCKOPT
static int
sockoptfunction (void * vtask, curl_socket_t fd, curlsocktype purpose UNUSED)
{
  struct tr_web_task * task = vtask;
  const bool isScrape = strstr (task->url, "scrape") != NULL;
  const bool isAnnounce = strstr (task->url, "announce") != NULL;

  /* announce and scrape requests have tiny payloads. */
  if (isScrape || isAnnounce)
    {
      const int sndbuf = isScrape ? 4096 : 1024;
      const int rcvbuf = isScrape ? 4096 : 3072;
      setsockopt (fd, SOL_SOCKET, SO_SNDBUF, (const void *) &sndbuf, sizeof (sndbuf));
      setsockopt (fd, SOL_SOCKET, SO_RCVBUF, (const void *) &rcvbuf, sizeof (rcvbuf));
    }

  /* return nonzero if this function encountered an error */
  return 0;
}
#endif

static long
getTimeoutFromURL (const struct tr_web_task * task)
{
  long timeout;
  const tr_session * session = task->session;

  if (!session || session->isClosed) timeout = 20L;
  else if (strstr (task->url, "scrape") != NULL) timeout = 30L;
  else if (strstr (task->url, "announce") != NULL) timeout = 90L;
  else timeout = 240L;

  return timeout;
}

static CURL *
createEasy (tr_session * s, struct tr_web * web, struct tr_web_task * task)
{
  bool is_default_value;
  const tr_address * addr;
  CURL * e = task->curl_easy = curl_easy_init ();

  task->timeout_secs = getTimeoutFromURL (task);

  curl_easy_setopt (e, CURLOPT_AUTOREFERER, 1L);
  curl_easy_setopt (e, CURLOPT_ENCODING, "gzip;q=1.0, deflate, identity");
  curl_easy_setopt (e, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt (e, CURLOPT_MAXREDIRS, -1L);
  curl_easy_setopt (e, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt (e, CURLOPT_PRIVATE, task);
#ifdef USE_LIBCURL_SOCKOPT
  curl_easy_setopt (e, CURLOPT_SOCKOPTFUNCTION, sockoptfunction);
  curl_easy_setopt (e, CURLOPT_SOCKOPTDATA, task);
#endif
  if (web->curl_ssl_verify)
    {
      curl_easy_setopt (e, CURLOPT_CAINFO, web->curl_ca_bundle);
    }
  else
    {
      curl_easy_setopt (e, CURLOPT_SSL_VERIFYHOST, 0L);
      curl_easy_setopt (e, CURLOPT_SSL_VERIFYPEER, 0L);
    }
  curl_easy_setopt (e, CURLOPT_TIMEOUT, task->timeout_secs);
  curl_easy_setopt (e, CURLOPT_URL, task->url);
  curl_easy_setopt (e, CURLOPT_USERAGENT, TR_NAME "/" SHORT_VERSION_STRING);
  curl_easy_setopt (e, CURLOPT_VERBOSE, (long)(web->curl_verbose?1:0));
  curl_easy_setopt (e, CURLOPT_WRITEDATA, task);
  curl_easy_setopt (e, CURLOPT_WRITEFUNCTION, writeFunc);

  if (((addr = tr_sessionGetPublicAddress (s, TR_AF_INET, &is_default_value))) && !is_default_value)
    curl_easy_setopt (e, CURLOPT_INTERFACE, tr_address_to_string (addr));
  else if (((addr = tr_sessionGetPublicAddress (s, TR_AF_INET6, &is_default_value))) && !is_default_value)
    curl_easy_setopt (e, CURLOPT_INTERFACE, tr_address_to_string (addr));

  if (task->cookies != NULL)
    curl_easy_setopt (e, CURLOPT_COOKIE, task->cookies);

  if (web->cookie_filename != NULL)
    curl_easy_setopt (e, CURLOPT_COOKIEFILE, web->cookie_filename);

  if (task->range != NULL)
    {
      curl_easy_setopt (e, CURLOPT_RANGE, task->range);
      /* don't bother asking the server to compress webseed fragments */
      curl_easy_setopt (e, CURLOPT_ENCODING, "identity");
    }

  return e;
}

/***
****
***/

static void
task_finish_func (void * vtask)
{
  struct tr_web_task * task = vtask;
  dbgmsg ("finished web task %p; got %ld", (void*)task, task->code);

  if (task->done_func != NULL)
    task->done_func (task->session,
                     task->did_connect,
                     task->did_timeout,
                     task->code,
                     evbuffer_pullup (task->response, -1),
                     evbuffer_get_length (task->response),
                     task->done_func_user_data);

  task_free (task);
}

/****
*****
****/

static void tr_webThreadFunc (void * vsession);

static struct tr_web_task *
tr_webRunImpl (tr_session         * session,
               int                  torrentId,
               const char         * url,
               const char         * range,
               const char         * cookies,
               tr_web_done_func     done_func,
               void               * done_func_user_data,
               struct evbuffer    * buffer)
{
  struct tr_web_task * task = NULL;

  if (!session->isClosing)
    {
      if (session->web == NULL)
        {
          tr_threadNew (tr_webThreadFunc, session);

          while (session->web == NULL)
            tr_wait_msec (20);
        }

      task = tr_new0 (struct tr_web_task, 1);
      task->session = session;
      task->torrentId = torrentId;
      task->url = tr_strdup (url);
      task->range = tr_strdup (range);
      task->cookies = tr_strdup (cookies);
      task->done_func = done_func;
      task->done_func_user_data = done_func_user_data;
      task->response = buffer ? buffer : evbuffer_new ();
      task->freebuf = buffer ? NULL : task->response;

      tr_lockLock (session->web->taskLock);
      task->next = session->web->tasks;
      session->web->tasks = task;
      tr_lockUnlock (session->web->taskLock);
    }

  return task;
}

struct tr_web_task *
tr_webRunWithCookies (tr_session        * session,
                      const char        * url,
                      const char        * cookies,
                      tr_web_done_func    done_func,
                      void              * done_func_user_data)
{
  return tr_webRunImpl (session, -1, url,
                        NULL, cookies,
                        done_func, done_func_user_data,
                        NULL);
}

struct tr_web_task *
tr_webRun (tr_session         * session,
           const char         * url,
           tr_web_done_func     done_func,
           void               * done_func_user_data)
{
  return tr_webRunWithCookies (session, url, NULL,
                               done_func, done_func_user_data);
}


struct tr_web_task *
tr_webRunWebseed (tr_torrent        * tor,
                  const char        * url,
                  const char        * range,
                  tr_web_done_func    done_func,
                  void              * done_func_user_data,
                  struct evbuffer   * buffer)
{
  return tr_webRunImpl (tor->session, tr_torrentId (tor), url,
                        range, NULL,
                        done_func, done_func_user_data,
                        buffer);
}

/**
 * Portability wrapper for select ().
 *
 * http://msdn.microsoft.com/en-us/library/ms740141%28VS.85%29.aspx
 * On win32, any two of the parameters, readfds, writefds, or exceptfds,
 * can be given as null. At least one must be non-null, and any non-null
 * descriptor set must contain at least one handle to a socket.
 */
static void
tr_select (int nfds,
           fd_set * r_fd_set, fd_set * w_fd_set, fd_set * c_fd_set,
           struct timeval  * t)
{
#ifdef _WIN32
  (void) nfds;

  if (!r_fd_set->fd_count && !w_fd_set->fd_count && !c_fd_set->fd_count)
    {
      const long int msec = t->tv_sec*1000 + t->tv_usec/1000;
      tr_wait_msec (msec);
    }
  else if (select (0, r_fd_set->fd_count ? r_fd_set : NULL,
                      w_fd_set->fd_count ? w_fd_set : NULL,
                      c_fd_set->fd_count ? c_fd_set : NULL, t) < 0)
    {
      char errstr[512];
      const int e = EVUTIL_SOCKET_ERROR ();
      tr_net_strerror (errstr, sizeof (errstr), e);
      dbgmsg ("Error: select (%d) %s", e, errstr);
    }
#else
  select (nfds, r_fd_set, w_fd_set, c_fd_set, t);
#endif
}

static void
tr_webThreadFunc (void * vsession)
{
  char * str;
  CURLM * multi;
  struct tr_web * web;
  int taskCount = 0;
  struct tr_web_task * task;
  tr_session * session = vsession;

  /* try to enable ssl for https support; but if that fails,
   * try a plain vanilla init */
  if (curl_global_init (CURL_GLOBAL_SSL))
    curl_global_init (0);

  web = tr_new0 (struct tr_web, 1);
  web->close_mode = ~0;
  web->taskLock = tr_lockNew ();
  web->tasks = NULL;
  web->curl_verbose = tr_env_key_exists ("TR_CURL_VERBOSE");
  web->curl_ssl_verify = tr_env_key_exists ("TR_CURL_SSL_VERIFY");
  web->curl_ca_bundle = tr_env_get_string ("CURL_CA_BUNDLE", NULL);
  if (web->curl_ssl_verify)
    {
      tr_logAddNamedInfo ("web", "will verify tracker certs using envvar CURL_CA_BUNDLE: %s",
               web->curl_ca_bundle == NULL ? "none" : web->curl_ca_bundle);
      tr_logAddNamedInfo ("web", "NB: this only works if you built against libcurl with openssl or gnutls, NOT nss");
      tr_logAddNamedInfo ("web", "NB: invalid certs will show up as 'Could not connect to tracker' like many other errors");
    }

  str = tr_buildPath (session->configDir, "cookies.txt", NULL);
  if (tr_sys_path_exists (str, NULL))
    web->cookie_filename = tr_strdup (str);
  tr_free (str);

  multi = curl_multi_init ();
  session->web = web;

  for (;;)
    {
      long msec;
      int unused;
      CURLMsg * msg;
      CURLMcode mcode;

      if (web->close_mode == TR_WEB_CLOSE_NOW)
        break;
      if ((web->close_mode == TR_WEB_CLOSE_WHEN_IDLE) && (web->tasks == NULL))
        break;

      /* add tasks from the queue */
      tr_lockLock (web->taskLock);
      while (web->tasks != NULL)
        {
          /* pop the task */
          task = web->tasks;
          web->tasks = task->next;
          task->next = NULL;

          dbgmsg ("adding task to curl: [%s]", task->url);
          curl_multi_add_handle (multi, createEasy (session, web, task));
          /*fprintf (stderr, "adding a task.. taskCount is now %d\n", taskCount);*/
          ++taskCount;
        }
      tr_lockUnlock (web->taskLock);

      /* unpause any paused curl handles */
      if (paused_easy_handles != NULL)
        {
          CURL * handle;
          tr_list * tmp;

          /* swap paused_easy_handles to prevent oscillation
             between writeFunc this while loop */
          tmp = paused_easy_handles;
          paused_easy_handles = NULL;

          while ((handle = tr_list_pop_front (&tmp)))
            curl_easy_pause (handle, CURLPAUSE_CONT);
        }

      /* maybe wait a little while before calling curl_multi_perform () */
      msec = 0;
      curl_multi_timeout (multi, &msec);
      if (msec < 0)
        msec = THREADFUNC_MAX_SLEEP_MSEC;
      if (session->isClosed)
        msec = 100; /* on shutdown, call perform () more frequently */
      if (msec > 0)
        {
          int usec;
          int max_fd;
          struct timeval t;
          fd_set r_fd_set, w_fd_set, c_fd_set;

          max_fd = 0;
          FD_ZERO (&r_fd_set);
          FD_ZERO (&w_fd_set);
          FD_ZERO (&c_fd_set);
          curl_multi_fdset (multi, &r_fd_set, &w_fd_set, &c_fd_set, &max_fd);

          if (msec > THREADFUNC_MAX_SLEEP_MSEC)
            msec = THREADFUNC_MAX_SLEEP_MSEC;

          usec = msec * 1000;
          t.tv_sec =  usec / 1000000;
          t.tv_usec = usec % 1000000;
          tr_select (max_fd+1, &r_fd_set, &w_fd_set, &c_fd_set, &t);
        }

      /* call curl_multi_perform () */
      do
        mcode = curl_multi_perform (multi, &unused);
      while (mcode == CURLM_CALL_MULTI_PERFORM);

      /* pump completed tasks from the multi */
      while ((msg = curl_multi_info_read (multi, &unused)))
        {
          if ((msg->msg == CURLMSG_DONE) && (msg->easy_handle != NULL))
            {
              double total_time;
              struct tr_web_task * task;
              long req_bytes_sent;
              CURL * e = msg->easy_handle;
              curl_easy_getinfo (e, CURLINFO_PRIVATE, (void*)&task);
              assert (e == task->curl_easy);
              curl_easy_getinfo (e, CURLINFO_RESPONSE_CODE, &task->code);
              curl_easy_getinfo (e, CURLINFO_REQUEST_SIZE, &req_bytes_sent);
              curl_easy_getinfo (e, CURLINFO_TOTAL_TIME, &total_time);
              task->did_connect = task->code>0 || req_bytes_sent>0;
              task->did_timeout = !task->code && (total_time >= task->timeout_secs);
              curl_multi_remove_handle (multi, e);
              tr_list_remove_data (&paused_easy_handles, e);
              curl_easy_cleanup (e);
              tr_runInEventThread (task->session, task_finish_func, task);
              --taskCount;
            }
        }
    }

  /* Discard any remaining tasks.
   * This is rare, but can happen on shutdown with unresponsive trackers. */
  while (web->tasks != NULL)
    {
      task = web->tasks;
      web->tasks = task->next;
      dbgmsg ("Discarding task \"%s\"", task->url);
      task_free (task);
    }

  /* cleanup */
  tr_list_free (&paused_easy_handles, NULL);
  curl_multi_cleanup (multi);
  tr_lockFree (web->taskLock);
  tr_free (web->curl_ca_bundle);
  tr_free (web->cookie_filename);
  tr_free (web);
  session->web = NULL;
}


void
tr_webClose (tr_session * session, tr_web_close_mode close_mode)
{
  if (session->web != NULL)
    {
      session->web->close_mode = close_mode;

      if (close_mode == TR_WEB_CLOSE_NOW)
        while (session->web != NULL)
          tr_wait_msec (100);
    }
}

void
tr_webGetTaskInfo (struct tr_web_task * task, tr_web_task_info info, void * dst)
{
  curl_easy_getinfo (task->curl_easy, (CURLINFO) info, dst);
}

/*****
******
******
*****/

const char *
tr_webGetResponseStr (long code)
{
  switch (code)
    {
      case   0: return "No Response";
      case 101: return "Switching Protocols";
      case 200: return "OK";
      case 201: return "Created";
      case 202: return "Accepted";
      case 203: return "Non-Authoritative Information";
      case 204: return "No Content";
      case 205: return "Reset Content";
      case 206: return "Partial Content";
      case 300: return "Multiple Choices";
      case 301: return "Moved Permanently";
      case 302: return "Found";
      case 303: return "See Other";
      case 304: return "Not Modified";
      case 305: return "Use Proxy";
      case 306: return " (Unused)";
      case 307: return "Temporary Redirect";
      case 400: return "Bad Request";
      case 401: return "Unauthorized";
      case 402: return "Payment Required";
      case 403: return "Forbidden";
      case 404: return "Not Found";
      case 405: return "Method Not Allowed";
      case 406: return "Not Acceptable";
      case 407: return "Proxy Authentication Required";
      case 408: return "Request Timeout";
      case 409: return "Conflict";
      case 410: return "Gone";
      case 411: return "Length Required";
      case 412: return "Precondition Failed";
      case 413: return "Request Entity Too Large";
      case 414: return "Request-URI Too Long";
      case 415: return "Unsupported Media Type";
      case 416: return "Requested Range Not Satisfiable";
      case 417: return "Expectation Failed";
      case 421: return "Misdirected Request";
      case 500: return "Internal Server Error";
      case 501: return "Not Implemented";
      case 502: return "Bad Gateway";
      case 503: return "Service Unavailable";
      case 504: return "Gateway Timeout";
      case 505: return "HTTP Version Not Supported";
      default:  return "Unknown Error";
    }
}

void
tr_http_escape (struct evbuffer  * out,
                const char       * str,
                size_t             len,
                bool               escape_slashes)
{
  if (str == NULL)
    return;

  if (len == TR_BAD_SIZE)
    len = strlen (str);

  for (const char * end = str + len; str != end; ++str)
    {
      if ((*str == ',') || (*str == '-')
                        || (*str == '.')
                        || (('0' <= *str) && (*str <= '9'))
                        || (('A' <= *str) && (*str <= 'Z'))
                        || (('a' <= *str) && (*str <= 'z'))
                        || ((*str == '/') && (!escape_slashes)))
        evbuffer_add_printf (out, "%c", *str);
      else
        evbuffer_add_printf (out, "%%%02X", (unsigned)(*str&0xFF));
    }
}

char *
tr_http_unescape (const char * str, size_t len)
{
  char * tmp = curl_unescape (str, len);
  char * ret = tr_strdup (tmp);
  curl_free (tmp);
  return ret;
}

static bool
is_rfc2396_alnum (uint8_t ch)
{
  return ('0' <= ch && ch <= '9')
      || ('A' <= ch && ch <= 'Z')
      || ('a' <= ch && ch <= 'z')
      || ch == '.'
      || ch == '-'
      || ch == '_'
      || ch == '~';
}

void
tr_http_escape_sha1 (char * out, const uint8_t * sha1_digest)
{
  const uint8_t * in = sha1_digest;
  const uint8_t * end = in + SHA_DIGEST_LENGTH;

  while (in != end)
    if (is_rfc2396_alnum (*in))
      *out++ = (char) *in++;
    else
      out += tr_snprintf (out, 4, "%%%02x", (unsigned int)*in++);

  *out = '\0';
}
