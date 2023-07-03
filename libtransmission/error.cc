// This file Copyright © 2013-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/transmission.h"

#include "libtransmission/error.h"

#include "libtransmission/net.h" // for tr_net_strerror()
#include "libtransmission/utils.h" // for tr_strerror()

void tr_error::ensure_message() const
{
    if (!std::empty(message_))
    {
        return;
    }

    if (type_ == Type::Errno)
    {
        message_ = tr_strerror(code_);
    }
    else if (type_ == Type::SockErrno)
    {
        message_ = tr_net_strerror(code_);
    }
}

void tr_error_set(tr_error* error, int error_code, std::string_view message)
{
    if (error != nullptr)
    {
        error->set(error_code, message);
    }
}

void tr_error_set_from_errno(tr_error* error, int error_code)
{
    if (error != nullptr)
    {
        error->set_from_errno(error_code);
    }
}

void tr_error_set_from_sockerrno(tr_error* error, int error_code)
{
    if (error != nullptr)
    {
        error->set_from_sockerrno(error_code);
    }
}

void tr_error_propagate(tr_error* tgt, tr_error&& src)
{
    if (tgt != nullptr)
    {
        *tgt = std::move(src);
    }
}

void tr_error_prefix(tr_error* error, std::string_view prefix)
{
    if (error != nullptr)
    {
        error->prefix(prefix);
    }
}

bool tr_error_is_set(tr_error const* const error)
{
    return error != nullptr && error->is_set();
}
