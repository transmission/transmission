// This file Copyright © Transmission authors and contributors.
// It may be used under the 3-Clause BSD (SPDX: BSD-3-Clause),
// GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// C++ RAII wrapper for the POSIX iconv interface.

#pragma once

#include <cerrno>
#include <cstddef> // size_t
#include <string>
#include <string_view>
#include <vector>

// On FreeBSD (and other BSDs), iconv is built into libc. However, GNU libiconv
// may also be installed as a dependency of other ports (e.g. gettext, glib).
// If the GNU libiconv header gets picked up, it redefines iconv_open() etc.
// to libiconv_open() etc., causing link failures against libc. LIBICONV_PLUG
// disables that renaming so the standard POSIX symbols are always used.
// On Linux/glibc this define is a harmless no-op.
// See: https://github.com/transmission/transmission/issues/4547
//      FreeBSD ports Mk/Uses/iconv.mk
#ifndef LIBICONV_PLUG
#define LIBICONV_PLUG 1
#endif
#include <iconv.h>

class tr_charset_converter
{
public:
    tr_charset_converter(std::string_view to_encoding, std::string_view from_encoding)
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage) -- iconv_open requires null-terminated strings; callers ensure this
        : cd_(iconv_open(to_encoding.data(), from_encoding.data()))
    {
    }

    ~tr_charset_converter()
    {
        close();
    }

    tr_charset_converter(tr_charset_converter const&) = delete;
    tr_charset_converter& operator=(tr_charset_converter const&) = delete;

    tr_charset_converter(tr_charset_converter&& other) noexcept
        : cd_(other.cd_)
    {
        other.cd_ = InvalidCd;
    }

    tr_charset_converter& operator=(tr_charset_converter&& other) noexcept
    {
        if (this != &other)
        {
            close();
            cd_ = other.cd_;
            other.cd_ = InvalidCd;
        }
        return *this;
    }

    [[nodiscard]] std::string convert(std::string_view input)
    {
        if (cd_ == InvalidCd)
        {
            return {};
        }

        size_t in_bytes_left = input.size();
        size_t out_bytes_left = (input.size() * 4) + 4; // heuristic for expansion
        std::vector<char> output(out_bytes_left);

        // iconv() requires char*, but string_view::data() returns const char*
        char* in_buf = const_cast<char*>(input.data());
        char* out_buf = output.data();

        // Reset shift state
        if (iconv(cd_, nullptr, nullptr, nullptr, nullptr) == static_cast<size_t>(-1) && errno != 0)
        {
            return {};
        }

        size_t const result = iconv(cd_, &in_buf, &in_bytes_left, &out_buf, &out_bytes_left);
        if (result == static_cast<size_t>(-1))
        {
            return {};
        }

        return { output.data(), output.size() - out_bytes_left };
    }

    [[nodiscard]] bool is_valid() const noexcept
    {
        return cd_ != InvalidCd;
    }

private:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast,performance-no-int-to-ptr) -- POSIX iconv sentinel value
    static inline iconv_t const InvalidCd = (iconv_t)-1;

    iconv_t cd_ = InvalidCd;

    void close() noexcept
    {
        if (cd_ != InvalidCd)
        {
            iconv_close(cd_);
            cd_ = InvalidCd;
        }
    }
};
