// This file Copyright © Transmission authors and contributors.
// It may be used under the 3-Clause BSD (SPDX: BSD-3-Clause),
// GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// C++ RAII wrapper for the Mozilla uchardet library.

#pragma once

#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <string>
#include <string_view>
#include <utility> // std::pair

#include <uchardet/uchardet.h>

class tr_charset_detector
{
public:
    enum class Status : uint8_t
    {
        OK = 0,
        InitFailed,
        InvalidInput,
        ProcessError,
        FileError,
        UnknownEncoding
    };

    tr_charset_detector() noexcept
        : ud_(uchardet_new())
    {
        if (ud_ == nullptr)
        {
            last_status_ = Status::InitFailed;
        }
    }

    ~tr_charset_detector()
    {
        if (ud_ != nullptr)
        {
            uchardet_delete(ud_);
        }
    }

    tr_charset_detector(tr_charset_detector const&) = delete;
    tr_charset_detector& operator=(tr_charset_detector const&) = delete;

    tr_charset_detector(tr_charset_detector&& other) noexcept
        : ud_(other.ud_)
        , last_status_(other.last_status_)
        , last_encoding_(std::move(other.last_encoding_))
    {
        other.ud_ = nullptr;
    }

    tr_charset_detector& operator=(tr_charset_detector&& other) noexcept
    {
        if (this != &other)
        {
            if (ud_ != nullptr)
            {
                uchardet_delete(ud_);
            }
            ud_ = other.ud_;
            last_status_ = other.last_status_;
            last_encoding_ = std::move(other.last_encoding_);
            other.ud_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] Status detect_from_buffer(char const* data, size_t length) noexcept
    {
        if (ud_ == nullptr)
        {
            return Status::InitFailed;
        }
        if (data == nullptr || length == 0)
        {
            return Status::InvalidInput;
        }

        uchardet_reset(ud_);
        if (uchardet_handle_data(ud_, data, length) != 0)
        {
            last_status_ = Status::ProcessError;
            return last_status_;
        }

        uchardet_data_end(ud_);
        char const* charset = uchardet_get_charset(ud_);
        if (charset == nullptr || charset[0] == '\0')
        {
            last_encoding_ = {};
            last_status_ = Status::UnknownEncoding;
        }
        else
        {
            last_encoding_ = charset;
            last_status_ = Status::OK;
        }
        return last_status_;
    }

    [[nodiscard]] Status detect_from_string(std::string_view const text) noexcept
    {
        return detect_from_buffer(text.data(), text.size());
    }

    [[nodiscard]] Status detect_from_string(std::string const& text) noexcept
    {
        return detect_from_buffer(text.data(), text.size());
    }

    [[nodiscard]] std::string_view encoding() const noexcept
    {
        return last_encoding_;
    }

    [[nodiscard]] Status last_status() const noexcept
    {
        return last_status_;
    }

    // Returns the Windows codepage number for the detected encoding,
    // or 0 if unknown. Covers all encodings that uchardet v0.0.8 can return.
    [[nodiscard]] static unsigned int encoding_to_codepage(std::string_view encoding) noexcept
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
    Status last_status_ = Status::OK;
    std::string_view last_encoding_;
};
