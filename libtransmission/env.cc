// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/env.h"

#include <cstdlib>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "lib/base/string-utils.h"
#include "lib/base/tr-assert.h"
#include "libtransmission/tr-strbuf.h"

bool tr_env_key_exists(char const* key) noexcept
{
    TR_ASSERT(key != nullptr);

#ifdef _WIN32
    return GetEnvironmentVariableA(key, nullptr, 0) != 0;
#else
    return getenv(key) != nullptr;
#endif
}

std::string tr_env_get_string(std::string_view key, std::string_view default_value)
{
#ifdef _WIN32

    if (auto const wide_key = tr_win32_utf8_to_native(key); !std::empty(wide_key))
    {
        if (auto const size = GetEnvironmentVariableW(wide_key.c_str(), nullptr, 0); size != 0)
        {
            auto wide_val = std::wstring{};
            wide_val.resize(size);
            if (GetEnvironmentVariableW(wide_key.c_str(), std::data(wide_val), std::size(wide_val)) == std::size(wide_val) - 1)
            {
                TR_ASSERT(wide_val.back() == L'\0');
                wide_val.resize(std::size(wide_val) - 1);
                return tr_win32_native_to_utf8(wide_val);
            }
        }
    }

#else

    auto const szkey = tr_strbuf<char, 256>{ key };

    if (auto const* const value = getenv(szkey); value != nullptr)
    {
        return value;
    }

#endif

    return std::string{ default_value };
}
