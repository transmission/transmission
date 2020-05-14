/*
 * This file Copyright (C) 2009-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#ifdef HAVE_MEMMEM
#define _GNU_SOURCE /* glibc's string.h needs this to pick up memmem */
#endif

#if defined(XCODE_BUILD)
#define HAVE_GETPAGESIZE
#define HAVE_VALLOC
#endif

#include <ctype.h> /* isdigit(), tolower() */
#include <errno.h>
#include <float.h> /* DBL_DIG */
#include <locale.h> /* localeconv() */
#include <math.h> /* fabs(), floor() */
#include <stdio.h>
#include <stdlib.h> /* getenv() */
#include <string.h> /* strerror(), memset(), memmem() */
#include <time.h> /* nanosleep() */

#ifdef _WIN32
#include <ws2tcpip.h> /* WSAStartup() */
#include <windows.h> /* Sleep(), GetSystemTimeAsFileTime(), GetEnvironmentVariable() */
#include <shellapi.h> /* CommandLineToArgv() */
#include <shlwapi.h> /* StrStrIA() */
#else
#include <sys/time.h>
#include <unistd.h> /* getpagesize() */
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
#include "platform.h" /* tr_lockLock() */
#include "platform-quota.h" /* tr_device_info_create(), tr_device_info_get_free_space(), tr_device_info_free() */
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"
#include "version.h"

time_t __tr_current_time = 0;

/***
****
***/

struct tm* tr_localtime_r(time_t const* _clock, struct tm* _result)
{
#ifdef HAVE_LOCALTIME_R

    return localtime_r(_clock, _result);

#else

    struct tm* p = localtime(_clock);

    if (p != NULL)
    {
        *(_result) = *p;
    }

    return p;

#endif
}

int tr_gettimeofday(struct timeval* tv)
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

    return gettimeofday(tv, NULL);

#endif
}

/***
****
***/

void* tr_malloc(size_t size)
{
    return size != 0 ? malloc(size) : NULL;
}

void* tr_malloc0(size_t size)
{
    return size != 0 ? calloc(1, size) : NULL;
}

void* tr_realloc(void* p, size_t size)
{
    void* result = size != 0 ? realloc(p, size) : NULL;

    if (result == NULL)
    {
        tr_free(p);
    }

    return result;
}

void tr_free(void* p)
{
    if (p != NULL)
    {
        free(p);
    }
}

void tr_free_ptrv(void* const* p)
{
    if (p == NULL)
    {
        return;
    }

    while (*p != NULL)
    {
        tr_free(*p);
        ++p;
    }
}

void* tr_memdup(void const* src, size_t byteCount)
{
    return memcpy(tr_malloc(byteCount), src, byteCount);
}

/***
****
***/

char const* tr_strip_positional_args(char const* str)
{
    char* out;
    static size_t bufsize = 0;
    static char* buf = NULL;
    char const* in = str;
    size_t const len = str != NULL ? strlen(str) : 0;

    if (buf == NULL || bufsize < len)
    {
        bufsize = len * 2 + 1;
        buf = tr_renew(char, buf, bufsize);
    }

    out = buf;

    for (; !tr_str_is_empty(str); ++str)
    {
        *out++ = *str;

        if (*str == '%' && isdigit(str[1]))
        {
            char const* tmp = str + 1;

            while (isdigit(*tmp))
            {
                ++tmp;
            }

            if (*tmp == '$')
            {
                str = tmp[1] == '\'' ? tmp + 1 : tmp;
            }
        }

        if (*str == '%' && str[1] == '\'')
        {
            str = str + 1;
        }
    }

    *out = '\0';
    return (in == NULL || strcmp(buf, in) != 0) ? buf : in;
}

/**
***
**/

void tr_timerAdd(struct event* timer, int seconds, int microseconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = microseconds;

    TR_ASSERT(tv.tv_sec >= 0);
    TR_ASSERT(tv.tv_usec >= 0);
    TR_ASSERT(tv.tv_usec < 1000000);

    evtimer_add(timer, &tv);
}

void tr_timerAddMsec(struct event* timer, int msec)
{
    int const seconds = msec / 1000;
    int const usec = (msec % 1000) * 1000;
    tr_timerAdd(timer, seconds, usec);
}

/**
***
**/

uint8_t* tr_loadFile(char const* path, size_t* size, tr_error** error)
{
    uint8_t* buf;
    tr_sys_path_info info;
    tr_sys_file_t fd;
    tr_error* my_error = NULL;
    char const* const err_fmt = _("Couldn't read \"%1$s\": %2$s");

    /* try to stat the file */
    if (!tr_sys_path_get_info(path, 0, &info, &my_error))
    {
        tr_logAddDebug(err_fmt, path, my_error->message);
        tr_error_propagate(error, &my_error);
        return NULL;
    }

    if (info.type != TR_SYS_PATH_IS_FILE)
    {
        tr_logAddError(err_fmt, path, _("Not a regular file"));
        tr_error_set_literal(error, TR_ERROR_EISDIR, _("Not a regular file"));
        return NULL;
    }

    /* file size should be able to fit into size_t */
    if (sizeof(info.size) > sizeof(*size))
    {
        TR_ASSERT(info.size <= SIZE_MAX);
    }

    /* Load the torrent file into our buffer */
    fd = tr_sys_file_open(path, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &my_error);

    if (fd == TR_BAD_SYS_FILE)
    {
        tr_logAddError(err_fmt, path, my_error->message);
        tr_error_propagate(error, &my_error);
        return NULL;
    }

    buf = tr_malloc(info.size + 1);

    if (!tr_sys_file_read(fd, buf, info.size, NULL, &my_error))
    {
        tr_logAddError(err_fmt, path, my_error->message);
        tr_sys_file_close(fd, NULL);
        free(buf);
        tr_error_propagate(error, &my_error);
        return NULL;
    }

    tr_sys_file_close(fd, NULL);
    buf[info.size] = '\0';
    *size = info.size;
    return buf;
}

