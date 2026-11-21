// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Persistent WebSocket tracker sessions for the WebTorrent announce protocol.
#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>

#include "libtransmission/announcer.h"

struct tr_announce_request;
struct tr_session;

class tr_announcer_ws final
{
public:
    explicit tr_announcer_ws(tr_session* session);
    ~tr_announcer_ws();

    tr_announcer_ws(tr_announcer_ws const&) = delete;
    tr_announcer_ws& operator=(tr_announcer_ws const&) = delete;
    tr_announcer_ws(tr_announcer_ws&&) = delete;
    tr_announcer_ws& operator=(tr_announcer_ws&&) = delete;

    void announce(tr_announce_request const& request, tr_announce_response_func on_response);
    void upkeep();

private:
    struct State;
    std::shared_ptr<State> state_;
};
