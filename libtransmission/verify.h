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
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include "libtransmission/transmission.h" // tr_piece_index_t
#include "libtransmission/torrent-metainfo.h" // tr_torrent_files::FoundFile

struct tr_torrent;

class tr_verify_worker
{
public:
    struct VerifyMediator
    {
        [[nodiscard]] virtual tr_torrent_metainfo const& metainfo() const = 0;
        [[nodiscard]] virtual time_t current_time() const = 0;
        [[nodiscard]] virtual std::optional<tr_torrent_files::FoundFile> find_file(tr_file_index_t file_index) const = 0;

        virtual void on_verify_started() = 0;
        virtual void on_piece_checked(tr_piece_index_t piece, bool has_piece) = 0;
        virtual void on_verify_done(bool aborted) = 0;
    };

    static void verify_torrent(VerifyMediator& verify_mediator, std::atomic<bool> const& abort_flag);

    ~tr_verify_worker();

    void add(tr_torrent* tor);

    void remove(tr_torrent* tor);

private:
    struct Node
    {
        tr_torrent* torrent = nullptr;
        uint64_t current_size = 0;

        [[nodiscard]] int compare(Node const& that) const;

        [[nodiscard]] bool operator<(Node const& that) const
        {
            return compare(that) < 0;
        }
    };

    void verify_thread_func();

    std::mutex verify_mutex_;

    std::set<Node> todo_;
    std::optional<Node> current_node_;

    std::optional<std::thread::id> verify_thread_id_;

    std::atomic<bool> stop_current_ = false;
    std::condition_variable stop_current_cv_;
};
