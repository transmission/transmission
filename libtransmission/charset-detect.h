// This file Copyright © Transmission authors and contributors.
// It may be used under the 3-Clause BSD (SPDX: BSD-3-Clause),
// GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// C++ RAII wrapper for the Mozilla uchardet library.

#pragma once

#include <array>
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

    // Result of looking up an encoding name returned by uchardet.
    struct encoding_info
    {
        unsigned int codepage = 0; // Windows codepage number, or 0 if unknown
        std::string_view iconv_name; // portable iconv name (LCD across glibc, FreeBSD Citrus, etc.)
    };

    // Looks up an encoding name (as returned by uchardet v0.0.8) and returns
    // both the Windows codepage number and a portable iconv encoding name.
    // The iconv_name normalizes IBM*/WINDOWS-* to CP* form which is supported
    // by both glibc and FreeBSD's Citrus iconv.
    [[nodiscard]] static encoding_info lookup_encoding(std::string_view encoding) noexcept
    {
        struct encoding_entry
        {
            std::string_view uchardet;
            unsigned int codepage;
            std::string_view iconv;
        };
        // clang-format off
        auto constexpr Table = std::array<encoding_entry, 44>{{
            { "ASCII",            20127, "ASCII" },
            { "BIG5",               950, "BIG5" },
            { "EUC-JP",           20932, "EUC-JP" },
            { "EUC-KR",           51949, "EUC-KR" },
            { "EUC-TW",           51950, "EUC-TW" },
            { "GB18030",          54936, "GB18030" },
            { "HZ-GB-2312",       52936, "HZ-GB-2312" },        // not usually on FreeBSD/Citrus
            { "IBM852",             852, "CP852" },
            { "IBM855",             855, "CP855" },
            { "IBM865",             865, "CP865" },
            { "IBM866",             866, "CP866" },
            { "ISO-2022-CN",      50227, "ISO-2022-CN" },       // not usually on FreeBSD/Citrus
            { "ISO-2022-JP",      50220, "ISO-2022-JP" },
            { "ISO-2022-KR",      50225, "ISO-2022-KR" },
            { "ISO-8859-1",       28591, "ISO-8859-1" },
            { "ISO-8859-10",      28600, "ISO-8859-10" },
            { "ISO-8859-11",      28601, "ISO-8859-11" },
            { "ISO-8859-13",      28603, "ISO-8859-13" },
            { "ISO-8859-15",      28605, "ISO-8859-15" },
            { "ISO-8859-16",      28606, "ISO-8859-16" },
            { "ISO-8859-2",       28592, "ISO-8859-2" },
            { "ISO-8859-3",       28593, "ISO-8859-3" },
            { "ISO-8859-4",       28594, "ISO-8859-4" },
            { "ISO-8859-5",       28595, "ISO-8859-5" },
            { "ISO-8859-6",       28596, "ISO-8859-6" },
            { "ISO-8859-7",       28597, "ISO-8859-7" },
            { "ISO-8859-8",       28598, "ISO-8859-8" },
            { "ISO-8859-9",       28599, "ISO-8859-9" },
            { "KOI8-R",           20866, "KOI8-R" },
            { "MAC-CENTRALEUROPE",10029, "MAC-CENTRALEUROPE" }, // "MACCENTEURO" on FreeBSD/Citrus
            { "MAC-CYRILLIC",     10007, "MACCYRILLIC" },
            { "SHIFT_JIS",          932, "SHIFT_JIS" },
            { "TIS-620",            874, "TIS-620" },
            { "UHC",                949, "CP949" },
            { "UTF-8",            65001, "UTF-8" },
            { "VISCII",            1258, "VISCII" },
            { "WINDOWS-1250",      1250, "CP1250" },
            { "WINDOWS-1251",      1251, "CP1251" },
            { "WINDOWS-1252",      1252, "CP1252" },
            { "WINDOWS-1253",      1253, "CP1253" },
            { "WINDOWS-1255",      1255, "CP1255" },
            { "WINDOWS-1256",      1256, "CP1256" },
            { "WINDOWS-1257",      1257, "CP1257" },
            { "WINDOWS-1258",      1258, "CP1258" },
        }};
        // clang-format on

        for (auto const& entry : Table)
        {
            if (entry.uchardet == encoding)
            {
                return { .codepage = entry.codepage, .iconv_name = entry.iconv };
            }
        }
        return {};
    }

private:
    uchardet_t ud_;
    Status last_status_ = Status::OK;
    std::string_view last_encoding_;
};
