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
#include <string_view>
#include <thread>
#include <utility> // std::move

#include "libtransmission/transmission.h"

#include "libtransmission/torrent-files.h"
#include "libtransmission/tr-macros.h"

class tr_relocate_worker
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        [[nodiscard]] virtual tr_torrent_files const& files() const = 0;
        [[nodiscard]] virtual std::string_view old_path() const = 0;
        [[nodiscard]] virtual std::string_view new_path() const = 0;
        [[nodiscard]] virtual std::string_view name() const = 0;
        [[nodiscard]] virtual tr_torrent_id_t torrent_id() const = 0;

        virtual void on_relocate_queued() = 0;
        virtual void on_relocate_started() = 0;
        virtual void on_file_relocated(tr_file_index_t file_index, bool success) = 0;
        virtual void on_relocate_done(bool aborted, std::optional<std::string> error_message) = 0;
    };

    tr_relocate_worker() = default;
    ~tr_relocate_worker();

    tr_relocate_worker(tr_relocate_worker const&) = delete;
    tr_relocate_worker(tr_relocate_worker&&) = delete;
    tr_relocate_worker& operator=(tr_relocate_worker const&) = delete;
    tr_relocate_worker& operator=(tr_relocate_worker&&) = delete;

    void add(std::unique_ptr<Mediator> mediator);

    void remove(tr_torrent_id_t tor_id);

private:
    struct Node
    {
        explicit Node(std::unique_ptr<Mediator> mediator) noexcept
            : mediator_{ std::move(mediator) }
        {
        }

        [[nodiscard]] int compare(Node const& that) const noexcept; // <=>

        [[nodiscard]] auto operator<(Node const& that) const noexcept
        {
            return compare(that) < 0;
        }

        [[nodiscard]] bool matches(tr_torrent_id_t tor_id) const noexcept
        {
            return mediator_->torrent_id() == tor_id;
        }

        std::unique_ptr<Mediator> mediator_;
    };

    static void relocate_torrent(Mediator& mediator, std::atomic<bool> const& abort_flag);

    void relocate_thread_func();

    std::mutex relocate_mutex_;

    std::set<Node> todo_;
    std::optional<Node> current_node_;

    std::optional<std::thread::id> relocate_thread_id_;

    std::atomic<bool> stop_current_ = false;
    std::condition_variable stop_current_cv_;
};
