// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // for std::sort, std::transform
#include <array> // std::array
#include <cerrno>
#include <cfloat> // DBL_DIG
#include <chrono>
#include <clocale> // localeconv()
#include <cstdint> // SIZE_MAX
#include <cstdlib> // getenv()
#include <cstring> /* strerror() */
#include <ctime> // nanosleep()
#include <iterator> // for std::back_inserter
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h> /* Sleep(), GetEnvironmentVariable() */

#include <shellapi.h> /* CommandLineToArgv() */
#include <ws2tcpip.h> /* WSAStartup() */
#endif

#ifndef _WIN32
#include <sys/stat.h> // mode_t
#endif

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#define UTF_CPP_CPLUSPLUS 201703L
#include <utf8.h>

#include <fmt/format.h>

#include <fast_float/fast_float.h>

#include "transmission.h"

#include "error-types.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "mime-types.h"
#include "net.h" // ntohl()
#include "platform-quota.h" /* tr_device_info_create(), tr_device_info_get_disk_space(), tr_device_info_free() */
#include "tr-assert.h"
#include "tr-strbuf.h"
#include "utils.h"
#include "variant.h"

using namespace std::literals;

time_t __tr_current_time = 0;

/**
***
**/

bool tr_loadFile(std::string_view filename, std::vector<char>& contents, tr_error** error)
{
    auto const szfilename = tr_pathbuf{ filename };

    /* try to stat the file */
    tr_error* my_error = nullptr;
    auto const info = tr_sys_path_get_info(szfilename, 0, &my_error);
    if (my_error != nullptr)
    {
        tr_logAddError(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", my_error->message),
            fmt::arg("error_code", my_error->code)));
        tr_error_propagate(error, &my_error);
        return false;
    }

    if (!info || !info->isFile())
    {
        tr_logAddError(fmt::format(_("Couldn't read '{path}': Not a regular file"), fmt::arg("path", filename)));
        tr_error_set(error, TR_ERROR_EISDIR, "Not a regular file"sv);
        return false;
    }

    /* Load the torrent file into our buffer */
    auto const fd = tr_sys_file_open(szfilename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &my_error);
    if (fd == TR_BAD_SYS_FILE)
    {
        tr_logAddError(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", my_error->message),
            fmt::arg("error_code", my_error->code)));
        tr_error_propagate(error, &my_error);
        return false;
    }

    contents.resize(info->size);
    if (!tr_sys_file_read(fd, std::data(contents), info->size, nullptr, &my_error))
    {
        tr_logAddError(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", my_error->message),
            fmt::arg("error_code", my_error->code)));
        tr_sys_file_close(fd);
        tr_error_propagate(error, &my_error);
        return false;
    }

    tr_sys_file_close(fd);
    return true;
}

bool tr_saveFile(std::string_view filename, std::string_view contents, tr_error** error)
{
    // follow symlinks to find the "real" file, to make sure the temporary
    // we build with tr_sys_file_open_temp() is created on the right partition
    if (auto const realname = tr_sys_path_resolve(filename); !std::empty(realname) && realname != filename)
    {
        return tr_saveFile(realname, contents, error);
    }

    // Write it to a temp file first.
    // This is a safeguard against edge cases, e.g. disk full, crash while writing, etc.
    auto tmp = tr_pathbuf{ filename, ".tmp.XXXXXX"sv };
    auto const fd = tr_sys_file_open_temp(std::data(tmp), error);
    if (fd == TR_BAD_SYS_FILE)
    {
        return false;
    }

    // Save the contents. This might take >1 pass.
    auto ok = bool{ true };
    while (!std::empty(contents))
    {
        auto n_written = uint64_t{};
        if (!tr_sys_file_write(fd, std::data(contents), std::size(contents), &n_written, error))
        {
            ok = false;
            break;
        }
        contents.remove_prefix(n_written);
    }

    // If we saved it to disk successfully, move it from '.tmp' to the correct filename
    auto const szfilename = tr_pathbuf{ filename };
    if (!tr_sys_file_close(fd, error) || !ok || !tr_sys_path_rename(tmp, szfilename, error))
    {
        return false;
    }

    tr_logAddTrace(fmt::format("Saved '{}'", filename));
    return true;
}

