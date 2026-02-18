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

    // Returns the Windows codepage number for the detected encoding,
    // or 0 if unknown. Covers all encodings that uchardet v0.0.8 can return.
    [[nodiscard]] static unsigned int encodingToCodepage(std::string_view encoding) noexcept
    {
        static constexpr std::pair<std::string_view, unsigned int> table[] = {
            { "ASCII", 20127 },        { "BIG5", 950 },
            { "EUC-JP", 20932 },       { "EUC-KR", 51949 },
            { "EUC-TW", 51950 },       { "GB18030", 54936 },
            { "HZ-GB-2312", 52936 },   { "IBM852", 852 },
            { "IBM855", 855 },         { "IBM865", 865 },
            { "IBM866", 866 },         { "ISO-2022-CN", 50227 },
            { "ISO-2022-JP", 50220 },  { "ISO-2022-KR", 50225 },
            { "ISO-8859-1", 28591 },   { "ISO-8859-10", 28600 },
            { "ISO-8859-11", 28601 },  { "ISO-8859-13", 28603 },
            { "ISO-8859-15", 28605 },  { "ISO-8859-16", 28606 },
            { "ISO-8859-2", 28592 },   { "ISO-8859-3", 28593 },
            { "ISO-8859-4", 28594 },   { "ISO-8859-5", 28595 },
            { "ISO-8859-6", 28596 },   { "ISO-8859-7", 28597 },
            { "ISO-8859-8", 28598 },   { "ISO-8859-9", 28599 },
            { "KOI8-R", 20866 },       { "MAC-CENTRALEUROPE", 10029 },
            { "MAC-CYRILLIC", 10007 }, { "SHIFT_JIS", 932 },
            { "TIS-620", 874 },        { "UHC", 949 },
            { "UTF-8", 65001 },        { "VISCII", 1258 }, // no native Windows VISCII; CP1258 is closest
            { "WINDOWS-1250", 1250 },  { "WINDOWS-1251", 1251 },
            { "WINDOWS-1252", 1252 },  { "WINDOWS-1253", 1253 },
            { "WINDOWS-1255", 1255 },  { "WINDOWS-1256", 1256 },
            { "WINDOWS-1257", 1257 },  { "WINDOWS-1258", 1258 },
        };

        for (auto const& [name, cp] : table)
        {
            if (name == encoding)
            {
                return cp;
            }
        }
        return 0;
    }

private:
    uchardet_t ud_;
    Status lastStatus_;
    std::string_view lastEncoding_;
};
