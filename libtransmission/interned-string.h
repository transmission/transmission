// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include <fmt/format.h>

#include "quark.h"

/**
 * Helper functions wrapped around tr_quark
 */
class tr_interned_string
{
public:
    tr_interned_string() = default;

    explicit tr_interned_string(tr_quark quark)
        : quark_{ quark }
        , sv_{ tr_quark_get_string_view(quark_) }
    {
    }

    explicit tr_interned_string(std::string_view sv)
        : tr_interned_string{ tr_quark_new(sv) }
    {
    }

    explicit tr_interned_string(char const* c_str)
        : tr_interned_string{ std::string_view{ c_str ? c_str : "" } }
    {
    }

    tr_interned_string& operator=(tr_quark quark)
    {
        quark_ = quark;
        sv_ = tr_quark_get_string_view(quark_);
        return *this;
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

    [[nodiscard]] constexpr auto sv() const noexcept
    {
        return sv_;
    }

    [[nodiscard]] constexpr char const* c_str() const noexcept
    {
        return std::data(sv()); // tr_quark strs are always zero-terminated
    }

    [[nodiscard]] constexpr auto data() const noexcept
    {
        return std::data(sv());
    }

    [[nodiscard]] constexpr auto empty() const noexcept
    {
        return quark_ == TR_KEY_NONE;
    }

    [[nodiscard]] constexpr auto size() const noexcept
    {
        return std::size(sv());
    }

    constexpr void clear()
    {
        *this = tr_interned_string{};
    }

    [[nodiscard]] constexpr auto begin() const noexcept
    {
        return std::begin(sv());
    }

    [[nodiscard]] constexpr auto end() const noexcept
    {
        return std::end(sv());
    }

    [[nodiscard]] constexpr auto rbegin() const noexcept
    {
        return std::rbegin(sv());
    }
    [[nodiscard]] constexpr auto rend() const noexcept
    {
        return std::rend(sv());
    }

    [[nodiscard]] constexpr auto compare(tr_interned_string const& that) const noexcept // <=>
    {
        if (this->quark() < that.quark())
        {
            return -1;
        }

        if (this->quark() > that.quark())
        {
            return 1;
        }

        return 0;
    }

    [[nodiscard]] constexpr bool operator<(tr_interned_string const& that) const noexcept
    {
        return this->compare(that) < 0;
    }

    [[nodiscard]] constexpr bool operator>(tr_interned_string const& that) const noexcept
    {
        return this->compare(that) > 0;
    }

    [[nodiscard]] constexpr bool operator==(tr_interned_string const& that) const noexcept
    {
        return this->compare(that) == 0;
    }
    [[nodiscard]] constexpr bool operator!=(tr_interned_string const& that) const noexcept
    {
        return this->compare(that) != 0;
    }

    [[nodiscard]] constexpr auto operator==(std::string_view that) const noexcept
    {
        return sv() == that;
    }
    [[nodiscard]] constexpr auto operator!=(std::string_view that) const noexcept
    {
        return sv() != that;
    }
    [[nodiscard]] constexpr bool operator==(char const* that) const noexcept
    {
        return *this == std::string_view{ that != nullptr ? that : "" };
    }
    [[nodiscard]] constexpr bool operator!=(char const* that) const noexcept
    {
        return *this != std::string_view{ that != nullptr ? that : "" };
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept
    {
        return sv();
    }

private:
    tr_quark quark_ = TR_KEY_NONE;
    std::string_view sv_ = "";
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
