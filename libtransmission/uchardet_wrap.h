// This file Copyright © Transmission authors and contributors.
// It may be used under the 3-Clause BSD (SPDX: BSD-3-Clause),
// GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// This file defines the C++ wrapper for the Mozilla uchardet library.

#pragma once

#include <string>
#include <string_view>

#include <uchardet/uchardet.h>

class CharsetDetector
{
public:
    enum Status
    {
        OK = 0,
        INIT_FAILED,
        INVALID_INPUT,
        PROCESS_ERROR,
        FILE_ERROR,
        UNKNOWN_ENCODING
    };

    CharsetDetector() noexcept
        : ud_(uchardet_new())
        , lastStatus_(OK)
    {
        if (!ud_)
        {
            lastStatus_ = INIT_FAILED;
        }
    }

    ~CharsetDetector()
    {
        if (ud_)
        {
            uchardet_delete(ud_);
        }
    }

    CharsetDetector(CharsetDetector const&) = delete;
    CharsetDetector& operator=(CharsetDetector const&) = delete;

    CharsetDetector(CharsetDetector&& other) noexcept
        : ud_(other.ud_)
        , lastStatus_(other.lastStatus_)
        , lastEncoding_(std::move(other.lastEncoding_))
    {
        other.ud_ = nullptr;
    }

    CharsetDetector& operator=(CharsetDetector&& other) noexcept
    {
        if (this != &other)
        {
            if (ud_)
            {
                uchardet_delete(ud_);
            }
            ud_ = other.ud_;
            lastStatus_ = other.lastStatus_;
            lastEncoding_ = std::move(other.lastEncoding_);
            other.ud_ = nullptr;
        }
        return *this;
    }

    Status detectFromBuffer(char const* data, size_t length) noexcept
    {
        if (!ud_)
            return INIT_FAILED;
        if (!data || length == 0)
            return INVALID_INPUT;

        uchardet_reset(ud_);
        if (uchardet_handle_data(ud_, data, length) != 0)
        {
            lastStatus_ = PROCESS_ERROR;
            return lastStatus_;
        }

        uchardet_data_end(ud_);
        char const* charset = uchardet_get_charset(ud_);
        if (!charset || charset[0] == '\0')
        {
            lastEncoding_ = {};
            lastStatus_ = UNKNOWN_ENCODING;
        }
        else
        {
            lastEncoding_ = charset;
            lastStatus_ = OK;
        }
        return lastStatus_;
    }

    Status detectFromString(std::string_view const text) noexcept
    {
        return detectFromBuffer(text.data(), text.size());
    }

    Status detectFromString(std::string const& text) noexcept
    {
        return detectFromBuffer(text.data(), text.size());
    }

    std::string_view const getEncoding() const noexcept
    {
        return lastEncoding_;
    }

    Status getLastStatus() const noexcept
    {
        return lastStatus_;
    }

private:
    uchardet_t ud_;
    Status lastStatus_;
    std::string_view lastEncoding_;
};