char* tr_buildPath(char const* first_element, ...)
{
    char const* element;
    char* buf;
    char* pch;
    va_list vl;
    size_t bufLen = 0;

    /* pass 1: allocate enough space for the string */
    va_start(vl, first_element);
    element = first_element;

    while (element != NULL)
    {
        bufLen += strlen(element) + 1;
        element = va_arg(vl, char const*);
    }

    pch = buf = tr_new(char, bufLen);
    va_end(vl);

    if (buf == NULL)
    {
        return NULL;
    }

    /* pass 2: build the string piece by piece */
    va_start(vl, first_element);
    element = first_element;

    while (element != NULL)
    {
        size_t const elementLen = strlen(element);
        memcpy(pch, element, elementLen);
        pch += elementLen;
        *pch++ = TR_PATH_DELIMITER;
        element = va_arg(vl, char const*);
    }

    va_end(vl);

    /* terminate the string. if nonempty, eat the unwanted trailing slash */
    if (pch != buf)
    {
        --pch;
    }

    *pch++ = '\0';

    /* sanity checks & return */
    TR_ASSERT(pch - buf == (ptrdiff_t)bufLen);
    return buf;
}

int64_t tr_getDirFreeSpace(char const* dir)
{
    int64_t free_space;

    if (tr_str_is_empty(dir))
    {
        errno = EINVAL;
        free_space = -1;
    }
    else
    {
        struct tr_device_info* info;
        info = tr_device_info_create(dir);
        free_space = tr_device_info_get_free_space(info);
        tr_device_info_free(info);
    }

    return free_space;
}

/****
*****
****/

char* evbuffer_free_to_str(struct evbuffer* buf, size_t* result_len)
{
    size_t const n = evbuffer_get_length(buf);
    char* ret = tr_new(char, n + 1);
    evbuffer_copyout(buf, ret, n);
    evbuffer_free(buf);
    ret[n] = '\0';

    if (result_len != NULL)
    {
        *result_len = n;
    }

    return ret;
}

char* tr_strdup(void const* in)
{
    return tr_strndup(in, in != NULL ? strlen(in) : 0);
}

char* tr_strndup(void const* in, size_t len)
{
    char* out = NULL;

    if (len == TR_BAD_SIZE)
    {
        out = tr_strdup(in);
    }
    else if (in != NULL)
    {
        out = tr_malloc(len + 1);

        if (out != NULL)
        {
            memcpy(out, in, len);
            out[len] = '\0';
        }
    }

    return out;
}

char const* tr_memmem(char const* haystack, size_t haystacklen, char const* needle, size_t needlelen)
{
#ifdef HAVE_MEMMEM

    return memmem(haystack, haystacklen, needle, needlelen);

#else

    if (needlelen == 0)
    {
        return haystack;
    }

    if (needlelen > haystacklen || haystack == NULL || needle == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i <= haystacklen - needlelen; ++i)
    {
        if (memcmp(haystack + i, needle, needlelen) == 0)
        {
            return haystack + i;
        }
    }

    return NULL;

#endif
}

char const* tr_strcasestr(char const* haystack, char const* needle)
{
#ifdef HAVE_STRCASESTR

    return strcasestr(haystack, needle);

#elif defined(_WIN32)

    return StrStrIA(haystack, needle);

#else

#error please open a PR to implement tr_strcasestr() for your platform

#endif
}

char* tr_strdup_printf(char const* fmt, ...)
{
    va_list ap;
    char* ret;

    va_start(ap, fmt);
    ret = tr_strdup_vprintf(fmt, ap);
    va_end(ap);

    return ret;
}

char* tr_strdup_vprintf(char const* fmt, va_list args)
{
    struct evbuffer* buf = evbuffer_new();
    evbuffer_add_vprintf(buf, fmt, args);
    return evbuffer_free_to_str(buf, NULL);
}

char const* tr_strerror(int i)
{
    char const* ret = strerror(i);

    if (ret == NULL)
    {
        ret = "Unknown Error";
    }

    return ret;
}

int tr_strcmp0(char const* str1, char const* str2)
{
    if (str1 != NULL && str2 != NULL)
    {
        return strcmp(str1, str2);
    }

    if (str1 != NULL)
    {
        return 1;
    }

    if (str2 != NULL)
    {
        return -1;
    }

    return 0;
}

int tr_memcmp0(void const* lhs, void const* rhs, size_t size)
{
    if (lhs != NULL && rhs != NULL)
    {
        return memcmp(lhs, rhs, size);
    }

    if (lhs != NULL)
    {
        return 1;
    }

    if (rhs != NULL)
    {
        return -1;
    }

    return 0;
}

/****
*****
****/

