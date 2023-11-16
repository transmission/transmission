// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility> // std::move

#include "libtransmission/transmission.h"

#include "libtransmission/torrent-metainfo.h"
#include "libtransmission/tr-macros.h"

class tr_verify_worker
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        [[nodiscard]] virtual tr_torrent_metainfo const& metainfo() const = 0;
        [[nodiscard]] virtual std::optional<std::string> find_file(tr_file_index_t file_index) const = 0;
        [[nodiscard]] virtual std::string download_dir() const = 0;

        virtual void on_verify_queued() = 0;
        virtual void on_verify_started() = 0;
        virtual void on_piece_checked(tr_piece_index_t piece, bool has_piece) = 0;
        virtual void on_verify_done(bool aborted) = 0;
    };

    ~tr_verify_worker();

    void add(std::unique_ptr<Mediator> mediator, tr_priority_t priority);

    void remove(tr_sha1_digest_t const& info_hash);

private:
    struct Node
    {
        Node(std::unique_ptr<Mediator> mediator, tr_priority_t priority) noexcept
            : mediator_{ std::move(mediator) }
            , priority_{ priority }
        {
        }

        [[nodiscard]] int compare(Node const& that) const noexcept; // <=>

        [[nodiscard]] auto operator<(Node const& that) const noexcept
        {
            return compare(that) < 0;
        }

        [[nodiscard]] bool matches(tr_sha1_digest_t const& info_hash) const noexcept
        {
            return mediator_->metainfo().info_hash() == info_hash;
        }

        std::unique_ptr<Mediator> mediator_;
        tr_priority_t priority_;
    };

    static void verify_torrent(Mediator& verify_mediator, bool abort_flag);

    void verify_thread_func();

    std::mutex verify_mutex_;

    std::set<Node> todo_;
    std::optional<Node> current_node_;

    std::optional<std::thread::id> verify_thread_id_;

    std::atomic<bool> stop_current_ = false;
    std::condition_variable stop_current_cv_;
};
