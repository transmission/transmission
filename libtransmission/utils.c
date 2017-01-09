/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifdef HAVE_MEMMEM
 #define _GNU_SOURCE /* glibc's string.h needs this to pick up memmem */
#endif

#if defined (XCODE_BUILD)
 #define HAVE_GETPAGESIZE
 #define HAVE_VALLOC
#endif

#include <assert.h>
#include <ctype.h> /* isdigit (), tolower () */
#include <errno.h>
#include <float.h> /* DBL_EPSILON */
#include <locale.h> /* localeconv () */
#include <math.h> /* pow (), fabs (), floor () */
#include <stdio.h>
#include <stdlib.h> /* getenv () */
#include <string.h> /* strerror (), memset (), memmem () */
#include <time.h> /* nanosleep () */

#ifdef _WIN32
 #include <ws2tcpip.h> /* WSAStartup () */
 #include <windows.h> /* Sleep (), GetSystemTimeAsFileTime (), GetEnvironmentVariable () */
 #include <shellapi.h> /* CommandLineToArgv () */
#else
 #include <sys/time.h>
 #include <unistd.h> /* getpagesize () */
#endif

#ifdef HAVE_ICONV
 #include <iconv.h>
#endif

#include <event2/buffer.h>
#include <event2/event.h>

#include "transmission.h"
#include "error.h"
#include "error-types.h"
#include "file.h"
#include "ConvertUTF.h"
#include "list.h"
#include "log.h"
#include "net.h"
#include "utils.h"
#include "platform.h" /* tr_lockLock () */
#include "platform-quota.h" /* tr_device_info_create(), tr_device_info_get_free_space(), tr_device_info_free() */
#include "variant.h"
#include "version.h"


time_t __tr_current_time   = 0;

/***
****
***/

struct tm *
tr_localtime_r (const time_t *_clock, struct tm *_result)
{
#ifdef HAVE_LOCALTIME_R
  return localtime_r (_clock, _result);
#else
  struct tm *p = localtime (_clock);
  if (p)
    * (_result) = *p;
  return p;
#endif
}