/* https://bugs.launchpad.net/percona-patches/+bug/526863/+attachment/1160199/+files/solaris_10_fix.patch */
char* tr_strsep(char** str, char const* delims)
{
#ifdef HAVE_STRSEP

    return strsep(str, delims);

#else

    char* token;

    if (*str == NULL) /* no more tokens */
    {
        return NULL;
    }

    token = *str;

    while (**str != '\0')
    {
        if (strchr(delims, **str) != NULL)
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

char* tr_strjoin(char const* const* arr, size_t len, char const* delim)
{
    size_t total_len = 1;
    size_t delim_len = strlen(delim);
    for (size_t i = 0; i < len; ++i)
    {
        total_len += strlen(arr[i]);
    }

    total_len += len > 0 ? (len - 1) * delim_len : 0;

    char* const ret = tr_new(char, total_len);
    char* p = ret;

    for (size_t i = 0; i < len; ++i)
    {
        if (i > 0)
        {
            memcpy(p, delim, delim_len);
            p += delim_len;
        }

        size_t const part_len = strlen(arr[i]);
        memcpy(p, arr[i], part_len);
        p += part_len;
    }

    *p = '\0';
    return ret;
}

char* tr_strstrip(char* str)
{
    if (str != NULL)
    {
        size_t len = strlen(str);

        while (len != 0 && isspace(str[len - 1]))
        {
            --len;
        }

        size_t pos = 0;

        while (pos < len && isspace(str[pos]))
        {
            ++pos;
        }

        len -= pos;
        memmove(str, str + pos, len);
        str[len] = '\0';
    }

    return str;
}

bool tr_str_has_suffix(char const* str, char const* suffix)
{
    size_t str_len;
    size_t suffix_len;

    if (str == NULL)
    {
        return false;
    }

    if (suffix == NULL)
    {
        return true;
    }

    str_len = strlen(str);
    suffix_len = strlen(suffix);

    if (str_len < suffix_len)
    {
        return false;
    }

    return evutil_ascii_strncasecmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

/****
*****
****/

uint64_t tr_time_msec(void)
{
    struct timeval tv;

    tr_gettimeofday(&tv);
    return (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

void tr_wait_msec(long int msec)
{
#ifdef _WIN32

    Sleep((DWORD)msec);

#else

    struct timespec ts;
    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;
    nanosleep(&ts, NULL);

#endif
}

/***
****
***/

int tr_snprintf(char* buf, size_t buflen, char const* fmt, ...)
{
    int len;
    va_list args;

    va_start(args, fmt);
    len = evutil_vsnprintf(buf, buflen, fmt, args);
    va_end(args);
    return len;
}

/*
 * Copy src to string dst of size siz. At most siz-1 characters
 * will be copied. Always NUL terminates (unless siz == 0).
 * Returns strlen (src); if retval >= siz, truncation occurred.
 */
size_t tr_strlcpy(char* dst, void const* src, size_t siz)
{
    TR_ASSERT(dst != NULL);
    TR_ASSERT(src != NULL);

#ifdef HAVE_STRLCPY

    return strlcpy(dst, src, siz);

#else

    char* d = dst;
    char const* s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0)
    {
        while (--n != 0)
        {
            if ((*d++ = *s++) == '\0')
            {
                break;
            }
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0)
    {
        if (siz != 0)
        {
            *d = '\0'; /* NUL-terminate dst */
        }

        while (*s++ != '\0')
        {
        }
    }

    return s - (char const*)src - 1; /* count does not include NUL */

#endif
}

/***
****
***/

double tr_getRatio(uint64_t numerator, uint64_t denominator)
{
    double ratio;

    if (denominator > 0)
    {
        ratio = numerator / (double)denominator;
    }
    else if (numerator > 0)
    {
        ratio = TR_RATIO_INF;
    }
    else
    {
        ratio = TR_RATIO_NA;
    }

    return ratio;
}

void tr_binary_to_hex(void const* input, char* output, size_t byte_length)
{
    static char const hex[] = "0123456789abcdef";
    uint8_t const* input_octets = input;

    /* go from back to front to allow for in-place conversion */
    input_octets += byte_length;
    output += byte_length * 2;

    *output = '\0';

    while (byte_length-- > 0)
    {
        unsigned int const val = *(--input_octets);
        *(--output) = hex[val & 0xf];
        *(--output) = hex[val >> 4];
    }
}

void tr_hex_to_binary(char const* input, void* output, size_t byte_length)
{
    static char const hex[] = "0123456789abcdef";
    uint8_t* output_octets = output;

    for (size_t i = 0; i < byte_length; ++i)
    {
        int const hi = strchr(hex, tolower(*input++)) - hex;
        int const lo = strchr(hex, tolower(*input++)) - hex;
        *output_octets++ = (uint8_t)((hi << 4) | lo);
    }
}

/***
****
***/

static bool isValidURLChars(char const* url, size_t url_len)
{
    static char const rfc2396_valid_chars[] =
        "abcdefghijklmnopqrstuvwxyz" /* lowalpha */
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ" /* upalpha */
        "0123456789" /* digit */
        "-_.!~*'()" /* mark */
        ";/?:@&=+$," /* reserved */
        "<>#%<\"" /* delims */
        "{}|\\^[]`"; /* unwise */

    if (url == NULL)
    {
        return false;
    }

    for (char const* c = url, * end = url + url_len; c < end && *c != '\0'; ++c)
    {
        if (memchr(rfc2396_valid_chars, *c, sizeof(rfc2396_valid_chars) - 1) == NULL)
        {
            return false;
        }
    }

    return true;
}

bool tr_urlIsValidTracker(char const* url)
{
    if (url == NULL)
    {
        return false;
    }

    size_t const url_len = strlen(url);

    return isValidURLChars(url, url_len) && tr_urlParse(url, url_len, NULL, NULL, NULL, NULL) &&
        (memcmp(url, "http://", 7) == 0 || memcmp(url, "https://", 8) == 0 || memcmp(url, "udp://", 6) == 0);
}

bool tr_urlIsValid(char const* url, size_t url_len)
{
    if (url == NULL)
    {
        return false;
    }

    if (url_len == TR_BAD_SIZE)
    {
        url_len = strlen(url);
    }

    return isValidURLChars(url, url_len) && tr_urlParse(url, url_len, NULL, NULL, NULL, NULL) &&
        (memcmp(url, "http://", 7) == 0 || memcmp(url, "https://", 8) == 0 || memcmp(url, "ftp://", 6) == 0 ||
            memcmp(url, "sftp://", 7) == 0);
}

bool tr_addressIsIP(char const* str)
{
    tr_address tmp;
    return tr_address_from_string(&tmp, str);
}

static int parse_port(char const* port, size_t port_len)
{
    char* tmp = tr_strndup(port, port_len);
    char* end;

    long port_num = strtol(tmp, &end, 10);

    if (*end != '\0' || port_num <= 0 || port_num >= 65536)
    {
        port_num = -1;
    }

    tr_free(tmp);

    return (int)port_num;
}

static int get_port_for_scheme(char const* scheme, size_t scheme_len)
{
    struct known_scheme
    {
        char const* name;
        int port;
    };

    static struct known_scheme const known_schemes[] =
    {
        { "udp", 80 },
        { "ftp", 21 },
        { "sftp", 22 },
        { "http", 80 },
        { "https", 443 },
        { NULL, 0 }
    };

    for (struct known_scheme const* s = known_schemes; s->name != NULL; ++s)
    {
        if (scheme_len == strlen(s->name) && memcmp(scheme, s->name, scheme_len) == 0)
        {
            return s->port;
        }
    }

    return -1;
}

bool tr_urlParse(char const* url, size_t url_len, char** setme_scheme, char** setme_host, int* setme_port, char** setme_path)
{
    if (url_len == TR_BAD_SIZE)
    {
        url_len = strlen(url);
    }

    char const* scheme = url;
    char const* scheme_end = tr_memmem(scheme, url_len, "://", 3);

    if (scheme_end == NULL)
    {
        return false;
    }

    size_t const scheme_len = scheme_end - scheme;

    if (scheme_len == 0)
    {
        return false;
    }

    url += scheme_len + 3;
    url_len -= scheme_len + 3;

    char const* authority = url;
    char const* authority_end = memchr(authority, '/', url_len);

    if (authority_end == NULL)
    {
        authority_end = authority + url_len;
    }

    size_t const authority_len = authority_end - authority;

    if (authority_len == 0)
    {
        return false;
    }

    url += authority_len;
    url_len -= authority_len;

    char const* host_end = memchr(authority, ':', authority_len);

    size_t const host_len = host_end != NULL ? (size_t)(host_end - authority) : authority_len;

    if (host_len == 0)
    {
        return false;
    }

    size_t const port_len = host_end != NULL ? authority_end - host_end - 1 : 0;

    if (setme_scheme != NULL)
    {
        *setme_scheme = tr_strndup(scheme, scheme_len);
    }

    if (setme_host != NULL)
    {
        *setme_host = tr_strndup(authority, host_len);
    }

    if (setme_port != NULL)
    {
        *setme_port = port_len > 0 ? parse_port(host_end + 1, port_len) : get_port_for_scheme(scheme, scheme_len);
    }

    if (setme_path != NULL)
    {
        if (url[0] == '\0')
        {
            *setme_path = tr_strdup("/");
        }
        else
        {
            *setme_path = tr_strndup(url, url_len);
        }
    }

    return true;
}

/***
****
***/

void tr_removeElementFromArray(void* array, unsigned int index_to_remove, size_t sizeof_element, size_t nmemb)
{
    char* a = array;

    memmove(a + sizeof_element * index_to_remove, a + sizeof_element * (index_to_remove + 1),
        sizeof_element * (--nmemb - index_to_remove));
}

int tr_lowerBound(void const* key, void const* base, size_t nmemb, size_t size, tr_voidptr_compare_func compar,
    bool* exact_match)
{
    size_t first = 0;
    char const* cbase = base;
    bool exact = false;

    while (nmemb != 0)
    {
        size_t const half = nmemb / 2;
        size_t const middle = first + half;
        int const c = (*compar)(key, cbase + size * middle);

        if (c <= 0)
        {
            if (c == 0)
            {
                exact = true;
            }

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
    do \
    { \
        register size_t __size = (size); \
        register char* __a = (a); \
        register char* __b = (b); \
        if (__a != __b) \
        { \
            do \
            { \
                char __tmp = *__a; \
                *__a++ = *__b; \
                *__b++ = __tmp; \
            } \
            while (--__size > 0); \
        } \
    } \
    while (0)

static size_t quickfindPartition(char* base, size_t left, size_t right, size_t size, tr_voidptr_compare_func compar,
    size_t pivotIndex)
{
    size_t storeIndex;

    /* move pivot to the end */
    SWAP(base + (size * pivotIndex), base + (size * right), size);

    storeIndex = left;

    for (size_t i = left; i < right; ++i)
    {
        if ((*compar)(base + (size * i), base + (size * right)) <= 0)
        {
            SWAP(base + (size * storeIndex), base + (size * i), size);
            ++storeIndex;
        }
    }

    /* move pivot to its final place */
    SWAP(base + (size * right), base + (size * storeIndex), size);

    /* sanity check the partition */
#ifdef TR_ENABLE_ASSERTS

    TR_ASSERT(storeIndex >= left);
    TR_ASSERT(storeIndex <= right);

    for (size_t i = left; i < storeIndex; ++i)
    {
        TR_ASSERT((*compar)(base + (size * i), base + (size * storeIndex)) <= 0);
    }

    for (size_t i = storeIndex + 1; i <= right; ++i)
    {
        TR_ASSERT((*compar)(base + (size * i), base + (size * storeIndex)) >= 0);
    }

#endif

    return storeIndex;
}

static void quickfindFirstK(char* base, size_t left, size_t right, size_t size, tr_voidptr_compare_func compar, size_t k)
{
    if (right > left)
    {
        size_t const pivotIndex = left + (right - left) / 2U;

        size_t const pivotNewIndex = quickfindPartition(base, left, right, size, compar, pivotIndex);

        if (pivotNewIndex > left + k) /* new condition */
        {
            quickfindFirstK(base, left, pivotNewIndex - 1, size, compar, k);
        }
        else if (pivotNewIndex < left + k)
        {
            quickfindFirstK(base, pivotNewIndex + 1, right, size, compar, k + left - pivotNewIndex - 1);
        }
    }
}

#ifdef TR_ENABLE_ASSERTS

static void checkBestScoresComeFirst(char* base, size_t nmemb, size_t size, tr_voidptr_compare_func compar, size_t k)
{
    size_t worstFirstPos = 0;

    for (size_t i = 1; i < k; ++i)
    {
        if ((*compar)(base + (size * worstFirstPos), base + (size * i)) < 0)
        {
            worstFirstPos = i;
        }
    }

    for (size_t i = 0; i < k; ++i)
    {
        TR_ASSERT((*compar)(base + (size * i), base + (size * worstFirstPos)) <= 0);
    }

    for (size_t i = k; i < nmemb; ++i)
    {
        TR_ASSERT((*compar)(base + (size * i), base + (size * worstFirstPos)) >= 0);
    }
}

#endif

void tr_quickfindFirstK(void* base, size_t nmemb, size_t size, tr_voidptr_compare_func compar, size_t k)
{
    if (k < nmemb)
    {
        quickfindFirstK(base, 0, nmemb - 1, size, compar, k);

#ifdef TR_ENABLE_ASSERTS
        checkBestScoresComeFirst(base, nmemb, size, compar, k);
#endif
    }
}

/***
****
***/

static char* strip_non_utf8(char const* in, size_t inlen)
{
    char const* end;
    struct evbuffer* buf = evbuffer_new();

    while (!tr_utf8_validate(in, inlen, &end))
    {
        int const good_len = end - in;

        evbuffer_add(buf, in, good_len);
        inlen -= (good_len + 1);
        in += (good_len + 1);
        evbuffer_add(buf, "?", 1);
    }

    evbuffer_add(buf, in, inlen);
    return evbuffer_free_to_str(buf, NULL);
}

static char* to_utf8(char const* in, size_t inlen)
{
    char* ret = NULL;

#ifdef HAVE_ICONV

    char const* encodings[] = { "CURRENT", "ISO-8859-15" };
    size_t const buflen = inlen * 4 + 10;
    char* out = tr_new(char, buflen);

    for (size_t i = 0; ret == NULL && i < TR_N_ELEMENTS(encodings); ++i)
    {
#ifdef ICONV_SECOND_ARGUMENT_IS_CONST
        char const* inbuf = in;
#else
        char* inbuf = (char*)in;
#endif
        char* outbuf = out;
        size_t inbytesleft = inlen;
        size_t outbytesleft = buflen;
        char const* test_encoding = encodings[i];

        iconv_t cd = iconv_open("UTF-8", test_encoding);

        if (cd != (iconv_t)-1)
        {
            if (iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft) != (size_t)-1)
            {
                ret = tr_strndup(out, buflen - outbytesleft);
            }

            iconv_close(cd);
        }
    }

    tr_free(out);

#endif

    if (ret == NULL)
    {
        ret = strip_non_utf8(in, inlen);
    }

    return ret;
}

char* tr_utf8clean(char const* str, size_t max_len)
{
    char* ret;
    char const* end;

    if (max_len == TR_BAD_SIZE)
    {
        max_len = strlen(str);
    }

    if (tr_utf8_validate(str, max_len, &end))
    {
        ret = tr_strndup(str, max_len);
    }
    else
    {
        ret = to_utf8(str, max_len);
    }

    TR_ASSERT(tr_utf8_validate(ret, TR_BAD_SIZE, NULL));
    return ret;
}

#ifdef _WIN32

char* tr_win32_native_to_utf8(wchar_t const* text, int text_size)
{
    return tr_win32_native_to_utf8_ex(text, text_size, 0, 0, NULL);
}

char* tr_win32_native_to_utf8_ex(wchar_t const* text, int text_size, int extra_chars_before, int extra_chars_after,
    int* real_result_size)
{
    char* ret = NULL;
    int size;

    if (text_size == -1)
    {
        text_size = wcslen(text);
    }

    size = WideCharToMultiByte(CP_UTF8, 0, text, text_size, NULL, 0, NULL, NULL);

    if (size == 0)
    {
        goto fail;
    }

    ret = tr_new(char, size + extra_chars_before + extra_chars_after + 1);
    size = WideCharToMultiByte(CP_UTF8, 0, text, text_size, ret + extra_chars_before, size, NULL, NULL);

    if (size == 0)
    {
        goto fail;
    }

    ret[size + extra_chars_before + extra_chars_after] = '\0';

    if (real_result_size != NULL)
    {
        *real_result_size = size;
    }

    return ret;

fail:
    tr_free(ret);

    return NULL;
}

wchar_t* tr_win32_utf8_to_native(char const* text, int text_size)
{
    return tr_win32_utf8_to_native_ex(text, text_size, 0, 0, NULL);
}

wchar_t* tr_win32_utf8_to_native_ex(char const* text, int text_size, int extra_chars_before, int extra_chars_after,
    int* real_result_size)
{
    wchar_t* ret = NULL;
    int size;

    if (text_size == -1)
    {
        text_size = strlen(text);
    }

    size = MultiByteToWideChar(CP_UTF8, 0, text, text_size, NULL, 0);

    if (size == 0)
    {
        goto fail;
    }

    ret = tr_new(wchar_t, size + extra_chars_before + extra_chars_after + 1);
    size = MultiByteToWideChar(CP_UTF8, 0, text, text_size, ret + extra_chars_before, size);

    if (size == 0)
    {
        goto fail;
    }

    ret[size + extra_chars_before + extra_chars_after] = L'\0';

    if (real_result_size != NULL)
    {
        *real_result_size = size;
    }

    return ret;

fail:
    tr_free(ret);

    return NULL;
}

char* tr_win32_format_message(uint32_t code)
{
    wchar_t* wide_text = NULL;
    DWORD wide_size;
    char* text = NULL;
    size_t text_size;

    wide_size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code, 0, (LPWSTR)&wide_text, 0, NULL);

    if (wide_size == 0)
    {
        return tr_strdup_printf("Unknown error (0x%08x)", code);
    }

    if (wide_size != 0 && wide_text != NULL)
    {
        text = tr_win32_native_to_utf8(wide_text, wide_size);
    }

    LocalFree(wide_text);

    if (text != NULL)
    {
        /* Most (all?) messages contain "\r\n" in the end, chop it */
        text_size = strlen(text);

        while (text_size > 0 && isspace((uint8_t)text[text_size - 1]))
        {
            text[--text_size] = '\0';
        }
    }

    return text;
}

void tr_win32_make_args_utf8(int* argc, char*** argv)
{
    int my_argc;
    wchar_t** my_wide_argv;

    my_wide_argv = CommandLineToArgvW(GetCommandLineW(), &my_argc);

    if (my_wide_argv == NULL)
    {
        return;
    }

    TR_ASSERT(*argc == my_argc);

    char** my_argv = tr_new(char*, my_argc + 1);
    int processed_argc = 0;

    for (int i = 0; i < my_argc; ++i, ++processed_argc)
    {
        my_argv[i] = tr_win32_native_to_utf8(my_wide_argv[i], -1);

        if (my_argv[i] == NULL)
        {
            break;
        }
    }

    if (processed_argc < my_argc)
    {
        for (int i = 0; i < processed_argc; ++i)
        {
            tr_free(my_argv[i]);
        }

        tr_free(my_argv);
    }
    else
    {
        my_argv[my_argc] = NULL;

        *argc = my_argc;
        *argv = my_argv;

        /* TODO: Add atexit handler to cleanup? */
    }

    LocalFree(my_wide_argv);
}

int tr_main_win32(int argc, char** argv, int (* real_main)(int, char**))
{
    tr_win32_make_args_utf8(&argc, &argv);
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    return (*real_main)(argc, argv);
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
static bool parseNumberSection(char const* str, size_t len, struct number_range* setme)
{
    long a;
    long b;
    bool success;
    char* end;
    int const error = errno;
    char* tmp = tr_strndup(str, len);

    errno = 0;
    a = b = strtol(tmp, &end, 10);

    if (errno != 0 || end == tmp)
    {
        success = false;
    }
    else if (*end != '-')
    {
        success = true;
    }
    else
    {
        char const* pch = end + 1;
        b = strtol(pch, &end, 10);

        if (errno != 0 || pch == end)
        {
            success = false;
        }
        else if (*end != '\0') /* trailing data */
        {
            success = false;
        }
        else
        {
            success = true;
        }
    }

    tr_free(tmp);

    setme->low = MIN(a, b);
    setme->high = MAX(a, b);

    errno = error;
    return success;
}

int compareInt(void const* va, void const* vb)
{
    int const a = *(int const*)va;
    int const b = *(int const*)vb;
    return a - b;
}

/**
 * Given a string like "1-4" or "1-4,6,9,14-51", this allocates and returns an
 * array of setmeCount ints of all the values in the array.
 * For example, "5-8" will return [ 5, 6, 7, 8 ] and setmeCount will be 4.
 * It's the caller's responsibility to call tr_free () on the returned array.
 * If a fragment of the string can't be parsed, NULL is returned.
 */
int* tr_parseNumberRange(char const* str_in, size_t len, int* setmeCount)
{
    int n = 0;
    int* uniq = NULL;
    char* str = tr_strndup(str_in, len);
    char const* walk;
    tr_list* ranges = NULL;
    bool success = true;

    walk = str;

    while (!tr_str_is_empty(walk) && success)
    {
        struct number_range range;
        char const* pch = strchr(walk, ',');

        if (pch != NULL)
        {
            success = parseNumberSection(walk, (size_t)(pch - walk), &range);
            walk = pch + 1;
        }
        else
        {
            success = parseNumberSection(walk, strlen(walk), &range);
            walk += strlen(walk);
        }

        if (success)
        {
            tr_list_append(&ranges, tr_memdup(&range, sizeof(struct number_range)));
        }
    }

    if (!success)
    {
        *setmeCount = 0;
        uniq = NULL;
    }
    else
    {
        int n2;
        int* sorted = NULL;

        /* build a sorted number array */
        n = n2 = 0;

        for (tr_list* l = ranges; l != NULL; l = l->next)
        {
            struct number_range const* r = l->data;
            n += r->high + 1 - r->low;
        }

        sorted = tr_new(int, n);

        if (sorted == NULL)
        {
            n = 0;
            uniq = NULL;
        }
        else
        {
            for (tr_list* l = ranges; l != NULL; l = l->next)
            {
                struct number_range const* r = l->data;

                for (int i = r->low; i <= r->high; ++i)
                {
                    sorted[n2++] = i;
                }
            }

            qsort(sorted, n, sizeof(int), compareInt);
            TR_ASSERT(n == n2);

            /* remove duplicates */
            uniq = tr_new(int, n);
            n = 0;

            if (uniq != NULL)
            {
                for (int i = 0; i < n2; ++i)
                {
                    if (n == 0 || uniq[n - 1] != sorted[i])
                    {
                        uniq[n++] = sorted[i];
                    }
                }
            }

            tr_free(sorted);
        }
    }

    /* cleanup */
    tr_list_free(&ranges, tr_free);
    tr_free(str);

    /* return the result */
    *setmeCount = n;
    return uniq;
}

/***
****
***/

double tr_truncd(double x, int precision)
{
    char* pt;
    char buf[128];
    tr_snprintf(buf, sizeof(buf), "%.*f", DBL_DIG, x);

    if ((pt = strstr(buf, localeconv()->decimal_point)) != NULL)
    {
        pt[precision != 0 ? precision + 1 : 0] = '\0';
    }

    return atof(buf);
}

/* return a truncated double as a string */
static char* tr_strtruncd(char* buf, double x, int precision, size_t buflen)
{
    tr_snprintf(buf, buflen, "%.*f", precision, tr_truncd(x, precision));
    return buf;
}

char* tr_strpercent(char* buf, double x, size_t buflen)
{
    if (x < 100.0)
    {
        tr_strtruncd(buf, x, 1, buflen);
    }
    else
    {
        tr_strtruncd(buf, x, 0, buflen);
    }

    return buf;
}

char* tr_strratio(char* buf, size_t buflen, double ratio, char const* infinity)
{
    if ((int)ratio == TR_RATIO_NA)
    {
        tr_strlcpy(buf, _("None"), buflen);
    }
    else if ((int)ratio == TR_RATIO_INF)
    {
        tr_strlcpy(buf, infinity, buflen);
    }
    else
    {
        tr_strpercent(buf, ratio, buflen);
    }

    return buf;
}

/***
****
***/

bool tr_moveFile(char const* oldpath, char const* newpath, tr_error** error)
{
    tr_sys_file_t in;
    tr_sys_file_t out;
    char* buf = NULL;
    tr_sys_path_info info;
    uint64_t bytesLeft;
    size_t const buflen = 1024 * 1024; /* 1024 KiB buffer */

    /* make sure the old file exists */
    if (!tr_sys_path_get_info(oldpath, 0, &info, error))
    {
        tr_error_prefix(error, "Unable to get information on old file: ");
        return false;
    }

    if (info.type != TR_SYS_PATH_IS_FILE)
    {
        tr_error_set_literal(error, TR_ERROR_EINVAL, "Old path does not point to a file.");
        return false;
    }

    /* make sure the target directory exists */
    {
        char* newdir = tr_sys_path_dirname(newpath, error);
        bool const i = newdir != NULL && tr_sys_dir_create(newdir, TR_SYS_DIR_CREATE_PARENTS, 0777, error);
        tr_free(newdir);

        if (!i)
        {
            tr_error_prefix(error, "Unable to create directory for new file: ");
            return false;
        }
    }

    /* they might be on the same filesystem... */
    if (tr_sys_path_rename(oldpath, newpath, NULL))
    {
        return true;
    }

    /* copy the file */
    in = tr_sys_file_open(oldpath, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, error);

    if (in == TR_BAD_SYS_FILE)
    {
        tr_error_prefix(error, "Unable to open old file: ");
        return false;
    }

    out = tr_sys_file_open(newpath, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0666, error);

    if (out == TR_BAD_SYS_FILE)
    {
        tr_error_prefix(error, "Unable to open new file: ");
        tr_sys_file_close(in, NULL);
        return false;
    }

    buf = tr_valloc(buflen);
    bytesLeft = info.size;

    while (bytesLeft > 0)
    {
        uint64_t const bytesThisPass = MIN(bytesLeft, buflen);
        uint64_t numRead;
        uint64_t bytesWritten;

        if (!tr_sys_file_read(in, buf, bytesThisPass, &numRead, error))
        {
            break;
        }

        if (!tr_sys_file_write(out, buf, numRead, &bytesWritten, error))
        {
            break;
        }

        TR_ASSERT(numRead == bytesWritten);
        TR_ASSERT(bytesWritten <= bytesLeft);
        bytesLeft -= bytesWritten;
    }

    /* cleanup */
    tr_free(buf);
    tr_sys_file_close(out, NULL);
    tr_sys_file_close(in, NULL);

    if (bytesLeft != 0)
    {
        tr_error_prefix(error, "Unable to read/write: ");
        return false;
    }

    {
        tr_error* my_error = NULL;

        if (!tr_sys_path_remove(oldpath, &my_error))
        {
            tr_logAddError("Unable to remove file at old path: %s", my_error->message);
            tr_error_free(my_error);
        }
    }

    return true;
}

/***
****
***/

void* tr_valloc(size_t bufLen)
{
    size_t allocLen;
    void* buf = NULL;
    static size_t pageSize = 0;

    if (pageSize == 0)
    {
#if defined(HAVE_GETPAGESIZE) && !defined(_WIN32)
        pageSize = (size_t)getpagesize();
#else /* guess */
        pageSize = 4096;
#endif
    }

    allocLen = pageSize;

    while (allocLen < bufLen)
    {
        allocLen += pageSize;
    }

#ifdef HAVE_POSIX_MEMALIGN

    if (buf == NULL)
    {
        if (posix_memalign(&buf, pageSize, allocLen) != 0)
        {
            buf = NULL; /* just retry with valloc/malloc */
        }
    }

#endif

#ifdef HAVE_VALLOC

    if (buf == NULL)
    {
        buf = valloc(allocLen);
    }

#endif

    if (buf == NULL)
    {
        buf = tr_malloc(allocLen);
    }

    return buf;
}

/***
****
***/

uint64_t tr_htonll(uint64_t x)
{
#ifdef HAVE_HTONLL

    return htonll(x);

#else

    /* fallback code by bdonlan at http://stackoverflow.com/questions/809902/64-bit-ntohl-in-c/875505#875505 */
    union
    {
        uint32_t lx[2];
        uint64_t llx;
    }
    u;
    u.lx[0] = htonl(x >> 32);
    u.lx[1] = htonl(x & 0xFFFFFFFFULL);
    return u.llx;

#endif
}

uint64_t tr_ntohll(uint64_t x)
{
#ifdef HAVE_NTOHLL

    return ntohll(x);

#else

    /* fallback code by bdonlan at http://stackoverflow.com/questions/809902/64-bit-ntohl-in-c/875505#875505 */
    union
    {
        uint32_t lx[2];
        uint64_t llx;
    }
    u;
    u.llx = x;
    return ((uint64_t)ntohl(u.lx[0]) << 32) | (uint64_t)ntohl(u.lx[1]);

#endif
}

/***
****
****
****
***/

struct formatter_unit
{
    char* name;
    int64_t value;
};

struct formatter_units
{
    struct formatter_unit units[4];
};

enum
{
    TR_FMT_KB,
    TR_FMT_MB,
    TR_FMT_GB,
    TR_FMT_TB
};

static void formatter_init(struct formatter_units* units, unsigned int kilo, char const* kb, char const* mb, char const* gb,
    char const* tb)
{
    uint64_t value;

    value = kilo;
    units->units[TR_FMT_KB].name = tr_strdup(kb);
    units->units[TR_FMT_KB].value = value;

    value *= kilo;
    units->units[TR_FMT_MB].name = tr_strdup(mb);
    units->units[TR_FMT_MB].value = value;

    value *= kilo;
    units->units[TR_FMT_GB].name = tr_strdup(gb);
    units->units[TR_FMT_GB].value = value;

    value *= kilo;
    units->units[TR_FMT_TB].name = tr_strdup(tb);
    units->units[TR_FMT_TB].value = value;
}

static char* formatter_get_size_str(struct formatter_units const* u, char* buf, int64_t bytes, size_t buflen)
{
    int precision;
    double value;
    char const* units;
    struct formatter_unit const* unit;

    if (bytes < u->units[1].value)
    {
        unit = &u->units[0];
    }
    else if (bytes < u->units[2].value)
    {
        unit = &u->units[1];
    }
    else if (bytes < u->units[3].value)
    {
        unit = &u->units[2];
    }
    else
    {
        unit = &u->units[3];
    }

    value = (double)bytes / unit->value;
    units = unit->name;

    if (unit->value == 1)
    {
        precision = 0;
    }
    else if (value < 100)
    {
        precision = 2;
    }
    else
    {
        precision = 1;
    }

    tr_snprintf(buf, buflen, "%.*f %s", precision, value, units);
    return buf;
}

static struct formatter_units size_units;

void tr_formatter_size_init(unsigned int kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    formatter_init(&size_units, kilo, kb, mb, gb, tb);
}

char* tr_formatter_size_B(char* buf, int64_t bytes, size_t buflen)
{
    return formatter_get_size_str(&size_units, buf, bytes, buflen);
}

static struct formatter_units speed_units;

unsigned int tr_speed_K = 0U;

void tr_formatter_speed_init(unsigned int kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    tr_speed_K = kilo;
    formatter_init(&speed_units, kilo, kb, mb, gb, tb);
}

char* tr_formatter_speed_KBps(char* buf, double KBps, size_t buflen)
{
    double const K = speed_units.units[TR_FMT_KB].value;
    double speed = KBps;

    if (speed <= 999.95) /* 0.0 KB to 999.9 KB */
    {
        tr_snprintf(buf, buflen, "%d %s", (int)speed, speed_units.units[TR_FMT_KB].name);
    }
    else
    {
        speed /= K;

        if (speed <= 99.995) /* 0.98 MB to 99.99 MB */
        {
            tr_snprintf(buf, buflen, "%.2f %s", speed, speed_units.units[TR_FMT_MB].name);
        }
        else if (speed <= 999.95) /* 100.0 MB to 999.9 MB */
        {
            tr_snprintf(buf, buflen, "%.1f %s", speed, speed_units.units[TR_FMT_MB].name);
        }
        else
        {
            tr_snprintf(buf, buflen, "%.1f %s", speed / K, speed_units.units[TR_FMT_GB].name);
        }
    }

    return buf;
}

static struct formatter_units mem_units;

unsigned int tr_mem_K = 0U;

void tr_formatter_mem_init(unsigned int kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    tr_mem_K = kilo;
    formatter_init(&mem_units, kilo, kb, mb, gb, tb);
}

char* tr_formatter_mem_B(char* buf, int64_t bytes_per_second, size_t buflen)
{
    return formatter_get_size_str(&mem_units, buf, bytes_per_second, buflen);
}

void tr_formatter_get_units(void* vdict)
{
    tr_variant* l;
    tr_variant* dict = vdict;

    tr_variantDictReserve(dict, 6);

    tr_variantDictAddInt(dict, TR_KEY_memory_bytes, mem_units.units[TR_FMT_KB].value);
    l = tr_variantDictAddList(dict, TR_KEY_memory_units, 4);

    for (int i = 0; i < 4; i++)
    {
        tr_variantListAddStr(l, mem_units.units[i].name);
    }

    tr_variantDictAddInt(dict, TR_KEY_size_bytes, size_units.units[TR_FMT_KB].value);
    l = tr_variantDictAddList(dict, TR_KEY_size_units, 4);

    for (int i = 0; i < 4; i++)
    {
        tr_variantListAddStr(l, size_units.units[i].name);
    }

    tr_variantDictAddInt(dict, TR_KEY_speed_bytes, speed_units.units[TR_FMT_KB].value);
    l = tr_variantDictAddList(dict, TR_KEY_speed_units, 4);

    for (int i = 0; i < 4; i++)
    {
        tr_variantListAddStr(l, speed_units.units[i].name);
    }
}

/***
****  ENVIRONMENT
***/

bool tr_env_key_exists(char const* key)
{
    TR_ASSERT(key != NULL);

#ifdef _WIN32
    return GetEnvironmentVariableA(key, NULL, 0) != 0;
#else
    return getenv(key) != NULL;
#endif
}

int tr_env_get_int(char const* key, int default_value)
{
    TR_ASSERT(key != NULL);

#ifdef _WIN32

    char value[16];

    if (GetEnvironmentVariableA(key, value, TR_N_ELEMENTS(value)) > 1)
    {
        return atoi(value);
    }

#else

    char const* value = getenv(key);

    if (!tr_str_is_empty(value))
    {
        return atoi(value);
    }

#endif

    return default_value;
}

char* tr_env_get_string(char const* key, char const* default_value)
{
    TR_ASSERT(key != NULL);

#ifdef _WIN32

    wchar_t* wide_key = tr_win32_utf8_to_native(key, -1);
    char* value = NULL;

    if (wide_key != NULL)
    {
        DWORD const size = GetEnvironmentVariableW(wide_key, NULL, 0);

        if (size != 0)
        {
            wchar_t* const wide_value = tr_new(wchar_t, size);

            if (GetEnvironmentVariableW(wide_key, wide_value, size) == size - 1)
            {
                value = tr_win32_native_to_utf8(wide_value, size);
            }

            tr_free(wide_value);
        }

        tr_free(wide_key);
    }

    if (value == NULL && default_value != NULL)
    {
        value = tr_strdup(default_value);
    }

    return value;

#else

    char* value = getenv(key);

    if (value == NULL)
    {
        value = (char*)default_value;
    }

    if (value != NULL)
    {
        value = tr_strdup(value);
    }

    return value;

#endif
}

/***
****
***/

void tr_net_init(void)
{
    static bool initialized = false;

    if (!initialized)
    {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        initialized = true;
    }
}