tr_disk_space tr_dirSpace(std::string_view directory)
{
    if (std::empty(directory))
    {
        errno = EINVAL;
        return { -1, -1 };
    }

    return tr_device_info_get_disk_space(tr_device_info_create(directory));
}

/****
*****
****/

size_t tr_strvToBuf(std::string_view src, char* buf, size_t buflen)
{
    size_t const len = std::size(src);

    if (buflen >= len)
    {
        auto const out = std::copy(std::begin(src), std::end(src), buf);

        if (buflen > len)
        {
            *out = '\0';
        }
    }

    return len;
}

extern "C"
{
    int DoMatch(char const* text, char const* p);
}

/* User-level routine. returns whether or not 'text' and 'p' matched */
bool tr_wildmat(std::string_view text, std::string_view pattern)
{
    // TODO(ckerr): replace wildmat with base/strings/pattern.cc
    // wildmat wants these to be zero-terminated.
    return pattern == "*"sv || DoMatch(std::string{ text }.c_str(), std::string{ pattern }.c_str()) != 0;
}

char const* tr_strerror(int errnum)
{
    if (char const* const ret = strerror(errnum); ret != nullptr)
    {
        return ret;
    }

    return "Unknown Error";
}

/****
*****
****/

std::string_view tr_strvStrip(std::string_view str)
{
    auto constexpr test = [](auto ch)
    {
        return isspace(static_cast<unsigned char>(ch));
    };

    auto const it = std::find_if_not(std::begin(str), std::end(str), test);
    str.remove_prefix(std::distance(std::begin(str), it));

    auto const rit = std::find_if_not(std::rbegin(str), std::rend(str), test);
    str.remove_suffix(std::distance(std::rbegin(str), rit));

    return str;
}

/****
*****
****/

uint64_t tr_time_msec()
{
    return std::chrono::system_clock::now().time_since_epoch() / 1ms;
}

void tr_wait_msec(long int delay_milliseconds)
{
#ifdef _WIN32

    Sleep((DWORD)delay_milliseconds);

#else

    struct timespec ts;
    ts.tv_sec = delay_milliseconds / 1000;
    ts.tv_nsec = (delay_milliseconds % 1000) * 1000000;
    nanosleep(&ts, nullptr);

#endif
}

/***
****
***/

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

    auto const res = fmt::format_to_n(dst, siz - 1, FMT_STRING("{:s}"), src);
    *res.out = '\0';
    return res.size;
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

namespace
{
namespace tr_strvUtf8Clean_impl
{

bool validateUtf8(std::string_view sv, char const** good_end)
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

std::string strip_non_utf8(std::string_view sv)
{
    auto out = std::string{};
    utf8::unchecked::replace_invalid(std::data(sv), std::data(sv) + std::size(sv), std::back_inserter(out), '?');
    return out;
}

std::string to_utf8(std::string_view sv)
{
#ifdef HAVE_ICONV
    size_t const buflen = std::size(sv) * 4 + 10;
    auto buf = std::vector<char>{};
    buf.resize(buflen);

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
        size_t inbytesleft = std::size(sv);
        char* out = std::data(buf);
        size_t outbytesleft = std::size(buf);
        auto const rv = iconv(cd, &inbuf, &inbytesleft, &out, &outbytesleft);
        iconv_close(cd);
        if (rv != size_t(-1))
        {
            return std::string{ std::data(buf), buflen - outbytesleft };
        }
    }

#endif

    return strip_non_utf8(sv);
}

} // namespace tr_strvUtf8Clean_impl
} // namespace

std::string tr_strvUtf8Clean(std::string_view cleanme)
{
    using namespace tr_strvUtf8Clean_impl;

    if (validateUtf8(cleanme, nullptr))
    {
        return std::string{ cleanme };
    }

    return to_utf8(cleanme);
}

#ifdef _WIN32

std::string tr_win32_native_to_utf8(std::wstring_view in)
{
    auto out = std::string{};
    out.resize(WideCharToMultiByte(CP_UTF8, 0, std::data(in), std::size(in), nullptr, 0, nullptr, nullptr));
    [[maybe_unused]] auto
        len = WideCharToMultiByte(CP_UTF8, 0, std::data(in), std::size(in), std::data(out), std::size(out), nullptr, nullptr);
    TR_ASSERT(len == std::size(out));
    return out;
}

