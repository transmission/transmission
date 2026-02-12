// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cctype>
#include <charconv> // std::from_chars()
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
#endif

#include <fmt/format.h>

#include <fast_float/fast_float.h>

#include <utf8.h>

#include <wildmat.h>

#include "lib/base/string-utils.h"
#include "lib/base/tr-assert.h"

/* User-level routine. returns whether or not 'text' and 'pattern' matched */
bool tr_wildmat(char const* text, char const* pattern)
{
    // TODO(ckerr): replace wildmat with base/strings/pattern.cc
    // wildmat wants these to be zero-terminated.
    return (pattern[0] == '*' && pattern[1] == '\0') || DoMatch(text, pattern) > 0;
}

char const* tr_strerror(int errnum)
{
    if (char const* const ret = strerror(errnum); ret != nullptr)
    {
        return ret;
    }

    return "Unknown Error";
}

// ---

std::string_view tr_strv_strip(std::string_view str)
{
    auto constexpr Test = [](auto ch)
    {
        return isspace(static_cast<unsigned char>(ch));
    };

    auto const it = std::ranges::find_if_not(str, Test);
    str.remove_prefix(std::ranges::distance(std::ranges::begin(str), it));

    auto const rit = std::ranges::find_if_not(std::ranges::rbegin(str), std::ranges::rend(str), Test);
    str.remove_suffix(std::ranges::distance(std::ranges::rbegin(str), rit));

    return str;
}

// ---

#if !(defined(__APPLE__) && defined(__clang__))

std::string tr_strv_to_utf8_string(std::string_view sv)
{
    return tr_strv_replace_invalid(sv);
}

#endif

std::string tr_strv_replace_invalid(std::string_view sv, uint32_t replacement)
{
    // stripping characters after first \0
    if (auto first_null = sv.find('\0'); first_null != std::string::npos)
    {
        sv = { std::data(sv), first_null };
    }
    auto out = std::string{};
    out.reserve(std::size(sv));
    utf8::unchecked::replace_invalid(std::data(sv), std::data(sv) + std::size(sv), std::back_inserter(out), replacement);
    return out;
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
        reinterpret_cast<LPWSTR>(&wide_text),
        0,
        nullptr);

    if (wide_size == 0)
    {
        return fmt::format("Unknown error ({:#08x})", code);
    }

    auto text = std::string{};

    if (wide_size != 0 && wide_text != nullptr)
    {
        text = tr_win32_native_to_utf8({ wide_text, wide_size });
    }

    LocalFree(wide_text);

    // Most (all?) messages contain "\r\n" in the end, chop it
    while (!std::empty(text) && isspace(text.back()) != 0)
    {
        text.resize(text.size() - 1);
    }

    return text;
}

#endif

std::string_view::size_type tr_strv_find_invalid_utf8(std::string_view const sv)
{
    return utf8::find_invalid(sv);
}

std::u8string tr_strv_to_u8string(std::string_view const sv)
{
    auto u8str = tr_strv_to_utf8_string(sv);
    auto const view = std::views::transform(u8str, [](char c) -> char8_t { return c; });
    return { view.begin(), view.end() };
}

// --- tr_num_parse()

template<typename T>
[[nodiscard]] std::optional<T> tr_num_parse(std::string_view str, std::string_view* remainder, int base)
    requires std::is_integral_v<T>
{
    auto val = T{};
    auto const* const begin_ch = std::data(str);
    auto const* const end_ch = begin_ch + std::size(str);
    auto const result = std::from_chars(begin_ch, end_ch, val, base);
    if (result.ec != std::errc{})
    {
        return std::nullopt;
    }
    if (remainder != nullptr)
    {
        *remainder = str;
        remainder->remove_prefix(result.ptr - std::data(str));
    }
    return val;
}

template std::optional<long long> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<long> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<int> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<char> tr_num_parse(std::string_view str, std::string_view* remainder, int base);

template std::optional<unsigned long long> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<unsigned long> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<unsigned int> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<unsigned short> tr_num_parse(std::string_view str, std::string_view* remainder, int base);
template std::optional<unsigned char> tr_num_parse(std::string_view str, std::string_view* remainder, int base);

template<typename T>
[[nodiscard]] std::optional<T> tr_num_parse(std::string_view str, std::string_view* remainder)
    requires std::is_floating_point_v<T>
{
    auto const* const begin_ch = std::data(str);
    auto const* const end_ch = begin_ch + std::size(str);
    auto val = T{};
    auto const result = fast_float::from_chars(begin_ch, end_ch, val);
    if (result.ec != std::errc{})
    {
        return std::nullopt;
    }
    if (remainder != nullptr)
    {
        *remainder = str;
        remainder->remove_prefix(result.ptr - std::data(str));
    }
    return val;
}

template std::optional<double> tr_num_parse(std::string_view sv, std::string_view* remainder);
