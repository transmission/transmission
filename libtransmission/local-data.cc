// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <ranges>
#include <stop_token>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "libtransmission/local-data.h"

#include "libtransmission/crypto-utils.h"
#include "libtransmission/inout.h"
#include "libtransmission/session.h"
#include "libtransmission/torrent.h"
#include "libtransmission/torrents.h"
#include "libtransmission/transmission.h"

namespace tr
{
namespace
{
[[nodiscard]] tr_error make_error(int err)
{
    auto error = tr_error{};
    if (err != 0)
    {
        error.set_from_errno(err);
    }

    return error;
}

[[nodiscard]] std::optional<tr_sha1_digest_t> recalculate_hash(
    LocalData::Backend& backend,
    tr_torrent_id_t const id,
    tr_block_info const block_info,
    tr_piece_index_t const piece)
{
    TR_ASSERT(piece < block_info.piece_count());

    auto sha = tr_sha1{};
    auto buffer = LocalData::BlockData{};

    auto const [begin_byte, end_byte] = block_info.byte_span_for_piece(piece);
    auto const [begin_block, end_block] = block_info.block_span_for_piece(piece);
    [[maybe_unused]] auto n_bytes_checked = size_t{};
    for (auto block = begin_block; block < end_block; ++block)
    {
        auto const byte_span = block_info.byte_span_for_block(block);

        buffer.clear();
        if (auto const err = backend.read(id, byte_span, buffer); err != 0)
        {
            return {};
        }

        auto begin = std::data(buffer);
        auto end = begin + byte_span.size();

        if (block == begin_block)
        {
            begin += (begin_byte - byte_span.begin);
        }
        if (block + 1U == end_block)
        {
            end -= (byte_span.end - end_byte);
        }

        sha.add(begin, end - begin);
        n_bytes_checked += (end - begin);
    }

    TR_ASSERT(block_info.piece_size(piece) == n_bytes_checked);
    return sha.finish();
}

class DefaultBackend final : public LocalData::Backend
{
public:
    DefaultBackend(tr_open_files& open_files, tr_torrents const& torrents)
        : open_files_{ open_files }
        , torrents_{ torrents }
    {
    }

    [[nodiscard]] int read(tr_torrent_id_t const id, tr_byte_span_t const byte_span, LocalData::BlockData& setme) override
    {
        if (!byte_span.is_valid())
        {
            return EINVAL;
        }

        auto const len = byte_span.size();
        if (len > tr_block_info::BlockSize)
        {
            return EINVAL;
        }

        auto const* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        auto const loc = tor->block_info().byte_loc(byte_span.begin);
        setme.resize(len);
        return tr_ioRead(*tor, loc, len, std::data(setme));
    }

    [[nodiscard]] int testPiece(tr_torrent_id_t const id, tr_piece_index_t const piece, tr_sha1_digest_t& setme_hash) override
    {
        auto const* const tor = torrents_.get(id);
        if (tor == nullptr || piece >= tor->piece_count())
        {
            return EINVAL;
        }

        auto const hash = recalculate_hash(*this, id, tor->block_info(), piece);
        if (!hash)
        {
            return EIO;
        }

        setme_hash = *hash;
        return 0;
    }

    [[nodiscard]] int write(tr_torrent_id_t const id, tr_byte_span_t const byte_span, LocalData::BlockData const& data) override
    {
        if (!byte_span.is_valid())
        {
            return EINVAL;
        }

        auto const len = byte_span.size();
        if (len > std::size(data))
        {
            return EINVAL;
        }

        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        auto const loc = tor->block_info().byte_loc(byte_span.begin);
        return tr_ioWrite(*tor, loc, len, std::data(data));
    }

    void close_torrent(tr_torrent_id_t const tor_id) override
    {
        open_files_.close_torrent(tor_id);
    }

    void close_file(tr_torrent_id_t const tor_id, tr_file_index_t const file_num) override
    {
        open_files_.close_file(tor_id, file_num);
    }

