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

#include <utf8.h>
#include <event2/buffer.h>
#include <event2/event.h>

#include "transmission.h"
#include "error.h"
#include "error-types.h"
#include "file.h"
#include "log.h"
#include "mime-types.h"
#include "net.h"
#include "platform-quota.h" /* tr_device_info_create(), tr_device_info_get_disk_space(), tr_device_info_free() */
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"
#include "version.h"

using namespace std::literals;

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

    for (; str && *str && pos + 1 < std::size(buf); ++str)
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

// TODO: return a std::vector<>
uint8_t* tr_loadFile(char const* path, size_t* size, tr_error** error)
{
    char const* const err_fmt = _("Couldn't read \"%1$s\": %2$s");

    /* try to stat the file */
    auto info = tr_sys_path_info{};
    tr_error* my_error = nullptr;
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
    if constexpr (sizeof(info.size) > sizeof(*size))
    {
        TR_ASSERT(info.size <= SIZE_MAX);
    }

    /* Load the torrent file into our buffer */
    tr_sys_file_t const fd = tr_sys_file_open(path, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &my_error);
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

bool tr_loadFile(std::vector<char>& setme, char const* path, tr_error** error)
{
    char const* const err_fmt = _("Couldn't read \"%1$s\": %2$s");

    /* try to stat the file */
    auto info = tr_sys_path_info{};
    tr_error* my_error = nullptr;
    if (!tr_sys_path_get_info(path, 0, &info, &my_error))
    {
        tr_logAddDebug(err_fmt, path, my_error->message);
        tr_error_propagate(error, &my_error);
        return false;
    }

    if (info.type != TR_SYS_PATH_IS_FILE)
    {
        tr_logAddError(err_fmt, path, _("Not a regular file"));
        tr_error_set_literal(error, TR_ERROR_EISDIR, _("Not a regular file"));
        return false;
    }

    /* Load the torrent file into our buffer */
    tr_sys_file_t const fd = tr_sys_file_open(path, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &my_error);
    if (fd == TR_BAD_SYS_FILE)
    {
        tr_logAddError(err_fmt, path, my_error->message);
        tr_error_propagate(error, &my_error);
        return false;
    }

    setme.resize(info.size);
    if (!tr_sys_file_read(fd, std::data(setme), info.size, nullptr, &my_error))
    {
        tr_logAddError(err_fmt, path, my_error->message);
        tr_sys_file_close(fd, nullptr);
        tr_error_propagate(error, &my_error);
        return false;
    }

    tr_sys_file_close(fd, nullptr);
    return true;
}

char* tr_buildPath(char const* first_element, ...)
{

    /* pass 1: allocate enough space for the string */
    va_list vl;
    va_start(vl, first_element);
    auto bufLen = size_t{};
    for (char const* element = first_element; element != nullptr;)
    {
        bufLen += strlen(element) + 1;
        element = va_arg(vl, char const*);
    }
    va_end(vl);
    char* const buf = tr_new(char, bufLen);
    if (buf == nullptr)
    {
        return nullptr;
    }

    /* pass 2: build the string piece by piece */
    char* pch = buf;
    va_start(vl, first_element);
    for (char const* element = first_element; element != nullptr;)
    {
        size_t const elementLen = strlen(element);
        pch = std::copy_n(element, elementLen, pch);
        *pch++ = TR_PATH_DELIMITER;
        element = va_arg(vl, char const*);
    }
    va_end(vl);

    // if nonempty, eat the unwanted trailing slash
    if (pch != buf)
    {
        --pch;
    }

    // zero-terminate the string
    *pch++ = '\0';

    /* sanity checks & return */
    TR_ASSERT(pch - buf == (ptrdiff_t)bufLen);
    return buf;
}

tr_disk_space tr_dirSpace(std::string_view dir)
{
    if (std::empty(dir))
    {
        errno = EINVAL;
        return { -1, -1 };
    }

    return tr_device_info_get_disk_space(tr_device_info_create(dir));
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

char* tr_strvDup(std::string_view in)
{
    auto const n = std::size(in);
    auto* const ret = tr_new(char, n + 1);
    std::copy(std::begin(in), std::end(in), ret);
    ret[n] = '\0';
    return ret;
}

char* tr_strndup(void const* vin, size_t len)
{
    auto const* const in = static_cast<char const*>(vin);
    return in == nullptr ? nullptr : tr_strvDup({ in, len == TR_BAD_SIZE ? strlen(in) : len });
}

char* tr_strdup(void const* in)
{
    return tr_strndup(in, TR_BAD_SIZE);
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
    va_start(ap, fmt);
    char* const ret = tr_strdup_vprintf(fmt, ap);
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

std::string_view tr_strvStrip(std::string_view str)
{
    auto constexpr test = [](auto ch)
    {
        return isspace(ch);
    };

    auto const it = std::find_if_not(std::begin(str), std::end(str), test);
    str.remove_prefix(std::distance(std::begin(str), it));

    auto const rit = std::find_if_not(std::rbegin(str), std::rend(str), test);
    str.remove_suffix(std::distance(std::rbegin(str), rit));

    return str;
}

bool tr_str_has_suffix(char const* str, char const* suffix)
{
    if (str == nullptr)
    {
        return false;
    }

    if (suffix == nullptr)
    {
        return true;
    }

    auto const str_len = strlen(str);
    auto const suffix_len = strlen(suffix);

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
    if (denominator > 0)
    {
        return numerator / (double)denominator;
    }

    if (numerator > 0)
    {
        return TR_RATIO_INF;
    }

    return TR_RATIO_NA;
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

/***
****
***/

bool tr_utf8_validate(std::string_view sv, char const** good_end)
{
    auto const* begin = std::data(sv);
    auto const* const end = begin + std::size(sv);
    auto const* walk = begin;
    auto all_good = false;

    try
    {
        while (walk < end)
        {
            utf8::next(walk, end);
        }

        all_good = true;
    }
    catch (utf8::exception const&)
    {
        all_good = false;
    }

    if (good_end != nullptr)
    {
        *good_end = walk;
    }

    return all_good;
}

static char* strip_non_utf8(std::string_view sv)
{
    char* ret = tr_new(char, std::size(sv) + 1);
    if (ret != nullptr)
    {
        auto const it = utf8::unchecked::replace_invalid(std::data(sv), std::data(sv) + std::size(sv), ret, '?');
        *it = '\0';
    }
    return ret;
}

static char* to_utf8(std::string_view sv)
{
#ifdef HAVE_ICONV
    size_t const buflen = std::size(sv) * 4 + 10;
    char* out = tr_new(char, buflen);

    auto constexpr Encodings = std::array<char const*, 2>{ "CURRENT", "ISO-8859-15" };
    for (auto const* test_encoding : Encodings)
    {
        iconv_t cd = iconv_open("UTF-8", test_encoding);
        if (cd == (iconv_t)-1) // NOLINT(performance-no-int-to-ptr)
        {
            continue;
        }

#ifdef ICONV_SECOND_ARGUMENT_IS_CONST
        auto const* inbuf = std::data(sv);
#else
        auto* inbuf = const_cast<char*>(std::data(sv));
#endif
        char* outbuf = out;
        size_t inbytesleft = std::size(sv);
        size_t outbytesleft = buflen;
        auto const rv = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
        iconv_close(cd);
        if (rv != size_t(-1))
        {
            char* const ret = tr_strndup(out, buflen - outbytesleft);
            tr_free(out);
            return ret;
        }
    }

    tr_free(out);

#endif

    return strip_non_utf8(sv);
}

char* tr_utf8clean(std::string_view sv)
{
    char* const ret = tr_utf8_validate(sv, nullptr) ? tr_strvDup(sv) : to_utf8(sv);
    TR_ASSERT(tr_utf8_validate(ret, nullptr));
    return ret;
}

std::string tr_strvUtf8Clean(std::string_view sv)
{
    if (tr_utf8_validate(sv, nullptr))
    {
        return std::string{ sv };
    }

    auto* const tmp = to_utf8(sv);
    auto ret = std::string{ tmp ? tmp : "" };
    tr_free(tmp);
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
static bool parseNumberSection(std::string_view str, number_range& range)
{
    auto const error = errno;
    auto success = bool{};

#if defined(HAVE_CHARCONV)
    // wants char*, so string_view::iterator don't work. make our own begin/end
    auto const* const begin_ch = std::data(str);
    auto const* const end_ch = begin_ch + std::size(str);
    auto result = std::from_chars(begin_ch, end_ch, range.low);
    success = result.ec == std::errc{};
    if (success)
    {
        range.high = range.low;
        if (result.ptr < end_ch && *result.ptr == '-')
        {
            result = std::from_chars(result.ptr + 1, end_ch, range.high);
            success = result.ec == std::errc{};
        }
    }
#else
    try
    {
        auto tmp = std::string(str);
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
 * If a fragment of the string can't be parsed, nullptr is returned.
 */
std::vector<int> tr_parseNumberRange(std::string_view str)
{
    auto values = std::set<int>{};
    auto token = std::string_view{};
    auto range = number_range{};
    while (tr_strvSep(&str, &token, ',') && parseNumberSection(token, range))
    {
        for (auto i = range.low; i <= range.high; ++i)
        {
            values.insert(i);
        }
    }

    return { std::begin(values), std::end(values) };
}

/***
****
***/

double tr_truncd(double x, int precision)
{
    char buf[128];
    tr_snprintf(buf, sizeof(buf), "%.*f", TR_ARG_TUPLE(DBL_DIG, x));

    char* const pt = strstr(buf, localeconv()->decimal_point);
    if (pt != nullptr)
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
    uint64_t value;
};

using formatter_units = std::array<formatter_unit, 4>;
/*
struct formatter_units
{
    struct formatter_unit units[4];
};
*/

enum
{
    TR_FMT_KB,
    TR_FMT_MB,
    TR_FMT_GB,
    TR_FMT_TB
};

static void formatter_init(
    formatter_units& units,
    uint64_t kilo,
    char const* kb,
    char const* mb,
    char const* gb,
    char const* tb)
{
    uint64_t value = kilo;
    units[TR_FMT_KB].name = tr_strdup(kb);
    units[TR_FMT_KB].value = value;

    value *= kilo;
    units[TR_FMT_MB].name = tr_strdup(mb);
    units[TR_FMT_MB].value = value;

    value *= kilo;
    units[TR_FMT_GB].name = tr_strdup(gb);
    units[TR_FMT_GB].value = value;

    value *= kilo;
    units[TR_FMT_TB].name = tr_strdup(tb);
    units[TR_FMT_TB].value = value;
}

static char* formatter_get_size_str(formatter_units const& u, char* buf, uint64_t bytes, size_t buflen)
{
    formatter_unit const* unit = nullptr;

    if (bytes < u[1].value)
    {
        unit = &u[0];
    }
    else if (bytes < u[2].value)
    {
        unit = &u[1];
    }
    else if (bytes < u[3].value)
    {
        unit = &u[2];
    }
    else
    {
        unit = &u[3];
    }

    double value = (double)bytes / unit->value;
    char const* units = unit->name;

    auto precision = int{};
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

static formatter_units size_units;

void tr_formatter_size_init(uint64_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    formatter_init(size_units, kilo, kb, mb, gb, tb);
}

char* tr_formatter_size_B(char* buf, uint64_t bytes, size_t buflen)
{
    return formatter_get_size_str(size_units, buf, bytes, buflen);
}

static formatter_units speed_units;

size_t tr_speed_K = 0;

void tr_formatter_speed_init(size_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    tr_speed_K = kilo;
    formatter_init(speed_units, kilo, kb, mb, gb, tb);
}

char* tr_formatter_speed_KBps(char* buf, double KBps, size_t buflen)
{
    double const K = speed_units[TR_FMT_KB].value;
    double speed = KBps;

    if (speed <= 999.95) /* 0.0 KB to 999.9 KB */
    {
        tr_snprintf(buf, buflen, "%d %s", (int)speed, speed_units[TR_FMT_KB].name);
    }
    else
    {
        speed /= K;

        if (speed <= 99.995) /* 0.98 MB to 99.99 MB */
        {
            tr_snprintf(buf, buflen, "%.2f %s", speed, speed_units[TR_FMT_MB].name);
        }
        else if (speed <= 999.95) /* 100.0 MB to 999.9 MB */
        {
            tr_snprintf(buf, buflen, "%.1f %s", speed, speed_units[TR_FMT_MB].name);
        }
        else
        {
            tr_snprintf(buf, buflen, "%.1f %s", speed / K, speed_units[TR_FMT_GB].name);
        }
    }

    return buf;
}

static formatter_units mem_units;

size_t tr_mem_K = 0;

void tr_formatter_mem_init(size_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    tr_mem_K = kilo;
    formatter_init(mem_units, kilo, kb, mb, gb, tb);
}

char* tr_formatter_mem_B(char* buf, size_t bytes_per_second, size_t buflen)
{
    return formatter_get_size_str(mem_units, buf, bytes_per_second, buflen);
}

void tr_formatter_get_units(void* vdict)
{
    auto* dict = static_cast<tr_variant*>(vdict);

    tr_variantDictReserve(dict, 6);

    tr_variantDictAddInt(dict, TR_KEY_memory_bytes, mem_units[TR_FMT_KB].value);
    tr_variant* l = tr_variantDictAddList(dict, TR_KEY_memory_units, std::size(mem_units));
    for (auto const& unit : mem_units)
    {
        tr_variantListAddStr(l, unit.name);
    }

    tr_variantDictAddInt(dict, TR_KEY_size_bytes, size_units[TR_FMT_KB].value);
    l = tr_variantDictAddList(dict, TR_KEY_size_units, std::size(size_units));
    for (auto const& unit : size_units)
    {
        tr_variantListAddStr(l, unit.name);
    }

    tr_variantDictAddInt(dict, TR_KEY_speed_bytes, speed_units[TR_FMT_KB].value);
    l = tr_variantDictAddList(dict, TR_KEY_speed_units, std::size(speed_units));
    for (auto const& unit : speed_units)
    {
        tr_variantListAddStr(l, unit.name);
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

std::string_view tr_get_mime_type_for_filename(std::string_view filename)
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
            return it->mime_type;
        }
    }

    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
    // application/octet-stream is the default value.
    // An unknown file type should use this type.
    auto constexpr Fallback = "application/octet-stream"sv;
    return Fallback;
}
