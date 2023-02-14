// This file Copyright © 2013-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <string_view>

#include <fmt/format.h>

#include "transmission.h"

#include "error.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "utils.h"

namespace
{
[[nodiscard]] char* tr_strvdup(std::string_view in)
{
    auto const n = std::size(in);
    auto* const ret = new char[n + 1];
    std::copy(std::begin(in), std::end(in), ret);
    ret[n] = '\0';
    return ret;
}
} // namespace

void tr_error_free(tr_error* error)
{
    if (error == nullptr)
    {
        return;
    }

    delete[] error->message;
    delete error;
}

void tr_error_set(tr_error** error, int code, std::string_view message)
{
    if (error == nullptr)
    {
        return;
    }

    TR_ASSERT(*error == nullptr);
    *error = new tr_error{ code, tr_strvdup(message) };
}

void tr_error_set_from_errno(tr_error** error, int errnum)
{
    tr_error_set(error, errnum, tr_strerror(errnum));
}

void tr_error_propagate(tr_error** new_error, tr_error** old_error)
{
    TR_ASSERT(old_error != nullptr);
    TR_ASSERT(*old_error != nullptr);

    if (new_error != nullptr)
    {
        TR_ASSERT(*new_error == nullptr);

        *new_error = *old_error;
        *old_error = nullptr;
    }
    else
    {
        tr_error_clear(old_error);
    }
}

void tr_error_clear(tr_error** error)
{
    if (error == nullptr)
    {
        return;
    }

    tr_error_free(*error);

    *error = nullptr;
}

void tr_error_prefix(tr_error** error, char const* prefix)
{
    TR_ASSERT(prefix != nullptr);

    if (error == nullptr || *error == nullptr)
    {
        return;
    }

    auto* err = *error;
    auto* const new_message = tr_strvdup(fmt::format(FMT_STRING("{:s}{:s}"), prefix, err->message));
    delete[] err->message;
    err->message = new_message;
}
