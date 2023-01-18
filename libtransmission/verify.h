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

struct tr_session;
struct tr_torrent;

class tr_verify_worker
{
public:
    using callback_func = std::function<void(tr_torrent*, bool aborted)>;

    ~tr_verify_worker();

    void addCallback(callback_func callback)
    {
        callbacks_.emplace_back(std::move(callback));
    }

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

    void callCallback(tr_torrent* tor, bool aborted) const
    {
        for (auto const& callback : callbacks_)
        {
            callback(tor, aborted);
        }
    }

    void verifyThreadFunc();
    [[nodiscard]] static bool verifyTorrent(tr_torrent* tor, std::atomic<bool> const& stop_flag);

    std::list<callback_func> callbacks_;
    std::mutex verify_mutex_;

    std::set<Node> todo_;
    std::optional<Node> current_node_;

    std::optional<std::thread::id> verify_thread_id_;

    std::atomic<bool> stop_current_ = false;
    std::condition_variable stop_current_cv_;
};