int
tr_gettimeofday (struct timeval * tv)
{
#ifdef _WIN32
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000ULL

  FILETIME ft;
  uint64_t tmp = 0;

  if (tv == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  GetSystemTimeAsFileTime(&ft);
  tmp |= ft.dwHighDateTime;
  tmp <<= 32;
  tmp |= ft.dwLowDateTime;
  tmp /= 10; /* to microseconds */
  tmp -= DELTA_EPOCH_IN_MICROSECS;

  tv->tv_sec = tmp / 1000000UL;
  tv->tv_usec = tmp % 1000000UL;

  return 0;

#undef DELTA_EPOCH_IN_MICROSECS
#else

  return gettimeofday (tv, NULL);

#endif
}

/***
****
***/

void*
tr_malloc (size_t size)
{
  return size ? malloc (size) : NULL;
}

void*
tr_malloc0 (size_t size)
{
  return size ? calloc (1, size) : NULL;
}

void *
tr_realloc (void * p, size_t size)
{
  void * result = size != 0 ? realloc (p, size) : NULL;
  if (result == NULL)
    tr_free (p);
  return result;
}

void
tr_free (void * p)
{
  if (p != NULL)
    free (p);
}

void*
tr_memdup (const void * src, size_t byteCount)
{
  return memcpy (tr_malloc (byteCount), src, byteCount);
}

/***
****
***/

const char*
tr_strip_positional_args (const char* str)
{
  char * out;
  static size_t bufsize = 0;
  static char * buf = NULL;
  const char * in = str;
  const size_t  len = str ? strlen (str) : 0;

  if (!buf || (bufsize < len))
    {
      bufsize = len * 2 + 1;
      buf = tr_renew (char, buf, bufsize);
    }

  for (out = buf; str && *str; ++str)
    {
      *out++ = *str;

      if ((*str == '%') && isdigit (str[1]))
        {
          const char * tmp = str + 1;
          while (isdigit (*tmp))
            ++tmp;
          if (*tmp == '$')
            str = tmp[1]=='\'' ? tmp+1 : tmp;
        }

      if ((*str == '%') && (str[1] == '\''))
        str = str + 1;

    }

  *out = '\0';
  return !in || strcmp (buf, in) ? buf : in;
}

/**
***
**/

void
tr_timerAdd (struct event * timer, int seconds, int microseconds)
{
  struct timeval tv;
  tv.tv_sec = seconds;
  tv.tv_usec = microseconds;

  assert (tv.tv_sec >= 0);
  assert (tv.tv_usec >= 0);
  assert (tv.tv_usec < 1000000);

  evtimer_add (timer, &tv);
}

void
tr_timerAddMsec (struct event * timer, int msec)
{
  const int seconds =  msec / 1000;
  const int usec = (msec%1000) * 1000;
  tr_timerAdd (timer, seconds, usec);
}

/**
***
**/

uint8_t *
tr_loadFile (const char  * path,
             size_t      * size,
             tr_error   ** error)
{
  uint8_t * buf;
  tr_sys_path_info info;
  tr_sys_file_t fd;
  tr_error * my_error = NULL;
  const char * const err_fmt = _("Couldn't read \"%1$s\": %2$s");

  /* try to stat the file */
  if (!tr_sys_path_get_info (path, 0, &info, &my_error))
    {
      tr_logAddDebug (err_fmt, path, my_error->message);
      tr_error_propagate (error, &my_error);
      return NULL;
    }

  if (info.type != TR_SYS_PATH_IS_FILE)
    {
      tr_logAddError (err_fmt, path, _("Not a regular file"));
      tr_error_set_literal (error, TR_ERROR_EISDIR, _("Not a regular file"));
      return NULL;
    }

  /* file size should be able to fit into size_t */
  if (sizeof(info.size) > sizeof(*size))
    assert (info.size <= SIZE_MAX);

  /* Load the torrent file into our buffer */
  fd = tr_sys_file_open (path, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &my_error);
  if (fd == TR_BAD_SYS_FILE)
    {
      tr_logAddError (err_fmt, path, my_error->message);
      tr_error_propagate (error, &my_error);
      return NULL;
    }

  buf = tr_malloc (info.size + 1);

  if (!tr_sys_file_read (fd, buf, info.size, NULL, &my_error))
    {
      tr_logAddError (err_fmt, path, my_error->message);
      tr_sys_file_close (fd, NULL);
      free (buf);
      tr_error_propagate (error, &my_error);
      return NULL;
    }

  tr_sys_file_close (fd, NULL);
  buf[info.size] = '\0';
  *size = info.size;
  return buf;
}

char*
tr_buildPath (const char *first_element, ...)
{
  const char * element;
  char * buf;
  char * pch;
  va_list vl;
  size_t bufLen = 0;

  /* pass 1: allocate enough space for the string */
  va_start (vl, first_element);
  element = first_element;
  while (element)
    {
      bufLen += strlen (element) + 1;
      element = va_arg (vl, const char*);
    }
  pch = buf = tr_new (char, bufLen);
  va_end (vl);
  if (buf == NULL)
    return NULL;

  /* pass 2: build the string piece by piece */
  va_start (vl, first_element);
  element = first_element;
  while (element)
    {
      const size_t elementLen = strlen (element);
      memcpy (pch, element, elementLen);
      pch += elementLen;
      *pch++ = TR_PATH_DELIMITER;
      element = va_arg (vl, const char*);
    }
  va_end (vl);

  /* terminate the string. if nonempty, eat the unwanted trailing slash */
  if (pch != buf)
    --pch;
  *pch++ = '\0';

  /* sanity checks & return */
  assert (pch - buf == (ptrdiff_t)bufLen);
  return buf;
}

int64_t
tr_getDirFreeSpace (const char * dir)
{
  int64_t free_space;

  if (!dir || !*dir)
    {
      errno = EINVAL;
      free_space = -1;
    }
  else
    {
      struct tr_device_info * info;
      info = tr_device_info_create (dir);
      free_space = tr_device_info_get_free_space (info);
      tr_device_info_free (info);
    }

  return free_space;
}

/****
*****
****/

char*
evbuffer_free_to_str (struct evbuffer * buf,
                      size_t          * result_len)
{
  const size_t n = evbuffer_get_length (buf);
  char * ret = tr_new (char, n + 1);
  evbuffer_copyout (buf, ret, n);
  evbuffer_free (buf);
  ret[n] = '\0';
  if (result_len != NULL)
    *result_len = n;
  return ret;
}

char*
tr_strdup (const void * in)
{
  return tr_strndup (in, in != NULL ? strlen (in) : 0);
}

char*
tr_strndup (const void * in, size_t len)
{
  char * out = NULL;

  if (len == TR_BAD_SIZE)
    {
      out = tr_strdup (in);
    }
  else if (in)
    {
      out = tr_malloc (len + 1);

      if (out != NULL)
        {
          memcpy (out, in, len);
          out[len] = '\0';
        }
    }

  return out;
}

const char*
tr_memmem (const char * haystack, size_t haystacklen,
           const char * needle, size_t needlelen)
{
#ifdef HAVE_MEMMEM
  return memmem (haystack, haystacklen, needle, needlelen);
#else
  size_t i;
  if (!needlelen)
    return haystack;
  if (needlelen > haystacklen || !haystack || !needle)
    return NULL;
  for (i=0; i<=haystacklen-needlelen; ++i)
    if (!memcmp (haystack+i, needle, needlelen))
      return haystack+i;
  return NULL;
#endif
}

char*
tr_strdup_printf (const char * fmt, ...)
{
  va_list ap;
  char * ret;

  va_start (ap, fmt);
  ret = tr_strdup_vprintf (fmt, ap);
  va_end (ap);

  return ret;
}

char *
tr_strdup_vprintf (const char * fmt,
                   va_list      args)
{
  struct evbuffer * buf = evbuffer_new ();
  evbuffer_add_vprintf (buf, fmt, args);
  return evbuffer_free_to_str (buf, NULL);
}

const char*
tr_strerror (int i)
{
  const char * ret = strerror (i);

  if (ret == NULL)
    ret = "Unknown Error";

  return ret;
}

int
tr_strcmp0 (const char * str1, const char * str2)
{
  if (str1 && str2) return strcmp (str1, str2);
  if (str1) return 1;
  if (str2) return -1;
  return 0;
}

/****
*****
****/

/* https://bugs.launchpad.net/percona-patches/+bug/526863/+attachment/1160199/+files/solaris_10_fix.patch */
char*
tr_strsep (char ** str, const char * delims)
{
#ifdef HAVE_STRSEP
  return strsep (str, delims);
#else
  char *token;

  if (*str == NULL) /* no more tokens */
    return NULL;

  token = *str;
  while (**str != '\0')
    {
      if (strchr (delims, **str) != NULL)
        {
          **str = '\0';
          (*str)++;
            return token;
        }
      (*str)++;
    }

  /* there is not another token */
  *str = NULL;

  return token;
#endif
}

char*
tr_strstrip (char * str)
{
  if (str != NULL)
    {
      size_t pos;
      size_t len = strlen (str);

      while (len && isspace (str[len - 1]))
        --len;

      for (pos = 0; pos < len && isspace (str[pos]);)
        ++pos;

      len -= pos;
      memmove (str, str + pos, len);
      str[len] = '\0';
    }

  return str;
}

bool
tr_str_has_suffix (const char *str, const char *suffix)
{
  size_t str_len;
  size_t suffix_len;

  if (!str)
    return false;
  if (!suffix)
    return true;

  str_len = strlen (str);
  suffix_len = strlen (suffix);
  if (str_len < suffix_len)
    return false;

  return !evutil_ascii_strncasecmp (str + str_len - suffix_len, suffix, suffix_len);
}

/****
*****
****/

uint64_t
tr_time_msec (void)
{
  struct timeval tv;

  tr_gettimeofday (&tv);
  return (uint64_t) tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

void
tr_wait_msec (long int msec)
{
#ifdef _WIN32
  Sleep ((DWORD)msec);
#else
  struct timespec ts;
  ts.tv_sec = msec / 1000;
  ts.tv_nsec = (msec % 1000) * 1000000;
  nanosleep (&ts, NULL);
#endif
}

/***
****
***/

int
tr_snprintf (char * buf, size_t buflen, const char * fmt, ...)
{
  int len;
  va_list args;

  va_start (args, fmt);
  len = evutil_vsnprintf (buf, buflen, fmt, args);
  va_end (args);
  return len;
}

/*
 * Copy src to string dst of size siz. At most siz-1 characters
 * will be copied. Always NUL terminates (unless siz == 0).
 * Returns strlen (src); if retval >= siz, truncation occurred.
 */
size_t
tr_strlcpy (char * dst, const void * src, size_t siz)
{
#ifdef HAVE_STRLCPY
  return strlcpy (dst, src, siz);
#else
  char *      d = dst;
  const char *s = src;
  size_t      n = siz;

  assert (s);
  assert (d);

  /* Copy as many bytes as will fit */
  if (n != 0)
    {
      while (--n != 0)
        {
          if ((*d++ = *s++) == '\0')
            break;
        }
    }

  /* Not enough room in dst, add NUL and traverse rest of src */
  if (n == 0)
    {
      if (siz != 0)
        *d = '\0'; /* NUL-terminate dst */
      while (*s++)
        ;
    }

  return s - (const char*)src - 1;  /* count does not include NUL */
#endif
}

/***
****
***/

double
tr_getRatio (uint64_t numerator, uint64_t denominator)
{
  double ratio;

  if (denominator > 0)
    ratio = numerator / (double)denominator;
  else if (numerator > 0)
    ratio = TR_RATIO_INF;
  else
    ratio = TR_RATIO_NA;

  return ratio;
}

void
tr_binary_to_hex (const void * input,
                  char       * output,
                  size_t       byte_length)
{
  static const char hex[] = "0123456789abcdef";
  const uint8_t * input_octets = input;

  /* go from back to front to allow for in-place conversion */
  input_octets += byte_length;
  output += byte_length * 2;

  *output = '\0';

  while (byte_length-- > 0)
    {
      const unsigned int val = *(--input_octets);
      *(--output) = hex[val & 0xf];
      *(--output) = hex[val >> 4];
    }
}

void
tr_hex_to_binary (const char * input,
                  void       * output,
                  size_t       byte_length)
{
  static const char hex[] = "0123456789abcdef";
  uint8_t * output_octets = output;
  size_t i;

  for (i = 0; i < byte_length; ++i)
    {
      const int hi = strchr (hex, tolower (*input++)) - hex;
      const int lo = strchr (hex, tolower (*input++)) - hex;
      *output_octets++ = (uint8_t) ((hi << 4) | lo);
    }
}

/***
****
***/

static bool
isValidURLChars (const char * url, size_t url_len)
{
  static const char rfc2396_valid_chars[] =
    "abcdefghijklmnopqrstuvwxyz" /* lowalpha */
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ" /* upalpha */
    "0123456789"                 /* digit */
    "-_.!~*'()"                  /* mark */
    ";/?:@&=+$,"                 /* reserved */
    "<>#%<\""                    /* delims */
    "{}|\\^[]`";                 /* unwise */

  if (url == NULL)
    return false;

  for (const char * c = url, * end = url + url_len; c < end && *c != '\0'; ++c)
    if (memchr (rfc2396_valid_chars, *c, sizeof (rfc2396_valid_chars) - 1) == NULL)
      return false;

  return true;
}

bool
tr_urlIsValidTracker (const char * url)
{
  if (url == NULL)
    return false;

  const size_t url_len = strlen (url);

  return isValidURLChars (url, url_len)
      && tr_urlParse (url, url_len, NULL, NULL, NULL, NULL)
      && (memcmp (url, "http://", 7) == 0 || memcmp (url, "https://", 8) == 0 ||
          memcmp (url, "udp://", 6) == 0);
}

bool
tr_urlIsValid (const char * url,
               size_t       url_len)
{
  if (url == NULL)
    return false;

  if (url_len == TR_BAD_SIZE)
    url_len = strlen (url);

  return isValidURLChars (url, url_len)
      && tr_urlParse (url, url_len, NULL, NULL, NULL, NULL)
      && (memcmp (url, "http://", 7) == 0 || memcmp (url, "https://", 8) == 0 ||
          memcmp (url, "ftp://", 6) == 0 || memcmp (url, "sftp://", 7) == 0);
}

bool
tr_addressIsIP (const char * str)
{
  tr_address tmp;
  return tr_address_from_string (&tmp, str);
}

static int
parse_port (const char * port,
            size_t       port_len)
{
  char * tmp = tr_strndup (port, port_len);
  char * end;

  long port_num = strtol (tmp, &end, 10);

  if (*end != '\0' || port_num <= 0 || port_num >= 65536)
    port_num = -1;

  tr_free (tmp);

  return (int) port_num;
}

static int
get_port_for_scheme (const char * scheme,
                     size_t       scheme_len)
{
  struct known_scheme
    {
      const char * name;
      int          port;
    };

  static const struct known_scheme known_schemes[] =
    {
      { "udp",    80 },
      { "ftp",    21 },
      { "sftp",   22 },
      { "http",   80 },
      { "https", 443 },
      { NULL,      0 }
    };

  for (const struct known_scheme * s = known_schemes; s->name != NULL; ++s)
    {
      if (scheme_len == strlen (s->name) && memcmp (scheme, s->name, scheme_len) == 0)
        return s->port;
    }

  return -1;
}

bool
tr_urlParse (const char  * url,
             size_t        url_len,
             char       ** setme_scheme,
             char       ** setme_host,
             int         * setme_port,
             char       ** setme_path)
{
  if (url_len == TR_BAD_SIZE)
    url_len = strlen (url);

  const char * scheme = url;
  const char * scheme_end = tr_memmem (scheme, url_len, "://", 3);
  if (scheme_end == NULL)
    return false;

  const size_t scheme_len = scheme_end - scheme;
  if (scheme_len == 0)
    return false;

  url += scheme_len + 3;
  url_len -= scheme_len + 3;

  const char * authority = url;
  const char * authority_end = memchr (authority, '/', url_len);
  if (authority_end == NULL)
    authority_end = authority + url_len;

  const size_t authority_len = authority_end - authority;
  if (authority_len == 0)
    return false;

  url += authority_len;
  url_len -= authority_len;

  const char * host_end = memchr (authority, ':', authority_len);

  const size_t host_len = host_end != NULL ? (size_t) (host_end - authority) : authority_len;
  if (host_len == 0)
    return false;

  const size_t port_len = host_end != NULL ? authority_end - host_end - 1 : 0;

  if (setme_scheme != NULL)
    *setme_scheme = tr_strndup (scheme, scheme_len);

  if (setme_host != NULL)
    *setme_host = tr_strndup (authority, host_len);

  if (setme_port != NULL)
    *setme_port = port_len > 0 ? parse_port (host_end + 1, port_len)
                               : get_port_for_scheme (scheme, scheme_len);

  if (setme_path != NULL)
    {
      if (url[0] == '\0')
        *setme_path = tr_strdup ("/");
      else
        *setme_path = tr_strndup (url, url_len);
    }

  return true;
}

/***
****
***/

void
tr_removeElementFromArray (void         * array,
                           unsigned int   index_to_remove,
                           size_t         sizeof_element,
                           size_t         nmemb)
{
  char * a = array;

  memmove (a + sizeof_element * index_to_remove,
           a + sizeof_element * (index_to_remove  + 1),
           sizeof_element * (--nmemb - index_to_remove));
}

int
tr_lowerBound (const void * key,
               const void * base,
               size_t       nmemb,
               size_t       size,
               int     (* compar)(const void* key, const void* arrayMember),
               bool       * exact_match)
{
  size_t first = 0;
  const char * cbase = base;
  bool exact = false;

  while (nmemb != 0)
    {
      const size_t half = nmemb / 2;
      const size_t middle = first + half;
      const int c = compar (key, cbase + size*middle);

      if (c <= 0)
        {
          if (c == 0)
            exact = true;
          nmemb = half;
        }
      else
        {
          first = middle + 1;
          nmemb = nmemb - half - 1;
        }
    }

  *exact_match = exact;
  return first;
}

/***
****
****
***/

/* Byte-wise swap two items of size SIZE.
   From glibc, written by Douglas C. Schmidt, LGPL 2.1 or higher */
#define SWAP(a, b, size) \
  do { \
    register size_t __size = (size); \
    register char *__a = (a), *__b = (b); \
    if (__a != __b) do { \
      char __tmp = *__a; \
      *__a++ = *__b; \
      *__b++ = __tmp; \
    } while (--__size > 0); \
  } while (0)


static size_t
quickfindPartition (char * base, size_t left, size_t right, size_t size,
                    int (*compar)(const void *, const void *), size_t pivotIndex)
{
  size_t i;
  size_t storeIndex;

  /* move pivot to the end */
  SWAP (base+(size*pivotIndex), base+(size*right), size);

  storeIndex = left;
  for (i=left; i<=right-1; ++i)
    {
      if (compar (base+(size*i), base+(size*right)) <= 0)
        {
          SWAP (base+(size*storeIndex), base+(size*i), size);
          ++storeIndex;
        }
    }

  /* move pivot to its final place */
  SWAP (base+(size*right), base+(size*storeIndex), size);

  /* sanity check the partition */
#ifndef NDEBUG
  assert (storeIndex >= left);
  assert (storeIndex <= right);

  for (i=left; i<storeIndex; ++i)
    assert (compar (base+(size*i), base+(size*storeIndex)) <= 0);
  for (i=storeIndex+1; i<=right; ++i)
    assert (compar (base+(size*i), base+(size*storeIndex)) >= 0);
#endif

  return storeIndex;
}

static void
quickfindFirstK (char * base, size_t left, size_t right, size_t size,
                 int (*compar)(const void *, const void *), size_t k)
{
  if (right > left)
    {
      const size_t pivotIndex = left + (right-left)/2u;

      const size_t pivotNewIndex = quickfindPartition (base, left, right, size, compar, pivotIndex);

      if (pivotNewIndex > left + k) /* new condition */
        quickfindFirstK (base, left, pivotNewIndex-1, size, compar, k);
      else if (pivotNewIndex < left + k)
        quickfindFirstK (base, pivotNewIndex+1, right, size, compar, k+left-pivotNewIndex-1);
    }
}

#ifndef NDEBUG
static void
checkBestScoresComeFirst (char * base, size_t nmemb, size_t size,
                          int (*compar)(const void *, const void *), size_t k)
{
  size_t i;
  size_t worstFirstPos = 0;

  for (i=1; i<k; ++i)
    if (compar (base+(size*worstFirstPos), base+(size*i)) < 0)
      worstFirstPos = i;

  for (i=0; i<k; ++i)
    assert (compar (base+(size*i), base+(size*worstFirstPos)) <= 0);
  for (i=k; i<nmemb; ++i)
    assert (compar (base+(size*i), base+(size*worstFirstPos)) >= 0);
}
#endif

void
tr_quickfindFirstK (void * base, size_t nmemb, size_t size,
                    int (*compar)(const void *, const void *), size_t k)
{
  if (k < nmemb)
    {
      quickfindFirstK (base, 0, nmemb-1, size, compar, k);

#ifndef NDEBUG
      checkBestScoresComeFirst (base, nmemb, size, compar, k);
#endif
    }
}

/***
****
***/

static char*
strip_non_utf8 (const char * in, size_t inlen)
{
  const char * end;
  struct evbuffer * buf = evbuffer_new ();

  while (!tr_utf8_validate (in, inlen, &end))
    {
      const int good_len = end - in;

      evbuffer_add (buf, in, good_len);
      inlen -= (good_len + 1);
      in += (good_len + 1);
      evbuffer_add (buf, "?", 1);
    }

  evbuffer_add (buf, in, inlen);
  return evbuffer_free_to_str (buf, NULL);
}

static char*
to_utf8 (const char * in, size_t inlen)
{
  char * ret = NULL;

#ifdef HAVE_ICONV
  int i;
  const char * encodings[] = { "CURRENT", "ISO-8859-15" };
  const int encoding_count = sizeof (encodings) / sizeof (encodings[1]);
  const size_t buflen = inlen*4 + 10;
  char * out = tr_new (char, buflen);

  for (i=0; !ret && i<encoding_count; ++i)
    {
#ifdef ICONV_SECOND_ARGUMENT_IS_CONST
      const char * inbuf = in;
#else
      char * inbuf = (char*) in;
#endif
      char * outbuf = out;
      size_t inbytesleft = inlen;
      size_t outbytesleft = buflen;
      const char * test_encoding = encodings[i];

      iconv_t cd = iconv_open ("UTF-8", test_encoding);
      if (cd != (iconv_t)-1)
        {
          if (iconv (cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft) != (size_t)-1)
            ret = tr_strndup (out, buflen-outbytesleft);
          iconv_close (cd);
        }
    }

  tr_free (out);
#endif

  if (ret == NULL)
    ret = strip_non_utf8 (in, inlen);

  return ret;
}

char*
tr_utf8clean (const char * str, size_t max_len)
{
  char * ret;
  const char * end;

  if (max_len == TR_BAD_SIZE)
    max_len = strlen (str);

  if (tr_utf8_validate (str, max_len, &end))
    ret = tr_strndup (str, max_len);
  else
    ret = to_utf8 (str, max_len);

  assert (tr_utf8_validate (ret, TR_BAD_SIZE, NULL));
  return ret;
}

#ifdef _WIN32

char *
tr_win32_native_to_utf8 (const wchar_t * text,
                         int             text_size)
{
  char * ret = NULL;
  int size;

  size = WideCharToMultiByte (CP_UTF8, 0, text, text_size, NULL, 0, NULL, NULL);
  if (size == 0)
    goto fail;

  ret = tr_new (char, size + 1);
  size = WideCharToMultiByte (CP_UTF8, 0, text, text_size, ret, size, NULL, NULL);
  if (size == 0)
    goto fail;

  ret[size] = '\0';

  return ret;

fail:
  tr_free (ret);

  return NULL;
}

wchar_t *
tr_win32_utf8_to_native (const char * text,
                         int          text_size)
{
  return tr_win32_utf8_to_native_ex (text, text_size, 0, 0, NULL);
}

wchar_t *
tr_win32_utf8_to_native_ex (const char * text,
                            int          text_size,
                            int          extra_chars_before,
                            int          extra_chars_after,
                            int        * real_result_size)
{
  wchar_t * ret = NULL;
  int size;

  if (text_size == -1)
    text_size = strlen (text);

  size = MultiByteToWideChar (CP_UTF8, 0, text, text_size, NULL, 0);
  if (size == 0)
    goto fail;

  ret = tr_new (wchar_t, size + extra_chars_before + extra_chars_after + 1);
  size = MultiByteToWideChar (CP_UTF8, 0, text, text_size, ret + extra_chars_before, size);
  if (size == 0)
    goto fail;

  ret[size + extra_chars_before + extra_chars_after] = L'\0';

  if (real_result_size != NULL)
    *real_result_size = size;

  return ret;

fail:
  tr_free (ret);

  return NULL;
}

char *
tr_win32_format_message (uint32_t code)
{
  wchar_t * wide_text = NULL;
  DWORD wide_size;
  char * text = NULL;
  size_t text_size;

  wide_size = FormatMessageW (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                              FORMAT_MESSAGE_FROM_SYSTEM |
                              FORMAT_MESSAGE_IGNORE_INSERTS,
                              NULL, code, 0, (LPWSTR)&wide_text, 0, NULL);
  if (wide_size == 0)
    return tr_strdup_printf ("Unknown error (0x%08x)", code);

  if (wide_size != 0 && wide_text != NULL)
    text = tr_win32_native_to_utf8 (wide_text, wide_size);

  LocalFree (wide_text);

  if (text != NULL)
    {
      /* Most (all?) messages contain "\r\n" in the end, chop it */
      text_size = strlen (text);
      while (text_size > 0 && isspace ((uint8_t) text[text_size - 1]))
        text[--text_size] = '\0';
    }

  return text;
}

void
tr_win32_make_args_utf8 (int    * argc,
                         char *** argv)
{
  int my_argc, i;
  char ** my_argv;
  wchar_t ** my_wide_argv;

  my_wide_argv = CommandLineToArgvW (GetCommandLineW (), &my_argc);
  if (my_wide_argv == NULL)
    return;

  assert (*argc == my_argc);

  my_argv = tr_new (char *, my_argc + 1);

  for (i = 0; i < my_argc; ++i)
    {
      my_argv[i] = tr_win32_native_to_utf8 (my_wide_argv[i], -1);
      if (my_argv[i] == NULL)
        break;
    }

  if (i < my_argc)
    {
      int j;

      for (j = 0; j < i; ++j)
        {
          tr_free (my_argv[j]);
        }

      tr_free (my_argv);
    }
  else
    {
      my_argv[my_argc] = NULL;

      *argc = my_argc;
      *argv = my_argv;

      /* TODO: Add atexit handler to cleanup? */
    }

  LocalFree (my_wide_argv);
}

int
tr_main_win32 (int     argc,
               char ** argv,
               int   (*real_main) (int, char **))
{
  tr_win32_make_args_utf8 (&argc, &argv);
  SetConsoleCP (CP_UTF8);
  SetConsoleOutputCP (CP_UTF8);
  return real_main (argc, argv);
}

#endif

/***
****
***/

struct number_range
{
  int low;
  int high;
};

/**
 * This should be a single number (ex. "6") or a range (ex. "6-9").
 * Anything else is an error and will return failure.
 */
static bool
parseNumberSection (const char * str, size_t len, struct number_range * setme)
{
  long a, b;
  bool success;
  char * end;
  const int error = errno;
  char * tmp = tr_strndup (str, len);

  errno = 0;
  a = b = strtol (tmp, &end, 10);
  if (errno || (end == tmp))
    {
      success = false;
    }
  else if (*end != '-')
    {
      success = true;
    }
  else
    {
      const char * pch = end + 1;
      b = strtol (pch, &end, 10);
      if (errno || (pch == end))
        success = false;
      else if (*end) /* trailing data */
        success = false;
      else
        success = true;
    }

  tr_free (tmp);

  setme->low = MIN (a, b);
  setme->high = MAX (a, b);

  errno = error;
  return success;
}

int
compareInt (const void * va, const void * vb)
{
  const int a = * (const int *)va;
  const int b = * (const int *)vb;
  return a - b;
}

/**
 * Given a string like "1-4" or "1-4,6,9,14-51", this allocates and returns an
 * array of setmeCount ints of all the values in the array.
 * For example, "5-8" will return [ 5, 6, 7, 8 ] and setmeCount will be 4.
 * It's the caller's responsibility to call tr_free () on the returned array.
 * If a fragment of the string can't be parsed, NULL is returned.
 */
int*
tr_parseNumberRange (const char * str_in, size_t len, int * setmeCount)
{
  int n = 0;
  int * uniq = NULL;
  char * str = tr_strndup (str_in, len);
  const char * walk;
  tr_list * ranges = NULL;
  bool success = true;

  walk = str;
  while (walk && *walk && success)
    {
      struct number_range range;
      const char * pch = strchr (walk, ',');
      if (pch)
        {
          success = parseNumberSection (walk, (size_t) (pch - walk), &range);
          walk = pch + 1;
        }
      else
        {
          success = parseNumberSection (walk, strlen (walk), &range);
          walk += strlen (walk);
        }
      if (success)
        tr_list_append (&ranges, tr_memdup (&range, sizeof (struct number_range)));
    }

  if (!success)
    {
      *setmeCount = 0;
      uniq = NULL;
    }
  else
    {
      int i;
      int n2;
      tr_list * l;
      int * sorted = NULL;

      /* build a sorted number array */
      n = n2 = 0;
      for (l=ranges; l!=NULL; l=l->next)
        {
          const struct number_range * r = l->data;
          n += r->high + 1 - r->low;
        }
      sorted = tr_new (int, n);
      if (sorted == NULL)
        {
          n = 0;
          uniq = NULL;
        }
      else
        {
          for (l=ranges; l!=NULL; l=l->next)
            {
              int i;
              const struct number_range * r = l->data;
              for (i=r->low; i<=r->high; ++i)
                sorted[n2++] = i;
            }
          qsort (sorted, n, sizeof (int), compareInt);
          assert (n == n2);

          /* remove duplicates */
          uniq = tr_new (int, n);
          if (uniq == NULL)
            {
              n = 0;
            }
          else
            {
              for (i=n=0; i<n2; ++i)
                if (!n || uniq[n-1] != sorted[i])
                  uniq[n++] = sorted[i];
            }

          tr_free (sorted);
        }
    }

  /* cleanup */
  tr_list_free (&ranges, tr_free);
  tr_free (str);

  /* return the result */
  *setmeCount = n;
  return uniq;
}

/***
****
***/

double
tr_truncd (double x, int precision)
{
  char * pt;
  char buf[128];
  const int max_precision = (int) log10 (1.0 / DBL_EPSILON) - 1;
  tr_snprintf (buf, sizeof (buf), "%.*f", max_precision, x);
  if ((pt = strstr (buf, localeconv ()->decimal_point)))
    pt[precision ? precision+1 : 0] = '\0';
  return atof (buf);
}

/* return a truncated double as a string */
static char*
tr_strtruncd (char * buf, double x, int precision, size_t buflen)
{
  tr_snprintf (buf, buflen, "%.*f", precision, tr_truncd (x, precision));
  return buf;
}

char*
tr_strpercent (char * buf, double x, size_t buflen)
{
  if (x < 100.0)
    tr_strtruncd (buf, x, 1, buflen);
  else
    tr_strtruncd (buf, x, 0, buflen);

  return buf;
}

char*
tr_strratio (char * buf, size_t buflen, double ratio, const char * infinity)
{
  if ((int)ratio == TR_RATIO_NA)
    tr_strlcpy (buf, _("None"), buflen);
  else if ((int)ratio == TR_RATIO_INF)
    tr_strlcpy (buf, infinity, buflen);
  else
    tr_strpercent (buf, ratio, buflen);

  return buf;
}

/***
****
***/

bool
tr_moveFile (const char * oldpath, const char * newpath, tr_error ** error)
{
  tr_sys_file_t in;
  tr_sys_file_t out;
  char * buf = NULL;
  tr_sys_path_info info;
  uint64_t bytesLeft;
  const size_t buflen = 1024 * 128; /* 128 KiB buffer */

  /* make sure the old file exists */
  if (!tr_sys_path_get_info (oldpath, 0, &info, error))
    {
      tr_error_prefix (error, "Unable to get information on old file: ");
      return false;
    }
  if (info.type != TR_SYS_PATH_IS_FILE)
    {
      tr_error_set_literal (error, TR_ERROR_EINVAL, "Old path does not point to a file.");
      return false;
    }

  /* make sure the target directory exists */
  {
    char * newdir = tr_sys_path_dirname (newpath, NULL);
    const bool i = tr_sys_dir_create (newdir, TR_SYS_DIR_CREATE_PARENTS, 0777, error);
    tr_free (newdir);
    if (!i)
      {
        tr_error_prefix (error, "Unable to create directory for new file: ");
        return false;
      }
  }

  /* they might be on the same filesystem... */
  if (tr_sys_path_rename (oldpath, newpath, NULL))
    return true;

  /* copy the file */
  in = tr_sys_file_open (oldpath, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, error);
  if (in == TR_BAD_SYS_FILE)
    {
      tr_error_prefix (error, "Unable to open old file: ");
      return false;
    }

  out = tr_sys_file_open (newpath, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0666, error);
  if (out == TR_BAD_SYS_FILE)
    {
      tr_error_prefix (error, "Unable to open new file: ");
      tr_sys_file_close (in, NULL);
      return false;
    }

  buf = tr_valloc (buflen);
  bytesLeft = info.size;
  while (bytesLeft > 0)
    {
      const uint64_t bytesThisPass = MIN (bytesLeft, buflen);
      uint64_t numRead, bytesWritten;
      if (!tr_sys_file_read (in, buf, bytesThisPass, &numRead, error))
        break;
      if (!tr_sys_file_write (out, buf, numRead, &bytesWritten, error))
        break;
      assert (numRead == bytesWritten);
      assert (bytesWritten <= bytesLeft);
      bytesLeft -= bytesWritten;
    }

  /* cleanup */
  tr_free (buf);
  tr_sys_file_close (out, NULL);
  tr_sys_file_close (in, NULL);

  if (bytesLeft != 0)
    {
      tr_error_prefix (error, "Unable to read/write: ");
      return false;
    }

  {
    tr_error * my_error = NULL;
    if (!tr_sys_path_remove (oldpath, &my_error))
      {
        tr_logAddError ("Unable to remove file at old path: %s", my_error->message);
        tr_error_free (my_error);
      }
  }

  return true;
}

/***
****
***/

void*
tr_valloc (size_t bufLen)
{
  size_t allocLen;
  void * buf = NULL;
  static size_t pageSize = 0;

  if (!pageSize)
    {
#if defined (HAVE_GETPAGESIZE) && !defined (_WIN32)
      pageSize = (size_t) getpagesize ();
#else /* guess */
      pageSize = 4096;
#endif
    }

  allocLen = pageSize;
  while (allocLen < bufLen)
    allocLen += pageSize;

#ifdef HAVE_POSIX_MEMALIGN
  if (!buf)
    if (posix_memalign (&buf, pageSize, allocLen))
      buf = NULL; /* just retry with valloc/malloc */
#endif
#ifdef HAVE_VALLOC
  if (!buf)
    buf = valloc (allocLen);
#endif
  if (!buf)
    buf = tr_malloc (allocLen);

  return buf;
}

/***
****
***/

uint64_t
tr_htonll (uint64_t x)
{
#ifdef HAVE_HTONLL
  return htonll (x);
#else
  /* fallback code by bdonlan at
   * http://stackoverflow.com/questions/809902/64-bit-ntohl-in-c/875505#875505 */
  union { uint32_t lx[2]; uint64_t llx; } u;
  u.lx[0] = htonl (x >> 32);
  u.lx[1] = htonl (x & 0xFFFFFFFFULL);
  return u.llx;
#endif
}

uint64_t
tr_ntohll (uint64_t x)
{
#ifdef HAVE_NTOHLL
  return ntohll (x);
#else
  /* fallback code by bdonlan at
   * http://stackoverflow.com/questions/809902/64-bit-ntohl-in-c/875505#875505 */
  union { uint32_t lx[2]; uint64_t llx; } u;
  u.llx = x;
  return ((uint64_t)ntohl (u.lx[0]) << 32) | (uint64_t)ntohl (u.lx[1]);
#endif
}

/***
****
****
****
***/

struct formatter_unit
{
  char * name;
  int64_t value;
};

struct formatter_units
{
  struct formatter_unit units[4];
};

enum { TR_FMT_KB, TR_FMT_MB, TR_FMT_GB, TR_FMT_TB };

static void
formatter_init (struct formatter_units * units,
                unsigned int kilo,
                const char * kb, const char * mb,
                const char * gb, const char * tb)
{
  uint64_t value;

  value = kilo;
  units->units[TR_FMT_KB].name = tr_strdup (kb);
  units->units[TR_FMT_KB].value = value;

  value *= kilo;
  units->units[TR_FMT_MB].name = tr_strdup (mb);
  units->units[TR_FMT_MB].value = value;

  value *= kilo;
  units->units[TR_FMT_GB].name = tr_strdup (gb);
  units->units[TR_FMT_GB].value = value;

  value *= kilo;
  units->units[TR_FMT_TB].name = tr_strdup (tb);
  units->units[TR_FMT_TB].value = value;
}

static char*
formatter_get_size_str (const struct formatter_units * u,
                        char * buf, int64_t bytes, size_t buflen)
{
  int precision;
  double value;
  const char * units;
  const struct formatter_unit * unit;

       if (bytes < u->units[1].value) unit = &u->units[0];
  else if (bytes < u->units[2].value) unit = &u->units[1];
  else if (bytes < u->units[3].value) unit = &u->units[2];
  else                                unit = &u->units[3];

  value = (double)bytes / unit->value;
  units = unit->name;

  if (unit->value == 1)
    precision = 0;
  else if (value < 100)
    precision = 2;
  else
    precision = 1;

  tr_snprintf (buf, buflen, "%.*f %s", precision, value, units);
  return buf;
}

static struct formatter_units size_units;

void
tr_formatter_size_init (unsigned int kilo,
                        const char * kb, const char * mb,
                        const char * gb, const char * tb)
{
  formatter_init (&size_units, kilo, kb, mb, gb, tb);
}

char*
tr_formatter_size_B (char * buf, int64_t bytes, size_t buflen)
{
  return formatter_get_size_str (&size_units, buf, bytes, buflen);
}

static struct formatter_units speed_units;

unsigned int tr_speed_K = 0u;

void
tr_formatter_speed_init (unsigned int kilo,
                         const char * kb, const char * mb,
                         const char * gb, const char * tb)
{
  tr_speed_K = kilo;
  formatter_init (&speed_units, kilo, kb, mb, gb, tb);
}

char*
tr_formatter_speed_KBps (char * buf, double KBps, size_t buflen)
{
  const double K = speed_units.units[TR_FMT_KB].value;
  double speed = KBps;

  if (speed <= 999.95) /* 0.0 KB to 999.9 KB */
    {
      tr_snprintf (buf, buflen, "%d %s", (int)speed, speed_units.units[TR_FMT_KB].name);
    }
  else
    {
      speed /= K;

      if (speed <= 99.995) /* 0.98 MB to 99.99 MB */
        tr_snprintf (buf, buflen, "%.2f %s", speed, speed_units.units[TR_FMT_MB].name);
      else if (speed <= 999.95) /* 100.0 MB to 999.9 MB */
        tr_snprintf (buf, buflen, "%.1f %s", speed, speed_units.units[TR_FMT_MB].name);
      else
        tr_snprintf (buf, buflen, "%.1f %s", speed/K, speed_units.units[TR_FMT_GB].name);
    }

  return buf;
}

static struct formatter_units mem_units;

unsigned int tr_mem_K = 0u;

void
tr_formatter_mem_init (unsigned int kilo,
                       const char * kb, const char * mb,
                       const char * gb, const char * tb)
{
  tr_mem_K = kilo;
  formatter_init (&mem_units, kilo, kb, mb, gb, tb);
}

char*
tr_formatter_mem_B (char * buf, int64_t bytes_per_second, size_t buflen)
{
  return formatter_get_size_str (&mem_units, buf, bytes_per_second, buflen);
}

void
tr_formatter_get_units (void * vdict)
{
  int i;
  tr_variant * l;
  tr_variant * dict = vdict;

  tr_variantDictReserve (dict, 6);

  tr_variantDictAddInt (dict, TR_KEY_memory_bytes, mem_units.units[TR_FMT_KB].value);
  l = tr_variantDictAddList (dict, TR_KEY_memory_units, 4);
  for (i=0; i<4; i++)
    tr_variantListAddStr (l, mem_units.units[i].name);

  tr_variantDictAddInt (dict, TR_KEY_size_bytes,   size_units.units[TR_FMT_KB].value);
  l = tr_variantDictAddList (dict, TR_KEY_size_units, 4);
  for (i=0; i<4; i++)
    tr_variantListAddStr (l, size_units.units[i].name);

  tr_variantDictAddInt (dict, TR_KEY_speed_bytes,  speed_units.units[TR_FMT_KB].value);
  l = tr_variantDictAddList (dict, TR_KEY_speed_units, 4);
  for (i=0; i<4; i++)
    tr_variantListAddStr (l, speed_units.units[i].name);
}

/***
****  ENVIRONMENT
***/

bool
tr_env_key_exists (const char * key)
{
  assert (key != NULL);

#ifdef _WIN32

  return GetEnvironmentVariableA (key, NULL, 0) != 0;

#else

  return getenv (key) != NULL;

#endif
}

int
tr_env_get_int (const char * key,
                int          default_value)
{
#ifdef _WIN32

  char value[16];

  assert (key != NULL);

  if (GetEnvironmentVariableA (key, value, ARRAYSIZE (value)) > 1)
    return atoi (value);

#else

  const char * value;

  assert (key != NULL);

  value = getenv (key);

  if (value != NULL && *value != '\0')
    return atoi (value);

#endif

  return default_value;
}

char * tr_env_get_string (const char * key,
                          const char * default_value)
{
#ifdef _WIN32

  wchar_t * wide_key;
  char * value = NULL;

  wide_key = tr_win32_utf8_to_native (key, -1);
  if (wide_key != NULL)
    {
      const DWORD size = GetEnvironmentVariableW (wide_key, NULL, 0);
      if (size != 0)
        {
          wchar_t * const wide_value = tr_new (wchar_t, size);
          if (GetEnvironmentVariableW (wide_key, wide_value, size) == size - 1)
            value = tr_win32_native_to_utf8 (wide_value, size);

          tr_free (wide_value);
        }

      tr_free (wide_key);
    }

  if (value == NULL && default_value != NULL)
    value = tr_strdup (default_value);

  return value;

#else

  char * value;

  assert (key != NULL);

  value = getenv (key);
  if (value == NULL)
    value = (char *) default_value;

  if (value != NULL)
    value = tr_strdup (value);

  return value;

#endif
}

/***
****
***/

void
tr_net_init (void)
{
    static bool initialized = false;

    if (!initialized)
    {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup (MAKEWORD (2, 2), &wsaData);
#endif
        initialized = true;
    }
}
