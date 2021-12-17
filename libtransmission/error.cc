/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstdarg>

#include "transmission.h"

#include "error.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "utils.h"

tr_error* tr_error_new_literal(int code, char const* message)
{
    TR_ASSERT(message != nullptr);

    auto* const error = tr_new(tr_error, 1);
    error->code = code;
    error->message = tr_strdup(message);

    return error;
}

static tr_error* tr_error_new_valist(int code, char const* message_format, va_list args) TR_GNUC_PRINTF(2, 0);

static tr_error* tr_error_new_valist(int code, char const* message_format, va_list args)
{
    TR_ASSERT(message_format != nullptr);

    auto* const error = tr_new(tr_error, 1);
    error->code = code;
    error->message = tr_strdup_vprintf(message_format, args);

    return error;
}

void tr_error_free(tr_error* error)
{
    if (error == nullptr)
    {
        return;
    }

    tr_free(error->message);
    tr_free(error);
}

void tr_error_set(tr_error** error, int code, char const* message_format, ...)
{
    TR_ASSERT(message_format != nullptr);

    if (error == nullptr)
    {
        return;
    }

    TR_ASSERT(*error == nullptr);

    va_list args;

    va_start(args, message_format);
    *error = tr_error_new_valist(code, message_format, args);
    va_end(args);
}

void tr_error_set_literal(tr_error** error, int code, char const* message)
{
    TR_ASSERT(message != nullptr);

    if (error == nullptr)
    {
        return;
    }

    TR_ASSERT(*error == nullptr);

    *error = tr_error_new_literal(code, message);
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
