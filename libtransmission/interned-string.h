/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#include <string_view>

#include "transmission.h"

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
    explicit tr_interned_string(std::string_view str)
        : tr_interned_string{ tr_quark_new(str) }
    {
    }
    explicit tr_interned_string(char const* str)
        : tr_interned_string{ std::string_view{ str ? str : "" } }
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
    tr_interned_string& operator=(char const* str)
    {
        return *this = std::string_view{ str != nullptr ? str : "" };
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

    [[nodiscard]] int compare(tr_interned_string const& that) const // <=>
    {
        return this->quark() - that.quark();
    }

    [[nodiscard]] bool operator<(tr_interned_string const& that) const
    {
        return this->compare(that) < 0;
    }

    [[nodiscard]] bool operator==(tr_interned_string const& that) const
    {
        return this->compare(that) == 0;
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