std::wstring tr_win32_utf8_to_native(std::string_view in)
{
    auto out = std::wstring{};
    out.resize(MultiByteToWideChar(CP_UTF8, 0, std::data(in), std::size(in), nullptr, 0));
    [[maybe_unused]] auto len = MultiByteToWideChar(CP_UTF8, 0, std::data(in), std::size(in), std::data(out), std::size(out));
    TR_ASSERT(len == std::size(out));
    return out;
}

std::string tr_win32_format_message(uint32_t code)
{
    wchar_t* wide_text = nullptr;
    auto const wide_size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        (LPWSTR)&wide_text,
        0,
        nullptr);

    if (wide_size == 0)
    {
        return fmt::format(FMT_STRING("Unknown error ({:#08x})"), code);
    }

    auto text = std::string{};

    if (wide_size != 0 && wide_text != nullptr)
    {
        text = tr_win32_native_to_utf8({ wide_text, wide_size });
    }

    LocalFree(wide_text);

    // Most (all?) messages contain "\r\n" in the end, chop it
    while (!std::empty(text) && isspace(text.back()))
    {
        text.resize(text.size() - 1);
    }

    return text;
}

namespace
{
namespace tr_main_win32_impl
{

std::optional<std::vector<std::string>> win32MakeUtf8Argv()
{
    int argc;
    auto argv = std::vector<std::string>{};
    if (wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc); wargv != nullptr)
    {
        for (int i = 0; i < argc; ++i)
        {
            if (wargv[i] == nullptr)
            {
                break;
            }

            auto str = tr_win32_native_to_utf8(wargv[i]);
            if (std::empty(str))
            {
                break;
            }

            argv.emplace_back(std::move(str));
        }

        LocalFree(wargv);
    }

    if (static_cast<int>(std::size(argv)) == argc)
    {
        return argv;
    }

    return {};
}

} // namespace tr_main_win32_impl
} // namespace

int tr_main_win32(int argc, char** argv, int (*real_main)(int, char**))
{
    using namespace tr_main_win32_impl;

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // build an argv from GetCommandLineW + CommandLineToArgvW
    if (auto argv_strs = win32MakeUtf8Argv(); argv_strs)
    {
        auto argv_cstrs = std::vector<char*>{};
        argv_cstrs.reserve(std::size(*argv_strs));
        std::transform(
            std::begin(*argv_strs),
            std::end(*argv_strs),
            std::back_inserter(argv_cstrs),
            [](auto& str) { return std::data(str); });
        argv_cstrs.push_back(nullptr); // argv is nullptr-terminated
        return (*real_main)(std::size(*argv_strs), std::data(argv_cstrs));
    }

    return (*real_main)(argc, argv);
}

#endif

/***
****
***/

namespace
{
namespace tr_parseNumberRange_impl
{

struct number_range
{
    int low;
    int high;
};

/**
 * This should be a single number (ex. "6") or a range (ex. "6-9").
 * Anything else is an error and will return failure.
 */
bool parseNumberSection(std::string_view str, number_range& range)
{
    auto constexpr Delimiter = "-"sv;

    auto const first = tr_parseNum<size_t>(str);
    if (!first)
    {
        return false;
    }

    range.low = range.high = *first;
    if (std::empty(str))
    {
        return true;
    }

    if (!tr_strvStartsWith(str, Delimiter))
    {
        return false;
    }

    str.remove_prefix(std::size(Delimiter));
    auto const second = tr_parseNum<size_t>(str);
    if (!second)
    {
        return false;
    }

    range.high = *second;
    return true;
}

} // namespace tr_parseNumberRange_impl
} // namespace

/**
 * Given a string like "1-4" or "1-4,6,9,14-51", this allocates and returns an
 * array of setmeCount ints of all the values in the array.
 * For example, "5-8" will return [ 5, 6, 7, 8 ] and setmeCount will be 4.
 * If a fragment of the string can't be parsed, nullptr is returned.
 */
