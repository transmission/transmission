// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// WebRTC offer/answer lifecycle, isolated from WebSocket tracker transport.
#pragma once
#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "libtransmission/types.h"
#include "libtransmission/announcer-ws-protocol.h"

struct tr_session;

class tr_rtc_signaling final
{
public:
    using OffersReady = std::function<void(std::vector<tr_ws_tracker::Offer>)>;
    using SendAnswer = std::function<void(std::string)>;

    explicit tr_rtc_signaling(tr_session* session);
    ~tr_rtc_signaling();
    tr_rtc_signaling(tr_rtc_signaling const&) = delete;
    tr_rtc_signaling& operator=(tr_rtc_signaling const&) = delete;
    tr_rtc_signaling(tr_rtc_signaling&&) = delete;
    tr_rtc_signaling& operator=(tr_rtc_signaling&&) = delete;

    void make_offers(tr_sha1_digest_t const& hash, std::string const& tracker_url, size_t count, OffersReady on_ready);
    void on_signal(
        tr_ws_tracker::Signal const& signal,
        std::string const& tracker_url,
        std::string const& our_peer_id,
        SendAnswer send_answer);
    void upkeep();

private:
    struct State;
    std::shared_ptr<State> state_;
};
