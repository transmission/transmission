// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/error.h"
#include "libtransmission/utils.h"

void tr_error::set_from_errno(int errnum)
{
    code_ = errnum;

    message_ = errnum != 0 ? tr_strerror(errnum) : "";
}
