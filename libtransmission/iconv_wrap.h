// This file Copyright © Transmission authors and contributors.
// It may be used under the 3-Clause BSD (SPDX: BSD-3-Clause),
// GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// This file defines the C++ wrapper for the POSIX iconv interface.

#pragma once

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

class IconvWrapper
{
public:
    IconvWrapper(std::string_view toEncoding, std::string_view fromEncoding)
    {
        cd_ = iconv_open(toEncoding.data(), fromEncoding.data());
    }

    IconvWrapper(IconvWrapper const&) = delete;
    IconvWrapper& operator=(IconvWrapper const&) = delete;

    IconvWrapper(IconvWrapper&& other) noexcept
        : cd_(other.cd_)
    {
        other.cd_ = (iconv_t)-1;
    }
    IconvWrapper& operator=(IconvWrapper&& other) noexcept
    {
        if (this != &other)
        {
            close();
            cd_ = other.cd_;
            other.cd_ = (iconv_t)-1;
        }
        return *this;
    }

    ~IconvWrapper()
    {
        close();
    }

    std::string convert(std::string_view input)
    {
        if (cd_ == (iconv_t)-1)
        {
            return {};
        }

        size_t inBytesLeft = input.size();
        size_t outBytesLeft = input.size() * 4 + 4; // heuristic for expansion
        std::vector<char> output(outBytesLeft);

        // iconv() requires char*, but string_view::data() returns const char*
        char* inBuf = const_cast<char*>(input.data());
        char* outBuf = output.data();

        // Reset shift state
        if (iconv(cd_, nullptr, nullptr, nullptr, nullptr) == (size_t)-1 && errno != 0)
        {
            return {};
        }

        size_t result = iconv(cd_, &inBuf, &inBytesLeft, &outBuf, &outBytesLeft);
        if (result == (size_t)-1)
        {
            return {};
        }

        return std::string(output.data(), output.size() - outBytesLeft);
    }

    bool is_valid() const noexcept
    {
        return cd_ != (iconv_t)-1;
    }

private:
    iconv_t cd_{ (iconv_t)-1 };

    void close() noexcept
    {
        if (cd_ != (iconv_t)-1)
        {
            iconv_close(cd_);
            cd_ = (iconv_t)-1;
        }
    }
};