std::vector<int> tr_parseNumberRange(std::string_view str)
{
    using namespace tr_parseNumberRange_impl;

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

double tr_truncd(double x, int decimal_places)
{
    auto buf = std::array<char, 128>{};
    auto const [out, len] = fmt::format_to_n(std::data(buf), std::size(buf) - 1, "{:.{}f}", x, DBL_DIG);
    *out = '\0';

    if (auto* const pt = strstr(std::data(buf), localeconv()->decimal_point); pt != nullptr)
    {
        pt[decimal_places != 0 ? decimal_places + 1 : 0] = '\0';
    }

    return atof(std::data(buf));
}

std::string tr_strpercent(double x)
{
    if (x < 5.0)
    {
        return fmt::format("{:.2f}", tr_truncd(x, 2));
    }

    if (x < 100.0)
    {
        return fmt::format("{:.1f}", tr_truncd(x, 1));
    }

    return fmt::format("{:.0f}", x);
}

std::string tr_strratio(double ratio, char const* infinity)
{
    if ((int)ratio == TR_RATIO_NA)
    {
        return _("None");
    }

    if ((int)ratio == TR_RATIO_INF)
    {
        auto buf = std::array<char, 64>{};
        tr_strlcpy(std::data(buf), infinity, std::size(buf));
        return std::data(buf);
    }

    return tr_strpercent(ratio);
}

/***
****
***/

bool tr_moveFile(std::string_view oldpath_in, std::string_view newpath_in, tr_error** error)
{
    auto const oldpath = tr_pathbuf{ oldpath_in };
    auto const newpath = tr_pathbuf{ newpath_in };

    // make sure the old file exists
    auto const info = tr_sys_path_get_info(oldpath, 0, error);
    if (!info)
    {
        tr_error_prefix(error, "Unable to get information on old file: ");
        return false;
    }
    if (!info->isFile())
    {
        tr_error_set(error, TR_ERROR_EINVAL, "Old path does not point to a file."sv);
        return false;
    }

    // ensure the target directory exists
    auto newdir = tr_pathbuf{ newpath.sv() };
    newdir.popdir();
    if (!tr_sys_dir_create(newdir, TR_SYS_DIR_CREATE_PARENTS, 0777, error))
    {
        tr_error_prefix(error, "Unable to create directory for new file: ");
        return false;
    }

    /* they might be on the same filesystem... */
    if (tr_sys_path_rename(oldpath, newpath))
    {
        return true;
    }

    /* Otherwise, copy the file. */
    if (!tr_sys_path_copy(oldpath, newpath, error))
    {
        tr_error_prefix(error, "Unable to copy: ");
        return false;
    }

    if (tr_error* my_error = nullptr; !tr_sys_path_remove(oldpath, &my_error))
    {
        tr_logAddError(fmt::format(
            _("Couldn't remove '{path}': {error} ({error_code})"),
            fmt::arg("path", oldpath),
            fmt::arg("error", my_error->message),
            fmt::arg("error_code", my_error->code)));
        tr_error_free(my_error);
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

    /* fallback code by bdonlan at https://stackoverflow.com/questions/809902/64-bit-ntohl-in-c/875505#875505 */
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

    /* fallback code by bdonlan at https://stackoverflow.com/questions/809902/64-bit-ntohl-in-c/875505#875505 */
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
***/

namespace
{
namespace formatter_impl
{

struct formatter_unit
{
    std::array<char, 16> name;
    uint64_t value;
};

using formatter_units = std::array<formatter_unit, 4>;

enum
{
    TR_FMT_KB,
    TR_FMT_MB,
    TR_FMT_GB,
    TR_FMT_TB
};

void formatter_init(formatter_units& units, uint64_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    uint64_t value = kilo;
    tr_strlcpy(std::data(units[TR_FMT_KB].name), kb, std::size(units[TR_FMT_KB].name));
    units[TR_FMT_KB].value = value;

    value *= kilo;
    tr_strlcpy(std::data(units[TR_FMT_MB].name), mb, std::size(units[TR_FMT_MB].name));
    units[TR_FMT_MB].value = value;

    value *= kilo;
    tr_strlcpy(std::data(units[TR_FMT_GB].name), gb, std::size(units[TR_FMT_GB].name));
    units[TR_FMT_GB].value = value;

    value *= kilo;
    tr_strlcpy(std::data(units[TR_FMT_TB].name), tb, std::size(units[TR_FMT_TB].name));
    units[TR_FMT_TB].value = value;
}

char* formatter_get_size_str(formatter_units const& u, char* buf, uint64_t bytes, size_t buflen)
{
    formatter_unit const* unit = nullptr;

    if (bytes < u[1].value)
    {
        unit = std::data(u);
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

    double const value = double(bytes) / unit->value;
    auto const* const units = std::data(unit->name);

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

    auto const [out, len] = fmt::format_to_n(buf, buflen - 1, "{:.{}f} {:s}", value, precision, units);
    *out = '\0';
    return buf;
}

formatter_units size_units;
formatter_units speed_units;
formatter_units mem_units;

} // namespace formatter_impl
} // namespace

size_t tr_speed_K = 0;

void tr_formatter_size_init(uint64_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    using namespace formatter_impl;
    formatter_init(size_units, kilo, kb, mb, gb, tb);
}

std::string tr_formatter_size_B(uint64_t bytes)
{
    using namespace formatter_impl;
    auto buf = std::array<char, 64>{};
    return formatter_get_size_str(size_units, std::data(buf), bytes, std::size(buf));
}

void tr_formatter_speed_init(size_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    using namespace formatter_impl;
    tr_speed_K = kilo;
    formatter_init(speed_units, kilo, kb, mb, gb, tb);
}

std::string tr_formatter_speed_KBps(double KBps)
{
    using namespace formatter_impl;

    auto speed = KBps;

    if (speed <= 999.95) // 0.0 KB to 999.9 KB
    {
        return fmt::format("{:d} {:s}", int(speed), std::data(speed_units[TR_FMT_KB].name));
    }

    double const K = speed_units[TR_FMT_KB].value;
    speed /= K;

    if (speed <= 99.995) // 0.98 MB to 99.99 MB
    {
        return fmt::format("{:.2f} {:s}", speed, std::data(speed_units[TR_FMT_MB].name));
    }

    if (speed <= 999.95) // 100.0 MB to 999.9 MB
    {
        return fmt::format("{:.1f} {:s}", speed, std::data(speed_units[TR_FMT_MB].name));
    }

    return fmt::format("{:.1f} {:s}", speed / K, std::data(speed_units[TR_FMT_GB].name));
}

size_t tr_mem_K = 0;

void tr_formatter_mem_init(size_t kilo, char const* kb, char const* mb, char const* gb, char const* tb)
{
    using namespace formatter_impl;

    tr_mem_K = kilo;
    formatter_init(mem_units, kilo, kb, mb, gb, tb);
}

std::string tr_formatter_mem_B(size_t bytes_per_second)
{
    using namespace formatter_impl;

    auto buf = std::array<char, 64>{};
    return formatter_get_size_str(mem_units, std::data(buf), bytes_per_second, std::size(buf));
}

void tr_formatter_get_units(void* vdict)
{
    using namespace formatter_impl;

    auto* dict = static_cast<tr_variant*>(vdict);

    tr_variantDictReserve(dict, 6);

    tr_variantDictAddInt(dict, TR_KEY_memory_bytes, mem_units[TR_FMT_KB].value);
    tr_variant* l = tr_variantDictAddList(dict, TR_KEY_memory_units, std::size(mem_units));
    for (auto const& unit : mem_units)
    {
        tr_variantListAddStr(l, std::data(unit.name));
    }

    tr_variantDictAddInt(dict, TR_KEY_size_bytes, size_units[TR_FMT_KB].value);
    l = tr_variantDictAddList(dict, TR_KEY_size_units, std::size(size_units));
    for (auto const& unit : size_units)
    {
        tr_variantListAddStr(l, std::data(unit.name));
    }

    tr_variantDictAddInt(dict, TR_KEY_speed_bytes, speed_units[TR_FMT_KB].value);
    l = tr_variantDictAddList(dict, TR_KEY_speed_units, std::size(speed_units));
    for (auto const& unit : speed_units)
    {
        tr_variantListAddStr(l, std::data(unit.name));
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

    auto value = std::array<char, 16>{};

    if (GetEnvironmentVariableA(key, std::data(value), std::size(value)) > 1)
    {
        return atoi(std::data(value));
    }

#else

    if (char const* const value = getenv(key); !tr_str_is_empty(value))
    {
        return atoi(value);
    }

#endif

    return default_value;
}

std::string tr_env_get_string(std::string_view key, std::string_view default_value)
{
#ifdef _WIN32

    if (auto const wide_key = tr_win32_utf8_to_native(key); !std::empty(wide_key))
    {
        if (auto const size = GetEnvironmentVariableW(wide_key.c_str(), nullptr, 0); size != 0)
        {
            auto wide_val = std::wstring{};
            wide_val.resize(size);
            if (GetEnvironmentVariableW(wide_key.c_str(), std::data(wide_val), std::size(wide_val)) == std::size(wide_val) - 1)
            {
                TR_ASSERT(wide_val.back() == L'\0');
                wide_val.resize(std::size(wide_val) - 1);
                return tr_win32_native_to_utf8(wide_val);
            }
        }
    }

#else

    auto const szkey = tr_strbuf<char, 256>{ key };

    if (auto const* const value = getenv(szkey); value != nullptr)
    {
        return value;
    }

#endif

    return std::string{ default_value };
}

/***
****
***/

void tr_net_init()
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

    if (auto const pos = filename.rfind('.'); pos != std::string_view::npos)
    {
        auto const suffix_lc = tr_strlower(filename.substr(pos + 1));
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

/// parseNum()

#if defined(__GNUC__) && !__has_include(<charconv>)

#include <iomanip> // std::setbase
#include <sstream>

template<typename T, std::enable_if_t<std::is_integral<T>::value, bool> = true>
[[nodiscard]] std::optional<T> tr_parseNum(std::string_view& sv, int base)
{
    auto val = T{};
    auto const str = std::string(std::data(sv), std::min(std::size(sv), size_t{ 64 }));
    auto sstream = std::stringstream{ str };
    auto const oldpos = sstream.tellg();
    /* The base parameter only works for bases 8, 10 and 16.
       All other bases will be converted to 0 which activates the
       prefix based parsing and therefore decimal in our usual cases.
       This differs from the from_chars solution below. */
    sstream >> std::setbase(base) >> val;
    auto const newpos = sstream.tellg();
    if ((newpos == oldpos) || (sstream.fail() && !sstream.eof()))
    {
        return std::nullopt;
    }
    sv.remove_prefix(sstream.eof() ? std::size(sv) : newpos - oldpos);
    return val;
}

#else // #if defined(__GNUC__) && !__has_include(<charconv>)

#include <charconv> // std::from_chars()

template<typename T, std::enable_if_t<std::is_integral<T>::value, bool>>
[[nodiscard]] std::optional<T> tr_parseNum(std::string_view& sv, int base)
{
    auto val = T{};
    auto const* const begin_ch = std::data(sv);
    auto const* const end_ch = begin_ch + std::size(sv);
    /* The base parameter works for any base from 2 to 36 (inclusive).
       This is different from the behaviour of the stringstream
       based solution above. */
    auto const result = std::from_chars(begin_ch, end_ch, val, base);
    if (result.ec != std::errc{})
    {
        return std::nullopt;
    }
    sv.remove_prefix(result.ptr - std::data(sv));
    return val;
}

#endif // #if defined(__GNUC__) && !__has_include(<charconv>)

template std::optional<long long> tr_parseNum(std::string_view& sv, int base);
template std::optional<long> tr_parseNum(std::string_view& sv, int base);
template std::optional<int> tr_parseNum(std::string_view& sv, int base);
template std::optional<char> tr_parseNum(std::string_view& sv, int base);

template std::optional<unsigned long long> tr_parseNum(std::string_view& sv, int base);
template std::optional<unsigned long> tr_parseNum(std::string_view& sv, int base);
template std::optional<unsigned int> tr_parseNum(std::string_view& sv, int base);
template std::optional<unsigned char> tr_parseNum(std::string_view& sv, int base);

template<typename T, std::enable_if_t<std::is_floating_point<T>::value, bool>>
[[nodiscard]] std::optional<T> tr_parseNum(std::string_view& sv)
{
    auto const* const begin_ch = std::data(sv);
    auto const* const end_ch = begin_ch + std::size(sv);
    auto val = T{};
    auto const result = fast_float::from_chars(begin_ch, end_ch, val);
    if (result.ec != std::errc{})
    {
        return std::nullopt;
    }
    sv.remove_prefix(result.ptr - std::data(sv));
    return val;
}

template std::optional<double> tr_parseNum(std::string_view& sv);
