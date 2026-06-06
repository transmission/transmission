// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility> // std::move

#include "libtransmission/types.h"

struct tr_error;

// Relocates a torrent's files from one parent directory to another on a
// dedicated background thread so that long, cross-filesystem copies do not
// block the session (libevent) thread. Jobs are run one at a time, in the
// order they were added. Modeled after tr_verify_worker.
class tr_move_worker
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        [[nodiscard]] virtual tr_sha1_digest_t const& info_hash() const = 0;

        virtual void on_move_queued() = 0;
        virtual void on_move_started() = 0;

        // Performs the blocking relocation. Called on the worker thread.
        [[nodiscard]] virtual bool move(tr_error* error) = 0;

        // Called on the worker thread once move() returns. Implementations are
        // expected to marshal any session-state changes back to the session thread.
        virtual void on_move_done(bool ok, tr_error const* error) = 0;
    };

    tr_move_worker() = default;
    ~tr_move_worker();

    tr_move_worker(tr_move_worker const&) = delete;
    tr_move_worker(tr_move_worker&&) = delete;
    tr_move_worker& operator=(tr_move_worker const&) = delete;
    tr_move_worker& operator=(tr_move_worker&&) = delete;

    void add(std::unique_ptr<Mediator> mediator);

    // If a job for `info_hash` is queued, drop it. If it is already running,
    // block until it finishes (a move in progress cannot be interrupted safely).
    void remove(tr_sha1_digest_t const& info_hash);

private:
    void move_thread_func();

    std::mutex move_mutex_;

    std::list<std::unique_ptr<Mediator>> todo_;
    std::unique_ptr<Mediator> current_;

    std::optional<std::thread::id> move_thread_id_;

    std::condition_variable done_current_cv_;
};
