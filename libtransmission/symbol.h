// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

// Namespace for interning strings
namespace transmission::symbol
{

/**
 * An interned string.
 *
 * Can be built at compile time with `known()` or added during
 * runtime with the `Interner` class.
 *
 * Known strings built at compile time -- such as strings from
 * the BT protocol, RPC API, and settings files -- can be used
 * in switch-case statements.
 */
struct Symbol
{
    [[nodiscard]] constexpr std::string_view sv() const noexcept
    {
        return sv_;
    }

    [[nodiscard]] constexpr uint32_t id() const noexcept
    {
        return id_;
    }

    // Implicit conversion to uint32_t.
    // This way, constexpr `Symbol` instances can be used switch-case statements
    // NOLINTNEXTLINE(google-explicit-constructor)
    constexpr operator uint32_t() const noexcept
    {
        return id_;
    }

    [[nodiscard]] constexpr bool operator==(Symbol const& that) const noexcept
    {
        return id_ == that.id_;
    }

    std::string_view sv_;

    uint32_t id_ = {};
};

namespace detail
{
// FNV hashes are designed to be fast while maintaining a low collision rate.
// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
constexpr uint32_t fnv1a_32(std::string_view s) noexcept
{
    uint32_t h = 2166136261U;
    for (char const c : s)
    {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619U;
    }
    return h;
}

// Used to distinguish between compile-time and run-time items:
// The top bit is true for strings interned during runtime.
constexpr uint32_t RuntimeTag = 1U << 31;
constexpr uint32_t PayloadMask = ~RuntimeTag;

constexpr uint32_t known_key(std::string_view const s) noexcept
{
    return fnv1a_32(s) & PayloadMask; // top bit 0
}
} // namespace detail

// Intern a known string at compile time.
// Usage: `inline constexpr auto TR_KEY_foo = known("foo");`
// TODO(C++20): consteval
template<std::size_t N>
// NOLINTNEXTLINE(modernize-avoid-c-arrays)
[[nodiscard]] constexpr Symbol known(char const (&lit)[N]) noexcept
{
    constexpr std::size_t Len = N > 0 ? N - 1 : 0;
    auto sv = std::string_view{ lit, Len };
    return Symbol{ sv, detail::known_key(sv) };
}

/**
 * A registry of Symbols.
 */
class StringInterner
{
public:
    static StringInterner& instance();

    StringInterner(StringInterner const&) = delete;
    StringInterner(StringInterner&&) = delete;
    StringInterner& operator=(StringInterner const&) = delete;
    StringInterner& operator=(StringInterner&&) = delete;

    // Return the interned copy of `str`, if any.
    // Can be used to query if a string has already been interned without interning.
    [[nodiscard]] std::optional<Symbol> get(std::string_view str) const noexcept;

    // Add a new Symbol.
    // If the string is already interned, return the existing copy.
    [[nodiscard]] Symbol get_or_intern(std::string_view str);

    // Add strings as a batch.
    // Only use this to register compile-time strings during startup.
    void add_known(Symbol const* entries, size_t n_entries);

    template<size_t N>
    void add_known(std::array<Symbol, N> const& entries)
    {
        add_known(entries.data(), entries.size());
    }

private:
    StringInterner();
    ~StringInterner() noexcept = default;

    struct Impl;
    std::unique_ptr<Impl> const pimpl_;
};

} // namespace transmission::symbol
