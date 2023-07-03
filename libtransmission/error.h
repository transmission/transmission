// This file Copyright © 2013-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>
#include <string_view>

#include "libtransmission/tr-assert.h"

struct tr_error
{
private:
    enum class Type
    {
        Generic,
        Errno,
        SockErrno,
    };

public:
    [[nodiscard]] constexpr auto code() const noexcept
    {
        return code_;
    }

    [[nodiscard]] constexpr bool is_set() const noexcept
    {
        return code() != 0;
    }

    [[nodiscard]] constexpr operator bool() const noexcept
    {
        return is_set();
    }

    [[nodiscard]] auto const& message() const
    {
        ensure_message();
        return message_;
    }

    void clear()
    {
        set(0, std::string_view{});
    }

    void set(int error_code, std::string_view message)
    {
        TR_ASSERT(!is_set());

        code_ = error_code;
        type_ = Type::Generic;
        message_ = message;
    }

    void set_from_errno(int error_code)
    {
        TR_ASSERT(!is_set());

        type_ = Type::Errno;
        code_ = error_code;
        message_.clear();
    }

    void set_from_sockerrno(int error_code)
    {
        TR_ASSERT(!is_set());

        type_ = Type::SockErrno;
        code_ = error_code;
        message_.clear();
    }

    void prefix_message(std::string_view prefix)
    {
        ensure_message();
        message_.insert(0, prefix);
    }

private:
    void ensure_message() const;

    Type type_ = Type::Generic;

    int code_ = 0;

    /** @brief Error message */
    mutable std::string message_;
};

void tr_error_set(tr_error* error, int error_code, std::string_view message)
{
    if (error != nullptr)
    {
        error->set(error_code, message);
    }
}

void tr_error_set_from_errno(tr_error* error, int error_code)
{
    if (error != nullptr)
    {
        error->set_from_errno(error_code);
    }
}

void tr_error_set_from_sockerrno(tr_error* error, int error_code)
{
    if (error != nullptr)
    {
        error->set_from_sockerrno(error_code);
    }
}

void tr_error_propagate(tr_error* tgt, tr_error&& src)
{
    if (tgt != nullptr)
    {
        *tgt = std::move(src);
    }
}

void tr_error_prefix_message(tr_error* error, std::string_view prefix)
{
    if (error != nullptr)
    {
        error->prefix_message(prefix);
    }
}
