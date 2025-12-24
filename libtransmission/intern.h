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
namespace transmission::intern
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
struct Interned
{
    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return sv;
    }

    [[nodiscard]] constexpr uint64_t id() const noexcept
    {
        return key;
    }

    // Implicit conversion to uint64_t.
    // This way, constexpr `Interned` instances can be used switch-case statements
    constexpr operator uint64_t() const noexcept
    {
        return key;
    }

    // TODO(C++20) spaceship operator
    [[nodiscard]] constexpr bool operator==(Interned const& that) const noexcept
    {
        return key == that.key;
    }
    [[nodiscard]] constexpr bool operator<(Interned const& that) const noexcept
    {
        return key < that.key;
    }
    [[nodiscard]] constexpr bool operator>(Interned const& that) const noexcept
    {
        return key > that.key;
    }

    std::string_view sv = {};

    uint64_t key = {};
};

namespace detail
{
// FNV hashes are designed to be fast while maintaining a low collision rate.
// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
constexpr uint64_t fnv1a_64(std::string_view s) noexcept
{
    uint64_t h = 14695981039346656037ull;
    for (char c : s)
    {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// Used to distinguish between compile-time and run-time items:
// The top bit is true for strings interned during runtime.
constexpr uint64_t kRuntimeTag = 1ull << 63;
constexpr uint64_t kPayloadMask = ~kRuntimeTag;

constexpr uint64_t known_key(std::string_view const s) noexcept
{
    return fnv1a_64(s) & kPayloadMask; // top bit 0
}
} // namespace detail

// Intern a known string at compile time.
// Usage: `inline constexpr auto TR_KEY_foo = known("foo");`
// TODO(C++20): consteval
template<std::size_t N>
[[nodiscard]] constexpr Interned known(char const (&lit)[N]) noexcept
{
    constexpr std::size_t len = N > 0 ? N - 1 : 0;
    auto sv = std::string_view{ lit, len };
    return Interned{ sv, detail::known_key(sv) };
}

/**
 * A registry of `Interned` strings.
 */
class Interner
{
public:
    static Interner& instance();

    // Get the interned copy of `str`.
    std::optional<Interned> lookup(std::string_view str) const noexcept;

    // Add a new Interned string.
    // If the string is already interned, return the existing copy.
    Interned add(std::string_view str);

    // Add strings as a batch.
    // Only use this to register compile-time strings during startup.
    void add_known(Interned const* entries, size_t n_entries);

    template<size_t N>
    void add_known(std::array<Interned, N> const& entries)
    {
        add_known(entries.data(), entries.size());
    }

private:
    Interner();
    ~Interner() noexcept = default;
    Interner(Interner const&) = delete;
    Interner& operator=(Interner const&) = delete;

    struct Impl;
    std::unique_ptr<Impl> const pimpl_;
};

} // namespace transmission::intern