    [[nodiscard]] int move(
        tr_torrent_id_t const id,
        std::string_view const old_parent,
        std::string_view const parent,
        std::string_view const parent_name) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        auto error = tr_error{};
        if (tor->files().move(old_parent, parent, parent_name, &error))
        {
            return 0;
        }

        return error ? error.code() : EIO;
    }

    [[nodiscard]] int remove(tr_torrent_id_t const id, tr_torrent_remove_func remove_func) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        if (!remove_func)
        {
            remove_func = tr_sys_path_remove;
        }

        auto error = tr_error{};
        tor->files().remove(tor->current_dir(), tor->name(), remove_func, &error);
        return error ? error.code() : 0;
    }

    [[nodiscard]] int rename(tr_torrent_id_t const id, std::string_view const oldpath, std::string_view const newname) override
    {
        auto* const tor = torrents_.get(id);
        if (tor == nullptr)
        {
            return EINVAL;
        }

        auto promise = std::promise<int>{};
        auto future = promise.get_future();

        tr_torrentRenamePath(
            tor,
            oldpath,
            newname,
            [&promise](
                tr_torrent_id_t /*tor_id*/,
                std::string_view /*old_path*/,
                std::string_view /*new_path*/,
                tr_error const& error) { promise.set_value(error.code()); });

        return future.get();
    }

private:
    tr_open_files& open_files_;
    tr_torrents const& torrents_;
};
} // namespace

class LocalData::Impl
{
private:
    enum class Op : std::uint8_t
    {
        Read,
        Test,
        Write,
        CloseFile,
        CloseTorrent,
        Move,
        Remove,
        Rename
    };

    struct Task
    {
        tr_torrent_id_t id = -1;
        Op op = Op::Read;
        uint64_t write_bytes = 0;
        std::function<void()> run;
        std::function<void()> cancel;
    };

public:
    explicit Impl(std::unique_ptr<Backend> backend, size_t worker_count)
        : backend_{ std::move(backend) }
    {
        if (worker_count == 0U)
        {
            worker_count = std::max(1U, std::thread::hardware_concurrency());
        }

        workers_.reserve(worker_count);
        for (size_t i = 0; i < worker_count; ++i)
        {
            workers_.emplace_back([this](std::stop_token const& /*stop_token*/) { worker_thread(); });
        }
    }

