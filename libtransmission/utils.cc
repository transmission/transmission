/*
 * This file Copyright (C) 2009-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#ifdef HAVE_MEMMEM
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* glibc's string.h needs this to pick up memmem */
#endif
#endif

#include <algorithm> // std::sort
#include <array> // std::array
#include <cctype> /* isdigit(), tolower() */
#include <cerrno>
#include <cfloat> /* DBL_DIG */
#include <clocale> /* localeconv() */
#include <cmath> /* fabs(), floor() */
#include <cstdint> /* SIZE_MAX */
#include <cstdio>
#include <cstdlib> /* getenv() */
#include <cstring> /* strerror(), memset(), memmem() */
#include <ctime> /* nanosleep() */
#include <exception>
#include <iterator> // std::back_inserter
#include <set>
#include <string>
#include <vector>

#if defined(__GNUC__) && !__has_include(<charconv>)
#undef HAVE_CHARCONV
#else
#define HAVE_CHARCONV 1
#include <charconv> // std::from_chars()
#endif

#ifdef _WIN32
#include <ws2tcpip.h> /* WSAStartup() */
#include <windows.h> /* Sleep(), GetSystemTimeAsFileTime(), GetEnvironmentVariable() */
#include <shellapi.h> /* CommandLineToArgv() */
#include <shlwapi.h> /* StrStrIA() */
#else
#include <ctime>
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
#include "log.h"
#include "mime-types.h"
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

struct tm* tr_gmtime_r(time_t const* timep, struct tm* result)
{
#if defined(HAVE_GMTIME_R)

    return gmtime_r(timep, result);

#elif defined(HAVE_GMTIME_S)

    return gmtime_s(result, timep) == 0 ? result : nullptr;

#else

    struct tm* p = gmtime(timep);
    if (p != nullptr)
    {
        *result = *p;
        return result;
    }

    return nullptr;

#endif
}

struct tm* tr_localtime_r(time_t const* timep, struct tm* result)
{
#if defined(HAVE_LOCALTIME_R)

    return localtime_r(timep, result);

#elif defined(HAVE_LOCALTIME_S)

    return localtime_s(result, timep) == 0 ? result : nullptr;

#else

    struct tm* p = localtime(timep);
    if (p != nullptr)
    {
        *result = *p;
        return result;
    }

    return nullptr;

#endif
}

int tr_gettimeofday(struct timeval* tv)
{
#ifdef _WIN32

#define DELTA_EPOCH_IN_MICROSECS 11644473600000000ULL

    FILETIME ft;
    uint64_t tmp = 0;

    if (tv == nullptr)
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

    return gettimeofday(tv, nullptr);

#endif
}

/***
****
***/

void* tr_malloc(size_t size)
{
    return size != 0 ? malloc(size) : nullptr;
}

void* tr_malloc0(size_t size)
{
    return size != 0 ? calloc(1, size) : nullptr;
}

void* tr_realloc(void* p, size_t size)
{
    void* result = size != 0 ? realloc(p, size) : nullptr;

    if (result == nullptr)
    {
        tr_free(p);
    }

    return result;
}

void tr_free(void* p)
{
    if (p != nullptr)
    {
        free(p);
    }
}

