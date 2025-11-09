// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <ctime>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/net.h" // for tr_address, tr_port

struct event_base;

namespace libtransmission
{
class TimerMaker;
}

class tr_lpd
{
public:
    class EventHandler
    {
    public:
        using Callback = std::function<void(tr_socket_t)>;
        explicit EventHandler(Callback callback)
            : callback_(std::move(callback))
        {
        }
        virtual ~EventHandler() = default;

        virtual void start() = 0;
        virtual void stop() = 0;

    protected:
        Callback callback_;
    };

    class tr_lpd_libevent_handler : public EventHandler
    {
    public:
        tr_lpd_libevent_handler(tr_session& session, tr_socket_t socket, Callback callback);
        ~tr_lpd_libevent_handler() override;
        void start() override;
        void stop() override;

    private:
        static void on_udp_readable(tr_socket_t s, short type, void* vself);
        tr_socket_t socket_ = TR_BAD_SOCKET;
        struct event* socket_event_ = nullptr;
    };

    class tr_lpd_libuv_handler : public EventHandler
    {
    public:
        tr_lpd_libuv_handler(tr_session& session, tr_socket_t socket, Callback callback);
        ~tr_lpd_libuv_handler() override;
        void start() override;
        void stop() override;

    private:
        static void on_udp_readable(struct uv_poll_s* handle, int status, int events);
        tr_socket_t socket_ = TR_BAD_SOCKET;
        struct uv_poll_s* socket_poll_ = nullptr;
    };

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

        [[nodiscard]] virtual std::unique_ptr<EventHandler> createEventHandler(tr_socket_t socket, EventHandler::Callback callback) = 0;

        virtual void setNextAnnounceTime(std::string_view info_hash_str, time_t announce_at) = 0;

        // returns true if info was used
        virtual bool onPeerFound(std::string_view info_hash_str, tr_address address, tr_port port) = 0;
    };

    virtual ~tr_lpd() = default;
    static std::unique_ptr<tr_lpd> create(Mediator& mediator);
};
