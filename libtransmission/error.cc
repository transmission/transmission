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