    Impl(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl()
    {
        shutdown();
    }

    void read(tr_torrent_id_t id, tr_byte_span_t byte_span, OnRead on_read)
    {
        auto callback = std::make_shared<OnRead>(std::move(on_read));

        auto task = Task{};
        task.id = id;
        task.op = Op::Read;
        task.run = [this, id, byte_span, callback = callback]() mutable
        {
            auto data = std::make_unique<BlockData>();
            auto const err = backend_->read(id, byte_span, *data);
            if (err != 0)
            {
                data.reset();
            }
            (*callback)(id, byte_span, make_error(err), std::move(data));
        };
        task.cancel = [id, byte_span, callback = std::move(callback)]() mutable
        {
            (*callback)(id, byte_span, make_error(ECANCELED), nullptr);
        };

        enqueue(std::move(task));
    }

    void testPiece(tr_torrent_id_t id, tr_piece_index_t piece, OnTest on_test)
    {
        auto callback = std::make_shared<OnTest>(std::move(on_test));

        auto task = Task{};
        task.id = id;
        task.op = Op::Test;
        task.run = [this, id, piece, callback = callback]() mutable
        {
            auto hash = tr_sha1_digest_t{};
            auto const err = backend_->testPiece(id, piece, hash);
            (*callback)(id, piece, make_error(err), err == 0 ? std::optional<tr_sha1_digest_t>{ hash } : std::nullopt);
        };
        task.cancel = [id, piece, callback = std::move(callback)]() mutable
        {
            (*callback)(id, piece, make_error(ECANCELED), std::nullopt);
        };

        enqueue(std::move(task));
    }

    void write(tr_torrent_id_t id, tr_byte_span_t byte_span, std::unique_ptr<BlockData> data, OnWrite on_write)
    {
        auto callback = std::make_shared<OnWrite>(std::move(on_write));

        if (!byte_span.is_valid())
        {
            (*callback)(id, byte_span, make_error(EINVAL));
            return;
        }

        auto const len = byte_span.size();

        auto task = Task{};
        task.id = id;
        task.op = Op::Write;
        task.write_bytes = len;

        if (data == nullptr || len > std::size(*data))
        {
            (*callback)(id, byte_span, make_error(EINVAL));
            return;
        }

        auto write_data = std::make_shared<BlockData>(std::move(*data));

        task.run = [this, id, byte_span, write_data = std::move(write_data), callback = callback]() mutable
        {
            auto const err = backend_->write(id, byte_span, *write_data);
            (*callback)(id, byte_span, make_error(err));
        };
        task.cancel = [id, byte_span, callback = std::move(callback)]() mutable
        {
            (*callback)(id, byte_span, make_error(ECANCELED));
        };

        enqueue(std::move(task));
    }

    void close_torrent(tr_torrent_id_t const tor_id)
    {
        auto task = Task{};
        task.id = tor_id;
        task.op = Op::CloseTorrent;
        task.run = [this, tor_id]()
        {
            backend_->close_torrent(tor_id);
        };

        enqueue(std::move(task));
    }

    void close_file(tr_torrent_id_t const tor_id, tr_file_index_t const file_num)
    {
        auto task = Task{};
        task.id = tor_id;
        task.op = Op::CloseFile;
        task.run = [this, tor_id, file_num]()
        {
            backend_->close_file(tor_id, file_num);
        };

        enqueue(std::move(task));
    }

    void rename(
        tr_torrent_id_t const tor_id,
        std::string_view oldpath,
        std::string_view newname,
        tr_torrent_rename_done_func callback)
    {
        auto oldpath_buf = std::string{ oldpath };
        auto newname_buf = std::string{ newname };
        auto callback_ptr = std::make_shared<tr_torrent_rename_done_func>(std::move(callback));

        auto task = Task{};
        task.id = tor_id;
        task.op = Op::Rename;
        task.run = [this, tor_id, oldpath = oldpath_buf, newname = newname_buf, callback = callback_ptr]() mutable
        {
            backend_->close_torrent(tor_id);

            auto const err = backend_->rename(tor_id, oldpath, newname);
            if (*callback != nullptr)
            {
                (*callback)(tor_id, oldpath, newname, make_error(err));
            }
        };
        task.cancel = [tor_id,
                       oldpath = std::move(oldpath_buf),
                       newname = std::move(newname_buf),
                       callback = std::move(callback_ptr)]() mutable
        {
            if (*callback != nullptr)
            {
                (*callback)(tor_id, oldpath, newname, make_error(ECANCELED));
            }
        };

        enqueue(std::move(task));
    }

    void move(tr_torrent_id_t const tor_id, std::string_view old_parent, std::string_view parent, std::string_view parent_name)
    {
        auto task = Task{};
        task.id = tor_id;
        task.op = Op::Move;
        task.run = [this,
                    tor_id,
                    old_parent = std::string{ old_parent },
                    parent = std::string{ parent },
                    parent_name = std::string{ parent_name }]()
        {
            backend_->close_torrent(tor_id);
            static_cast<void>(backend_->move(tor_id, old_parent, parent, parent_name));
        };

        enqueue(std::move(task));
    }

    void remove(tr_torrent_id_t const tor_id, tr_torrent_remove_func remove_func)
    {
        auto canceled_callbacks = std::vector<std::function<void()>>{};

        auto task = Task{};
        task.id = tor_id;
        task.op = Op::Remove;
        task.run = [this, tor_id, remove_func = std::move(remove_func)]() mutable
        {
            backend_->close_torrent(tor_id);
            static_cast<void>(backend_->remove(tor_id, std::move(remove_func)));
        };

        {
            auto const lock = std::lock_guard(mutex_);

            auto& queue = queues_[tor_id];
            auto it = std::begin(queue);
            while (it != std::end(queue))
            {
                auto const should_discard = it->op == Op::Read || it->op == Op::Write || it->op == Op::Rename ||
                    it->op == Op::Move || it->op == Op::Test;
                if (!should_discard)
                {
                    ++it;
                    continue;
                }

                if (it->op == Op::Write)
                {
                    enqueued_write_bytes_ -= it->write_bytes;
                }

                if (!is_read_like(it->op))
                {
                    --pending_non_read_;
                }

                if (it->cancel)
                {
                    canceled_callbacks.emplace_back(std::move(it->cancel));
                }

                it = queue.erase(it);
            }

            auto const was_empty = std::empty(queue);
            queue.emplace_back(std::move(task));
            if (was_empty)
            {
                runnable_ids_.push_back(tor_id);
            }
        }

        for (auto& canceled_callback : canceled_callbacks)
        {
            canceled_callback();
        }

        cv_.notify_one();
    }

    void shutdown()
    {
        auto canceled_callbacks = std::vector<std::function<void()>>{};

        {
            auto lock = std::unique_lock(mutex_);
            if (stopping_workers_)
            {
                return;
            }

            shutting_down_ = true;

            for (auto& [id, queue] : queues_)
            {
                static_cast<void>(id);

                auto it = std::begin(queue);
                while (it != std::end(queue))
                {
                    if (is_read_like(it->op))
                    {
                        if (it->cancel)
                        {
                            canceled_callbacks.emplace_back(std::move(it->cancel));
                        }

                        it = queue.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            shutdown_cv_.wait(lock, [this]() { return pending_non_read_ == 0U && active_non_read_ == 0U; });
            stopping_workers_ = true;
        }

        for (auto& callback : canceled_callbacks)
        {
            callback();
        }

        cv_.notify_all();

        for (auto& worker : workers_)
        {
            worker.request_stop();
        }
        workers_.clear();
    }

    [[nodiscard]] uint64_t enqueued_write_bytes() const
    {
        auto const lock = std::lock_guard(mutex_);
        return enqueued_write_bytes_;
    }

private:
    void enqueue(Task task)
    {
        auto cancel = std::function<void()>{};

        {
            auto const lock = std::lock_guard(mutex_);

            if (shutting_down_ && is_read_like(task.op))
            {
                cancel = std::move(task.cancel);
            }
            else
            {
                if (task.op == Op::Write)
                {
                    enqueued_write_bytes_ += task.write_bytes;
                }

                if (!is_read_like(task.op))
                {
                    ++pending_non_read_;
                }

                auto& queue = queues_[task.id];
                queue.emplace_back(std::move(task));
                if (std::size(queue) == 1U)
                {
                    runnable_ids_.push_back(queue.front().id);
                }
            }
        }

        if (cancel)
        {
            cancel();
            return;
        }

        cv_.notify_one();
    }

    [[nodiscard]] bool has_runnable_task_unlocked() const
    {
        return std::ranges::any_of(
            runnable_ids_,
            [this](tr_torrent_id_t const id)
            { return !active_ids_.contains(id) && queues_.contains(id) && !std::empty(queues_.at(id)); });
    }

    [[nodiscard]] bool dequeue_next_task_unlocked(Task& setme)
    {
        while (!std::empty(runnable_ids_))
        {
            auto const id = runnable_ids_.front();
            runnable_ids_.pop_front();

            if (active_ids_.contains(id))
            {
                continue;
            }

            auto it = queues_.find(id);
            if (it == std::end(queues_) || std::empty(it->second))
            {
                continue;
            }

            setme = std::move(it->second.front());
            it->second.pop_front();
            active_ids_.insert(id);

            if (!is_read_like(setme.op))
            {
                --pending_non_read_;
                ++active_non_read_;
            }

            if (std::empty(it->second))
            {
                queues_.erase(it);
            }

            return true;
        }

        return false;
    }

    void worker_thread()
    {
        while (true)
        {
            auto task = Task{};

            {
                auto lock = std::unique_lock(mutex_);
                cv_.wait(lock, [this]() { return stopping_workers_ || has_runnable_task_unlocked(); });

                if (stopping_workers_)
                {
                    return;
                }

                if (!dequeue_next_task_unlocked(task))
                {
                    continue;
                }
            }

            if (task.run)
            {
                task.run();
            }

            {
                auto const lock = std::lock_guard(mutex_);
                active_ids_.erase(task.id);

                if (task.op == Op::Write)
                {
                    enqueued_write_bytes_ -= task.write_bytes;
                }

                if (!is_read_like(task.op))
                {
                    --active_non_read_;
                    if (shutting_down_ && pending_non_read_ == 0U && active_non_read_ == 0U)
                    {
                        shutdown_cv_.notify_all();
                    }
                }

                if (auto it = queues_.find(task.id); it != std::end(queues_) && !std::empty(it->second))
                {
                    runnable_ids_.push_back(task.id);
                }
            }

            cv_.notify_one();
        }
    }

    [[nodiscard]] static bool is_read_like(Op const op)
    {
        return op == Op::Read || op == Op::Test;
    }

    std::unique_ptr<Backend> backend_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable shutdown_cv_;

    std::unordered_map<tr_torrent_id_t, std::deque<Task>> queues_;
    std::deque<tr_torrent_id_t> runnable_ids_;
    std::unordered_set<tr_torrent_id_t> active_ids_;
    std::vector<std::jthread> workers_;

    uint64_t enqueued_write_bytes_ = 0;
    size_t pending_non_read_ = 0;
    size_t active_non_read_ = 0;

    bool shutting_down_ = false;
    bool stopping_workers_ = false;
};

LocalData::LocalData(tr_open_files& open_files, tr_torrents const& torrents, size_t worker_count)
    : impl_{ std::make_unique<Impl>(std::make_unique<DefaultBackend>(open_files, torrents), worker_count) }
{
}

LocalData::LocalData(std::unique_ptr<Backend> backend, size_t worker_count)
    : impl_{ std::make_unique<Impl>(std::move(backend), worker_count) }
{
}

LocalData::~LocalData() = default;

void LocalData::read(tr_torrent_id_t const id, tr_byte_span_t const byte_span, OnRead on_read)
{
    impl_->read(id, byte_span, std::move(on_read));
}

void LocalData::testPiece(tr_torrent_id_t const id, tr_piece_index_t const piece, OnTest on_test)
{
    impl_->testPiece(id, piece, std::move(on_test));
}

void LocalData::write(
    tr_torrent_id_t const id,
    tr_byte_span_t const byte_span,
    std::unique_ptr<BlockData> data,
    OnWrite on_write)
{
    impl_->write(id, byte_span, std::move(data), std::move(on_write));
}

void LocalData::close_torrent(tr_torrent_id_t const tor_id)
{
    impl_->close_torrent(tor_id);
}

void LocalData::close_file(tr_torrent_id_t const tor_id, tr_file_index_t const file_num)
{
    impl_->close_file(tor_id, file_num);
}

void LocalData::rename(
    tr_torrent_id_t const id,
    std::string_view const oldpath,
    std::string_view const newname,
    tr_torrent_rename_done_func callback)
{
    impl_->rename(id, oldpath, newname, std::move(callback));
}

void LocalData::move(
    tr_torrent_id_t const id,
    std::string_view const old_parent,
    std::string_view const parent,
    std::string_view const parent_name)
{
    impl_->move(id, old_parent, parent, parent_name);
}

void LocalData::remove(tr_torrent_id_t const id, tr_torrent_remove_func remove_func)
{
    impl_->remove(id, std::move(remove_func));
}

void LocalData::shutdown()
{
    impl_->shutdown();
}

uint64_t LocalData::enqueued_write_bytes() const
{
    return impl_->enqueued_write_bytes();
}

} // namespace tr
