// This file Copyright 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include "libtransmission/transmission.h" // tr_piece_index_t
#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/tr-macros.h" // tr_info_hash_t

class tr_verify_worker
{
public:
    struct VerifyMediator
    {
        virtual ~VerifyMediator() = default;

        [[nodiscard]] virtual tr_torrent_metainfo const& metainfo() const = 0;
        [[nodiscard]] virtual std::optional<std::string> find_file(tr_file_index_t file_index) const = 0;

        virtual void on_verify_queued() = 0;
        virtual void on_verify_started() = 0;
        virtual void on_piece_checked(tr_piece_index_t piece, bool has_piece) = 0;
        virtual void on_verify_done(bool aborted) = 0;
    };

    ~tr_verify_worker();

    void add(std::unique_ptr<VerifyMediator> mediator, tr_priority_t priority);

    void remove(tr_sha1_digest_t const& info_hash);

private:
    struct Node
    {
        Node(std::unique_ptr<VerifyMediator> mediator, tr_priority_t priority)
            : mediator_{ std::move(mediator) }
            , priority_{ priority }
        {
        }

        [[nodiscard]] int compare(Node const& that) const // <=>
        {
            // prefer higher-priority torrents
            if (priority_ != that.priority_)
            {
                return priority_ > that.priority_ ? 1 : -1;
            }

            // prefer smaller torrents, since they will verify faster
            auto const& metainfo = mediator().metainfo();
            auto const& that_metainfo = that.mediator().metainfo();
            if (metainfo.total_size() != that_metainfo.total_size())
            {
                return metainfo.total_size() < that_metainfo.total_size() ? 1 : -1;
            }

            // uniqueness check
            auto const& this_hash = mediator().metainfo().info_hash();
            auto const& that_hash = that.mediator().metainfo().info_hash();
            if (this_hash != that_hash)
            {
                return this_hash < that_hash ? 1 : -1;
            }

            return 0;
        }

        [[nodiscard]] bool operator<(Node const& that) const
        {
            return compare(that) < 0;
        }

        [[nodiscard]] constexpr bool matches(tr_sha1_digest_t const& info_hash) const noexcept
        {
            return mediator().metainfo().info_hash() == info_hash;
        }

        [[nodiscard]] constexpr VerifyMediator& mediator() const noexcept
        {
            return *mediator_;
        }

        std::unique_ptr<VerifyMediator> mediator_;
        tr_priority_t priority_;
    };

    static void verify_torrent(VerifyMediator& verify_mediator, std::atomic<bool> const& abort_flag);

    void verify_thread_func();

    std::mutex verify_mutex_;

    std::set<Node> todo_;
    std::optional<Node> current_node_;

    std::optional<std::thread::id> verify_thread_id_;

    std::atomic<bool> stop_current_ = false;
    std::condition_variable stop_current_cv_;
};