void tr_free_ptrv(void* const* p)
{
    if (p == nullptr)
    {
        return;
    }

    while (*p != nullptr)
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
    static auto buf = std::array<char, 512>{};

    char const* in = str;
    size_t pos = 0;

    for (; str && *str && pos + 1 < buf.size(); ++str)
    {
        buf[pos++] = *str;

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

    buf[pos] = '\0';

    return in && !strcmp(buf.data(), in) ? in : buf.data();
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
    tr_sys_path_info info;
    tr_sys_file_t fd;
    tr_error* my_error = nullptr;
    char const* const err_fmt = _("Couldn't read \"%1$s\": %2$s");

    /* try to stat the file */
    if (!tr_sys_path_get_info(path, 0, &info, &my_error))
    {
        tr_logAddDebug(err_fmt, path, my_error->message);
        tr_error_propagate(error, &my_error);
        return nullptr;
    }

    if (info.type != TR_SYS_PATH_IS_FILE)
    {
        tr_logAddError(err_fmt, path, _("Not a regular file"));
        tr_error_set_literal(error, TR_ERROR_EISDIR, _("Not a regular file"));
        return nullptr;
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
        return nullptr;
    }

    auto* buf = static_cast<uint8_t*>(tr_malloc(info.size + 1));

    if (!tr_sys_file_read(fd, buf, info.size, nullptr, &my_error))
    {
        tr_logAddError(err_fmt, path, my_error->message);
        tr_sys_file_close(fd, nullptr);
        free(buf);
        tr_error_propagate(error, &my_error);
        return nullptr;
    }

    tr_sys_file_close(fd, nullptr);
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

    while (element != nullptr)
    {
        bufLen += strlen(element) + 1;
        element = va_arg(vl, char const*);
    }

    pch = buf = tr_new(char, bufLen);
    va_end(vl);

    if (buf == nullptr)
    {
        return nullptr;
    }

    /* pass 2: build the string piece by piece */
    va_start(vl, first_element);
    element = first_element;

    while (element != nullptr)
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

    if (result_len != nullptr)
    {
        *result_len = n;
    }

    return ret;
}

char* tr_strdup(void const* in)
{
    return tr_strndup(in, in != nullptr ? strlen(static_cast<char const*>(in)) : 0);
}

char* tr_strndup(void const* in, size_t len)
{
    char* out = nullptr;

    if (len == TR_BAD_SIZE)
    {
        out = tr_strdup(in);
    }
    else if (in != nullptr)
    {
        out = static_cast<char*>(tr_malloc(len + 1));

        if (out != nullptr)
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

    return static_cast<char const*>(memmem(haystack, haystacklen, needle, needlelen));

#else

    if (needlelen == 0)
    {
        return haystack;
    }

    if (needlelen > haystacklen || haystack == nullptr || needle == nullptr)
    {
        return nullptr;
    }

    for (size_t i = 0; i <= haystacklen - needlelen; ++i)
    {
        if (memcmp(haystack + i, needle, needlelen) == 0)
        {
            return haystack + i;
        }
    }

    return nullptr;

#endif
}

extern "C"
{
    int DoMatch(char const* text, char const* p);
}

/* User-level routine. returns whether or not 'text' and 'p' matched */
bool tr_wildmat(char const* text, char const* p)
{
    return (p[0] == '*' && p[1] == '\0') || (DoMatch(text, p) == true);
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
    return evbuffer_free_to_str(buf, nullptr);
}

char const* tr_strerror(int i)
{
    char const* ret = strerror(i);

    if (ret == nullptr)
    {
        ret = "Unknown Error";
    }

    return ret;
}

int tr_strcmp0(char const* str1, char const* str2)
{
    if (str1 != nullptr && str2 != nullptr)
    {
        return strcmp(str1, str2);
    }

    if (str1 != nullptr)
    {
        return 1;
    }

    if (str2 != nullptr)
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

    if (*str == nullptr) /* no more tokens */
    {
        return nullptr;
    }

    token = *str;

    while (**str != '\0')
    {
        if (strchr(delims, **str) != nullptr)
        {
            **str = '\0';
            (*str)++;
            return token;
        }

        (*str)++;
    }

    /* there is not another token */
    *str = nullptr;

    return token;

#endif
}

char* tr_strstrip(char* str)
{
    if (str != nullptr)
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

    if (str == nullptr)
    {
        return false;
    }

    if (suffix == nullptr)
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
    nanosleep(&ts, nullptr);

#endif
}

/***
****
***/

int tr_snprintf(void* buf, size_t buflen, char const* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    int len = evutil_vsnprintf(static_cast<char*>(buf), buflen, fmt, args);
    va_end(args);
    return len;
}

/*
 * Copy src to string dst of size siz. At most siz-1 characters
 * will be copied. Always NUL terminates (unless siz == 0).
 * Returns strlen (src); if retval >= siz, truncation occurred.
 */
size_t tr_strlcpy(void* vdst, void const* vsrc, size_t siz)
{
    auto* dst = static_cast<char*>(vdst);
    auto const* const src = static_cast<char const*>(vsrc);

    TR_ASSERT(dst != nullptr);
    TR_ASSERT(src != nullptr);

#ifdef HAVE_STRLCPY

    return strlcpy(dst, src, siz);

#else

    auto* d = dst;
    auto* s = src;
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

void tr_binary_to_hex(void const* vinput, void* voutput, size_t byte_length)
{
    static char const hex[] = "0123456789abcdef";

    auto const* input = static_cast<uint8_t const*>(vinput);
    auto* output = static_cast<char*>(voutput);

    /* go from back to front to allow for in-place conversion */
    input += byte_length;
    output += byte_length * 2;

    *output = '\0';

    while (byte_length-- > 0)
    {
        unsigned int const val = *(--input);
        *(--output) = hex[val & 0xf];
        *(--output) = hex[val >> 4];
    }
}

void tr_hex_to_binary(void const* vinput, void* voutput, size_t byte_length)
{
    static char const hex[] = "0123456789abcdef";

    auto const* input = static_cast<uint8_t const*>(vinput);
    auto* output = static_cast<uint8_t*>(voutput);

    for (size_t i = 0; i < byte_length; ++i)
    {
        int const hi = strchr(hex, tolower(*input++)) - hex;
        int const lo = strchr(hex, tolower(*input++)) - hex;
        *output++ = (uint8_t)((hi << 4) | lo);
    }
}

/***
****
***/

static bool isValidURLChars(char const* url, size_t url_len)
{
    static char const rfc2396_valid_chars
        [] = "abcdefghijklmnopqrstuvwxyz" /* lowalpha */
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ" /* upalpha */
             "0123456789" /* digit */
             "-_.!~*'()" /* mark */
             ";/?:@&=+$," /* reserved */
             "<>#%<\"" /* delims */
             "{}|\\^[]`"; /* unwise */

    if (url == nullptr)
    {
        return false;
    }

    for (char const *c = url, *end = url + url_len; c < end && *c != '\0'; ++c)
    {
        if (memchr(rfc2396_valid_chars, *c, sizeof(rfc2396_valid_chars) - 1) == nullptr)
        {
            return false;
        }
    }

    return true;
}

bool tr_urlIsValidTracker(char const* url)
{
    if (url == nullptr)
    {
        return false;
    }

    size_t const url_len = strlen(url);

    return isValidURLChars(url, url_len) && tr_urlParse(url, url_len, nullptr, nullptr, nullptr, nullptr) &&
        (memcmp(url, "http://", 7) == 0 || memcmp(url, "https://", 8) == 0 || memcmp(url, "udp://", 6) == 0);
}

bool tr_urlIsValid(char const* url, size_t url_len)
{
    if (url == nullptr)
    {
        return false;
    }

    if (url_len == TR_BAD_SIZE)
    {
        url_len = strlen(url);
    }

    return isValidURLChars(url, url_len) && tr_urlParse(url, url_len, nullptr, nullptr, nullptr, nullptr) &&
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

    static struct known_scheme const known_schemes[] = {
        { "udp", 80 }, //
        { "ftp", 21 }, //
        { "sftp", 22 }, //
        { "http", 80 }, //
        { "https", 443 }, //
        { nullptr, 0 }, //
    };

    for (struct known_scheme const* s = known_schemes; s->name != nullptr; ++s)
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

    if (scheme_end == nullptr)
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
    auto const* authority_end = static_cast<char const*>(memchr(authority, '/', url_len));

    if (authority_end == nullptr)
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

    auto const* host_end = static_cast<char const*>(memchr(authority, ':', authority_len));

    size_t const host_len = host_end != nullptr ? (size_t)(host_end - authority) : authority_len;

    if (host_len == 0)
    {
        return false;
    }

    size_t const port_len = host_end != nullptr ? authority_end - host_end - 1 : 0;

    if (setme_scheme != nullptr)
    {
        *setme_scheme = tr_strndup(scheme, scheme_len);
    }

    if (setme_host != nullptr)
    {
        *setme_host = tr_strndup(authority, host_len);
    }

    if (setme_port != nullptr)
    {
        *setme_port = port_len > 0 ? parse_port(host_end + 1, port_len) : get_port_for_scheme(scheme, scheme_len);
    }

    if (setme_path != nullptr)
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

void tr_removeElementFromArray(void* array, size_t index_to_remove, size_t sizeof_element, size_t nmemb)
{
    auto* a = static_cast<char*>(array);

    memmove(
        a + sizeof_element * index_to_remove,
        a + sizeof_element * (index_to_remove + 1),
        sizeof_element * (--nmemb - index_to_remove));
}

int tr_lowerBound(
    void const* key,
    void const* base,
    size_t nmemb,
    size_t size,
    tr_voidptr_compare_func compar,
    bool* exact_match)
{
    size_t first = 0;
    auto const* cbase = static_cast<char const*>(base);
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
    return evbuffer_free_to_str(buf, nullptr);
}

static char* to_utf8(char const* in, size_t inlen)
{
    char* ret = nullptr;

#ifdef HAVE_ICONV

    char const* encodings[] = { "CURRENT", "ISO-8859-15" };
    size_t const buflen = inlen * 4 + 10;
    char* out = tr_new(char, buflen);

    for (size_t i = 0; ret == nullptr && i < TR_N_ELEMENTS(encodings); ++i)
    {
#ifdef ICONV_SECOND_ARGUMENT_IS_CONST
        auto const* inbuf = in;
#else
        auto* inbuf = const_cast<char*>(in);
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

    if (ret == nullptr)
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

    TR_ASSERT(tr_utf8_validate(ret, TR_BAD_SIZE, nullptr));
    return ret;
}

#ifdef _WIN32

char* tr_win32_native_to_utf8(wchar_t const* text, int text_size)
{
    return tr_win32_native_to_utf8_ex(text, text_size, 0, 0, nullptr);
}

char* tr_win32_native_to_utf8_ex(
    wchar_t const* text,
    int text_size,
    int extra_chars_before,
    int extra_chars_after,
    int* real_result_size)
{
    char* ret = nullptr;
    int size;

    if (text_size == -1)
    {
        text_size = wcslen(text);
    }

    size = WideCharToMultiByte(CP_UTF8, 0, text, text_size, nullptr, 0, nullptr, nullptr);

    if (size == 0)
    {
        goto fail;
    }

    ret = tr_new(char, size + extra_chars_before + extra_chars_after + 1);
    size = WideCharToMultiByte(CP_UTF8, 0, text, text_size, ret + extra_chars_before, size, nullptr, nullptr);

    if (size == 0)
    {
        goto fail;
    }

    ret[size + extra_chars_before + extra_chars_after] = '\0';

    if (real_result_size != nullptr)
    {
        *real_result_size = size;
    }

    return ret;

fail:
    tr_free(ret);

    return nullptr;
}

wchar_t* tr_win32_utf8_to_native(char const* text, int text_size)
{
    return tr_win32_utf8_to_native_ex(text, text_size, 0, 0, nullptr);
}

wchar_t* tr_win32_utf8_to_native_ex(
    char const* text,
    int text_size,
    int extra_chars_before,
    int extra_chars_after,
    int* real_result_size)
{
    wchar_t* ret = nullptr;
    int size;

    if (text_size == -1)
    {
        text_size = strlen(text);
    }

    size = MultiByteToWideChar(CP_UTF8, 0, text, text_size, nullptr, 0);

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

    if (real_result_size != nullptr)
    {
        *real_result_size = size;
    }

    return ret;

fail:
    tr_free(ret);

    return nullptr;
}

char* tr_win32_format_message(uint32_t code)
{
    wchar_t* wide_text = nullptr;
    DWORD wide_size;
    char* text = nullptr;
    size_t text_size;

    wide_size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        (LPWSTR)&wide_text,
        0,
        nullptr);

    if (wide_size == 0)
    {
        return tr_strdup_printf("Unknown error (0x%08x)", code);
    }

    if (wide_size != 0 && wide_text != nullptr)
    {
        text = tr_win32_native_to_utf8(wide_text, wide_size);
    }

    LocalFree(wide_text);

    if (text != nullptr)
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

    if (my_wide_argv == nullptr)
    {
        return;
    }

    TR_ASSERT(*argc == my_argc);

    char** my_argv = tr_new(char*, my_argc + 1);
    int processed_argc = 0;

    for (int i = 0; i < my_argc; ++i, ++processed_argc)
    {
        my_argv[i] = tr_win32_native_to_utf8(my_wide_argv[i], -1);

        if (my_argv[i] == nullptr)
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
        my_argv[my_argc] = nullptr;

        *argc = my_argc;
        *argv = my_argv;

        /* TODO: Add atexit handler to cleanup? */
    }

    LocalFree(my_wide_argv);
}

int tr_main_win32(int argc, char** argv, int (*real_main)(int, char**))
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
static bool parseNumberSection(char const* str, char const* const end, number_range& range)
{
    bool success;
    auto const error = errno;

#if defined(HAVE_CHARCONV)
    auto result = std::from_chars(str, end, range.low);
    success = result.ec == std::errc{};
    if (success)
    {
        range.high = range.low;
        if (result.ptr != end && *result.ptr == '-')
        {
            result = std::from_chars(result.ptr + 1, end, range.high);
            success = result.ec == std::errc{};
        }
    }
#else
    try
    {
        auto tmp = std::string(str, end);
        auto pos = size_t{};
        range.low = range.high = std::stoi(tmp, &pos);
        if (pos != std::size(tmp) && tmp[pos] == '-')
        {
            tmp.erase(0, pos + 1);
            range.high = std::stoi(tmp, &pos);
        }
        success = true;
    }
    catch (std::exception&)
    {
        success = false;
    }
#endif

    errno = error;
    return success;
}

/**
 * Given a string like "1-4" or "1-4,6,9,14-51", this allocates and returns an
 * array of setmeCount ints of all the values in the array.
 * For example, "5-8" will return [ 5, 6, 7, 8 ] and setmeCount will be 4.
 * It's the caller's responsibility to call tr_free () on the returned array.
 * If a fragment of the string can't be parsed, NULL is returned.
 */
std::vector<int> tr_parseNumberRange(char const* str, size_t len) // TODO: string_view
{
    auto values = std::set<int>{};

    auto const* const end = str + (len != TR_BAD_SIZE ? len : strlen(str));
    for (auto const* walk = str; walk < end;)
    {
        auto delim = std::find(walk, end, ',');
        auto range = number_range{};
        if (!parseNumberSection(walk, delim, range))
        {
            break;
        }

        for (auto i = range.low; i <= range.high; ++i)
        {
            values.insert(i);
        }

        walk = delim + 1;
    }

    return { std::begin(values), std::end(values) };
}

/***
****
***/

double tr_truncd(double x, int precision)
{
    char* pt;
    char buf[128];
    tr_snprintf(buf, sizeof(buf), "%.*f", TR_ARG_TUPLE(DBL_DIG, x));

    if ((pt = strstr(buf, localeconv()->decimal_point)) != nullptr)
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
    tr_sys_path_info info;

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
        bool const i = newdir != nullptr && tr_sys_dir_create(newdir, TR_SYS_DIR_CREATE_PARENTS, 0777, error);
        tr_free(newdir);

        if (!i)
        {
            tr_error_prefix(error, "Unable to create directory for new file: ");
            return false;
        }
    }

    /* they might be on the same filesystem... */
    if (tr_sys_path_rename(oldpath, newpath, nullptr))
    {
        return true;
    }

    /* Otherwise, copy the file. */
    if (!tr_sys_path_copy(oldpath, newpath, error))
    {
        tr_error_prefix(error, "Unable to copy: ");
        return false;
    }

    {
        tr_error* my_error = nullptr;

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
    } u;
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
    } u;
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
    size_t value;
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

static void formatter_init(
    struct formatter_units* units,
    size_t kilo,
    char const* kb,
    char const* mb,
    char const* gb,
    char const* tb)
{
    size_t value;

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

static char* formatter_get_size_str(struct formatter_units const* u, char* buf, size_t bytes, size_t buflen)
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

    tr_snprintf(buf, buflen, "%.*f %s", TR_ARG_TUPLE(precision, value), units);
    return buf;
}

static struct formatter_units size_units;

void tr_formatter_size_init(size_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    formatter_init(&size_units, kilo, kb, mb, gb, tb);
}

char* tr_formatter_size_B(char* buf, size_t bytes, size_t buflen)
{
    return formatter_get_size_str(&size_units, buf, bytes, buflen);
}

static struct formatter_units speed_units;

size_t tr_speed_K = 0;

void tr_formatter_speed_init(size_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
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

size_t tr_mem_K = 0;

void tr_formatter_mem_init(size_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    tr_mem_K = kilo;
    formatter_init(&mem_units, kilo, kb, mb, gb, tb);
}

char* tr_formatter_mem_B(char* buf, size_t bytes_per_second, size_t buflen)
{
    return formatter_get_size_str(&mem_units, buf, bytes_per_second, buflen);
}

void tr_formatter_get_units(void* vdict)
{
    tr_variant* l;
    auto* dict = static_cast<tr_variant*>(vdict);

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
    TR_ASSERT(key != nullptr);

#ifdef _WIN32
    return GetEnvironmentVariableA(key, nullptr, 0) != 0;
#else
    return getenv(key) != nullptr;
#endif
}

int tr_env_get_int(char const* key, int default_value)
{
    TR_ASSERT(key != nullptr);

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
    TR_ASSERT(key != nullptr);

#ifdef _WIN32

    wchar_t* wide_key = tr_win32_utf8_to_native(key, -1);
    char* value = nullptr;

    if (wide_key != nullptr)
    {
        DWORD const size = GetEnvironmentVariableW(wide_key, nullptr, 0);

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

    if (value == nullptr && default_value != nullptr)
    {
        value = tr_strdup(default_value);
    }

    return value;

#else

    char const* value = getenv(key);

    if (value == nullptr)
    {
        value = default_value;
    }

    return value != nullptr ? tr_strdup(value) : nullptr;

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

/// mime-type

char const* tr_get_mime_type_for_filename(std::string_view filename)
{
    auto constexpr compare = [](mime_type_suffix const& entry, auto const& suffix)
    {
        return entry.suffix < suffix;
    };

    auto const pos = filename.rfind('.');
    if (pos != filename.npos)
    {
        // make a lowercase copy of the file suffix
        filename.remove_prefix(pos + 1);
        auto suffix_lc = std::string{};
        std::transform(
            std::begin(filename),
            std::end(filename),
            std::back_inserter(suffix_lc),
            [](auto c) { return std::tolower(c); });

        // find it
        auto const it = std::lower_bound(std::begin(mime_type_suffixes), std::end(mime_type_suffixes), suffix_lc, compare);
        if (it != std::end(mime_type_suffixes) && suffix_lc == it->suffix)
        {
            return std::data(it->mime_type);
        }
    }

    return nullptr;
}
