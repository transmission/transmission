// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm>
#include <compare>
#include <string_view>

#include <fmt/format.h>

#include "libtransmission/quark.h"

/**
 * Helper functions wrapped around tr_quark
 */
class tr_interned_string
{
public:
    tr_interned_string() = default;

    explicit tr_interned_string(tr_quark quark)
        : quark_{ quark }
        , sv_{ tr_quark_get_u8string_view(quark_) }
    {
    }

    explicit tr_interned_string(std::u8string_view sv)
        : tr_interned_string{ tr_quark_new(sv) }
    {
    }

    explicit tr_interned_string(std::string_view sv)
        : tr_interned_string{ tr_quark_new(sv) }
    {
    }

    explicit tr_interned_string(char const* c_str)
        : tr_interned_string{ std::string_view{ c_str != nullptr ? c_str : "" } }
    {
    }

    tr_interned_string& operator=(tr_quark quark)
    {
        quark_ = quark;
        sv_ = tr_quark_get_u8string_view(quark_);
        return *this;
    }

    tr_interned_string& operator=(std::u8string_view sv)
    {
        return *this = tr_quark_new(sv);
    }

    tr_interned_string& operator=(std::string_view sv)
    {
        return *this = tr_quark_new(sv);
    }

    tr_interned_string& operator=(char const* c_str)
    {
        return *this = std::string_view{ c_str != nullptr ? c_str : "" };
    }

    [[nodiscard]] constexpr auto quark() const noexcept
    {
        return quark_;
    }

    [[nodiscard]] constexpr auto u8sv() const noexcept
    {
        return sv_;
    }

    [[nodiscard]] constexpr auto data() const noexcept
    {
        return std::data(u8sv());
    }

    [[nodiscard]] char const* c_str() const noexcept
    {
        return reinterpret_cast<char const*>(data()); // tr_quark strs are always zero-terminated
    }

    [[nodiscard]] constexpr auto empty() const noexcept
    {
        return quark_ == TR_KEY_NONE;
    }

    [[nodiscard]] constexpr auto size() const noexcept
    {
        return std::size(u8sv());
    }

    [[nodiscard]] auto sv() const noexcept
    {
        return std::string_view{ reinterpret_cast<char const*>(data()), size() };
    }

    constexpr void clear()
    {
        *this = tr_interned_string{};
    }

    [[nodiscard]] constexpr auto begin() const noexcept
    {
        return std::begin(u8sv());
    }

    [[nodiscard]] constexpr auto end() const noexcept
    {
        return std::end(u8sv());
    }

    [[nodiscard]] constexpr auto rbegin() const noexcept
    {
        return std::rbegin(u8sv());
    }
    [[nodiscard]] constexpr auto rend() const noexcept
    {
        return std::rend(u8sv());
    }

    [[nodiscard]] constexpr auto operator<=>(tr_interned_string const& that) const noexcept
    {
        return this->quark() <=> that.quark();
    }
    [[nodiscard]] constexpr bool operator==(tr_interned_string const& that) const noexcept
    {
        return (*this <=> that) == 0;
    }

    [[nodiscard]] constexpr auto operator<=>(std::string_view that) const noexcept
    {
        return std::lexicographical_compare_three_way(begin(), end(), that.begin(), that.end());
    }
    [[nodiscard]] constexpr bool operator==(std::string_view that) const noexcept
    {
        return std::ranges::equal(*this, that);
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    [[nodiscard]] constexpr operator std::u8string_view() const noexcept
    {
        return u8sv();
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    [[nodiscard]] operator std::string_view() const noexcept
    {
        return sv();
    }

private:
    tr_quark quark_ = TR_KEY_NONE;
    std::u8string_view sv_;
};

template<>
struct fmt::formatter<tr_interned_string> : formatter<std::string_view>
{
    template<typename FormatContext>
    constexpr auto format(tr_interned_string const& is, FormatContext& ctx) const
    {
        return formatter<std::string_view>::format(is.sv(), ctx);
    }
};
