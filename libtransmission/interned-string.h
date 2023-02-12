// This file Copyright © 2021-2022 Mnemosyne LLC.
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

    [[nodiscard]] constexpr tr_quark quark() const noexcept
    {
        return quark_;
    }
    [[nodiscard]] std::string_view sv() const
    {
        return tr_quark_get_string_view(quark_);
    }
    [[nodiscard]] char const* c_str() const
    {
        return std::data(sv()); // tr_quark strs are always zero-terminated
    }

    [[nodiscard]] auto data() const
    {
        return std::data(this->sv());
    }
    [[nodiscard]] constexpr auto empty() const noexcept
    {
        return quark_ == TR_KEY_NONE;
    }
    [[nodiscard]] auto size() const
    {
        return std::size(this->sv());
    }
    void clear()
    {
        *this = TR_KEY_NONE;
    }

    [[nodiscard]] auto begin() const
    {
        return std::begin(this->sv());
    }
    [[nodiscard]] auto end() const
    {
        return std::end(this->sv());
    }

    [[nodiscard]] auto rbegin() const
    {
        return std::rbegin(this->sv());
    }
    [[nodiscard]] auto rend() const
    {
        return std::rend(this->sv());
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

    [[nodiscard]] bool operator==(std::string_view that) const
    {
        return this->sv() == that;
    }
    [[nodiscard]] bool operator!=(std::string_view that) const
    {
        return this->sv() != that;
    }
    [[nodiscard]] bool operator==(char const* that) const
    {
        return *this == std::string_view{ that != nullptr ? that : "" };
    }
    [[nodiscard]] bool operator!=(char const* that) const
    {
        return *this != std::string_view{ that != nullptr ? that : "" };
    }

    operator std::string_view() const
    {
        return sv();
    }

private:
    tr_quark quark_ = TR_KEY_NONE;
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
