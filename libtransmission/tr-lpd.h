// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <ctime>
#include <memory>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "net.h" // for tr_address, tr_port

struct event_base;

namespace libtransmission
{
class TimerMaker;
}

class tr_lpd
{
public:
    class Mediator
    {
    public:
        struct TorrentInfo
        {
            std::string_view info_hash_str;
            tr_torrent_activity activity;
            bool allows_lpd;
            time_t announce_after;
        };

        virtual ~Mediator() = default;

        [[nodiscard]] virtual tr_address bind_address(tr_address_type type) const = 0;

        [[nodiscard]] virtual tr_port port() const = 0;

        [[nodiscard]] virtual bool allowsLPD() const = 0;

        [[nodiscard]] virtual std::vector<TorrentInfo> torrents() const = 0;

        [[nodiscard]] virtual libtransmission::TimerMaker& timerMaker() = 0;

        virtual void setNextAnnounceTime(std::string_view info_hash_str, time_t announce_at) = 0;

        // returns true if info was used
        virtual bool onPeerFound(std::string_view info_hash_str, tr_address address, tr_port port) = 0;
    };

    virtual ~tr_lpd() = default;
    static std::unique_ptr<tr_lpd> create(Mediator& mediator, event_base* event_base);
};
