// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>
#include <string_view>

#include "libtransmission/tr-macros.h"

/** @brief Structure holding error information. */
struct tr_error
{
public:
    tr_error() = default;

    tr_error(int code, std::string message)
        : message_{ std::move(message) }
        , code_{ code }
    {
    }

    [[nodiscard]] constexpr auto code() const noexcept
    {
        return code_;
    }

    [[nodiscard]] TR_CONSTEXPR20 auto message() const noexcept
    {
        return std::string_view{ message_ };
    }

    [[nodiscard]] constexpr auto has_value() const noexcept
    {
        return code_ != 0;
    }

    [[nodiscard]] constexpr operator bool() const noexcept
    {
        return has_value();
    }

    void set(int code, std::string&& message)
    {
        code_ = code;
        message_ = std::move(message);
    }

    void set(int code, std::string_view message)
    {
        code_ = code;
        message_.assign(message);
    }

    void set(int code, char const* const message)
    {
        set(code, std::string_view{ message != nullptr ? message : "" });
    }

    void prefix_message(std::string_view prefix)
    {
        message_.insert(std::begin(message_), std::begin(prefix), std::end(prefix));
    }

    // convenience utility for `set(errno, tr_strerror(errno))`
    void set_from_errno(int errnum);

private:
    /** @brief Error message */
    std::string message_;

    /** @brief Error code, platform-specific */
    int code_ = 0;
};
