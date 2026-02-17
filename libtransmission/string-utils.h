// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
[[nodiscard]] std::string tr_win32_format_message(uint32_t code);
[[nodiscard]] std::string tr_win32_native_to_utf8(std::wstring_view);
[[nodiscard]] std::wstring tr_win32_utf8_to_native(std::string_view);
#endif

/** @brief Convenience wrapper around `strerorr()` guaranteed to not return nullptr
    @param errnum the error number to describe */
[[nodiscard]] char const* tr_strerror(int errnum);

template <typename T> [[nodiscard]] std::string tr_strlower(T in)
{
    auto out = std::string{ std::move(in) };
    std::for_each(std::begin(out), std::end(out), [](char& ch) { ch = std::tolower(ch); });
    return out;
}

template <typename T> [[nodiscard]] std::string tr_strupper(T in)
{
    auto out = std::string{ std::move(in) };
    std::for_each(std::begin(out), std::end(out), [](char& ch) { ch = std::toupper(ch); });
    return out;
}

/**
 * @brief Rich Salz's classic implementation of shell-style pattern matching for `?`, `\`, `[]`, and `*` characters.
 * @return 1 if the pattern matches, 0 if it doesn't, or -1 if an error occurred
 */
[[nodiscard]] bool tr_wildmat(char const* text, char const* pattern);

// c++23 (P1679R3), GCC 11.1, clang 12
template <typename T> [[nodiscard]] constexpr bool tr_strv_contains(std::string_view sv, T key) noexcept
{
    return sv.find(key) != std::string_view::npos;
}

// c++20 (P0457R2), GCC 9.1, clang 6
[[nodiscard]] constexpr bool tr_strv_starts_with(std::string_view sv, char key)
{
    return !std::empty(sv) && sv.front() == key;
}

// c++20 (P0457R2), GCC 9.1, clang 6
[[nodiscard]] constexpr bool tr_strv_starts_with(std::string_view sv, std::string_view key)
{
    return std::size(key) <= std::size(sv) && sv.substr(0, std::size(key)) == key;
}

// c++20 (P0457R2), GCC 9.1, clang 6
[[nodiscard]] constexpr bool tr_strv_starts_with(
    std::wstring_view sv,
    std::wstring_view key) // c++20 (P0457R2), GCC 9.1, clang 6
{
    return std::size(key) <= std::size(sv) && sv.substr(0, std::size(key)) == key;
}

// c++20 (P0457R2), GCC 9.1, clang 6
[[nodiscard]] constexpr bool tr_strv_ends_with(std::string_view sv, std::string_view key)
{
    return std::size(key) <= std::size(sv) && sv.substr(std::size(sv) - std::size(key)) == key;
}

// c++20 (P0457R2), GCC 9.1, clang 6
[[nodiscard]] constexpr bool tr_strv_ends_with(std::string_view sv, char key)
{
    return !std::empty(sv) && sv.back() == key;
}

template <typename... Args> constexpr std::string_view tr_strv_sep(std::string_view* sv, Args&&... args)
{
    auto pos = sv->find_first_of(std::forward<Args>(args)...);
    auto const ret = sv->substr(0, pos);
    sv->remove_prefix(pos != std::string_view::npos ? pos + 1 : std::size(*sv));
    return ret;
}

template <typename... Args> constexpr bool tr_strv_sep(std::string_view* sv, std::string_view* token, Args&&... args)
{
    if (std::empty(*sv))
    {
        return false;
    }

    *token = tr_strv_sep(sv, std::forward<Args>(args)...);
    return true;
}

[[nodiscard]] std::string_view tr_strv_strip(std::string_view str);

[[nodiscard]] std::string tr_strv_to_utf8_string(std::string_view sv);

#ifdef __APPLE__
#ifdef __OBJC__
@class NSString;
[[nodiscard]] std::string tr_strv_to_utf8_string(NSString* str);
[[nodiscard]] NSString* tr_strv_to_utf8_nsstring(std::string_view sv);
[[nodiscard]] NSString* tr_strv_to_utf8_nsstring(std::string_view sv, NSString* key, NSString* comment);
#endif
#endif

[[nodiscard]] std::u8string tr_strv_to_u8string(std::string_view sv);

[[nodiscard]] std::string tr_strv_replace_invalid(std::string_view sv, uint32_t replacement = 0xFFFD /*�*/);

[[nodiscard]] std::string_view::size_type tr_strv_find_invalid_utf8(std::string_view sv);
[[nodiscard]] std::u8string_view::size_type tr_strv_find_invalid_utf8(std::u8string_view sv);
