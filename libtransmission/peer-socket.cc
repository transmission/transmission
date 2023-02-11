// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libutp/utp.h>

#include "transmission.h"

#include "peer-socket.h"
#include "net.h"

void tr_peer_socket::close(tr_session* session)
{
    if (is_tcp())
    {
        tr_netClose(session, handle.tcp);
    }
#ifdef WITH_UTP
    else if (is_utp())
    {
        utp_set_userdata(handle.utp, nullptr);
        utp_close(handle.utp);
    }
#endif
}
