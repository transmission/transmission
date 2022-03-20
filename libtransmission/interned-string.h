// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

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

    [[nodiscard]] tr_quark quark() const
    {
        return quark_;
    }
    [[nodiscard]] char const* c_str() const
    {
        return tr_quark_get_string(quark_);
    }
    [[nodiscard]] std::string_view sv() const
    {
        return tr_quark_get_string_view(quark_);
    }

    [[nodiscard]] auto data() const
    {
        return std::data(this->sv());
    }
    [[nodiscard]] auto empty() const
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

    [[nodiscard]] auto compare(tr_interned_string const& that) const // <=>
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

    [[nodiscard]] bool operator<(tr_interned_string const& that) const
    {
        return this->compare(that) < 0;
    }

    [[nodiscard]] bool operator>(tr_interned_string const& that) const
    {
        return this->compare(that) > 0;
    }

    [[nodiscard]] bool operator==(tr_interned_string const& that) const
    {
        return this->compare(that) == 0;
    }
    [[nodiscard]] bool operator!=(tr_interned_string const& that) const
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

private:
    tr_quark quark_ = TR_KEY_NONE;
};
