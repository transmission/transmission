/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"

#include "error.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "utils.h"

void tr_error_free(tr_error* error)
{
    if (error == nullptr)
    {
        return;
    }

    tr_free(error->message);
    delete error;
}

void tr_error_set(tr_error** error, int code, std::string_view message)
{
    if (error == nullptr)
    {
        return;
    }

    TR_ASSERT(*error == nullptr);
    *error = new tr_error{ code, tr_strvDup(message) };
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
    auto* const new_message = tr_strvDup(tr_strvJoin(prefix, err->message));
    tr_free(err->message);
    err->message = new_message;
}
