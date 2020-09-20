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
#include "utils.h"

tr_error* tr_error_new_literal(int code, char const* message)
{
    TR_ASSERT(message != NULL);

    tr_error* error = tr_new(tr_error, 1);
    error->code = code;
    error->message = tr_strdup(message);

    return error;
}

tr_error* tr_error_new_valist(int code, char const* message_format, va_list args)
{
    TR_ASSERT(message_format != NULL);

    tr_error* error = tr_new(tr_error, 1);
    error->code = code;
    error->message = tr_strdup_vprintf(message_format, args);

    return error;
}

void tr_error_free(tr_error* error)
{
    if (error == NULL)
    {
        return;
    }

    tr_free(error->message);
    tr_free(error);
}

void tr_error_set(tr_error** error, int code, char const* message_format, ...)
{
    TR_ASSERT(message_format != NULL);

    if (error == NULL)
    {
        return;
    }

    TR_ASSERT(*error == NULL);

    va_list args;

    va_start(args, message_format);
    *error = tr_error_new_valist(code, message_format, args);
    va_end(args);
}

void tr_error_set_literal(tr_error** error, int code, char const* message)
{
    TR_ASSERT(message != NULL);

    if (error == NULL)
    {
        return;
    }

    TR_ASSERT(*error == NULL);

    *error = tr_error_new_literal(code, message);
}

void tr_error_propagate(tr_error** new_error, tr_error** old_error)
{
    TR_ASSERT(old_error != NULL);
    TR_ASSERT(*old_error != NULL);

    if (new_error != NULL)
    {
        TR_ASSERT(*new_error == NULL);

        *new_error = *old_error;
        *old_error = NULL;
    }
    else
    {
        tr_error_clear(old_error);
    }
}

void tr_error_clear(tr_error** error)
{
    if (error == NULL)
    {
        return;
    }

    tr_error_free(*error);

    *error = NULL;
}

static void error_prefix_valist(tr_error** error, char const* prefix_format, va_list args) TR_GNUC_PRINTF(2, 0);

static void error_prefix_valist(tr_error** error, char const* prefix_format, va_list args)
{
    TR_ASSERT(error != NULL);
    TR_ASSERT(*error != NULL);
    TR_ASSERT(prefix_format != NULL);

    char* prefix = tr_strdup_vprintf(prefix_format, args);

    char* new_message = tr_strdup_printf("%s%s", prefix, (*error)->message);
    tr_free((*error)->message);
    (*error)->message = new_message;

    tr_free(prefix);
}

void tr_error_prefix(tr_error** error, char const* prefix_format, ...)
{
    TR_ASSERT(prefix_format != NULL);

    if (error == NULL || *error == NULL)
    {
        return;
    }

    va_list args;

    va_start(args, prefix_format);
    error_prefix_valist(error, prefix_format, args);
    va_end(args);
}

void tr_error_propagate_prefixed(tr_error** new_error, tr_error** old_error, char const* prefix_format, ...)
{
    TR_ASSERT(prefix_format != NULL);

    tr_error_propagate(new_error, old_error);

    if (new_error == NULL)
    {
        return;
    }

    va_list args;

    va_start(args, prefix_format);
    error_prefix_valist(new_error, prefix_format, args);
    va_end(args);
}
