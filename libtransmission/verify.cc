// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef> // std::byte
#include <cstdint> // uint64_t, uint32_t
#include <memory>
#include <mutex>
#include <thread>
#include <utility> // for std::move()
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/crypto-utils.h"
#include <libtransmission/error.h>
#include "libtransmission/file.h"
#include "libtransmission/tr-macros.h"
#include "libtransmission/verify.h"

using namespace std::chrono_literals;

namespace
{
[[nodiscard]] auto current_time_secs()
{
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::steady_clock::now());
}
} // namespace

void tr_verify_worker::verify_torrent(
    Mediator& verify_mediator,
    std::atomic<bool> const& abort_flag,
    std::chrono::milliseconds const sleep_per_seconds_during_verify)
{
    verify_mediator.on_verify_started();

    tr_sys_file_t fd = TR_BAD_SYS_FILE;
    uint64_t file_pos = 0U;
    uint32_t piece_pos = 0U;
    tr_file_index_t file_index = 0U;
    tr_file_index_t prev_file_index = ~file_index;
    tr_piece_index_t piece = 0U;
    auto buffer = std::vector<std::byte>(1024U * 256U);
    auto sha = tr_sha1{};
    auto last_slept_at = current_time_secs();
    bool file_valid = false;
    std::vector<tr_file_index_t> files_ending_in_current_piece;

    auto const& metainfo = verify_mediator.metainfo();
    while (!abort_flag && piece < metainfo.piece_count())
    {
        auto const file_length = metainfo.file_size(file_index);

        /* if we're starting a new file... */
        if (file_pos == 0U && fd == TR_BAD_SYS_FILE && file_index != prev_file_index)
        {
            file_valid = true;
            auto error = tr_error{};
            auto const found = verify_mediator.find_file(file_index, &error);
            if (!found)
            {
                file_valid = false;
                fd = TR_BAD_SYS_FILE;
                verify_mediator.on_file_error(file_index, std::string(fmt::format("Error: {:s}.", error.message())));
            }
            else if (!found->isFile())
            {
                file_valid = false;
                fd = TR_BAD_SYS_FILE;
                verify_mediator.on_file_error(file_index, std::string("Not a file."));
            }
            else if (found->size != file_length)
            {
                file_valid = false;
                fd = TR_BAD_SYS_FILE;
                verify_mediator.on_file_error(
                    file_index,
                    std::string(fmt::format("Incorrect file size {:d}. Expected {:d}.", found->size, file_length)));
            }
            else
            {
                fd = tr_sys_file_open(found->filename(), TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, &error);
                if (fd == TR_BAD_SYS_FILE)
                {
                    file_valid = false;
                    verify_mediator.on_file_error(file_index, std::string(fmt::format("Error: {:s}.", error.message())));
                }
            }
            prev_file_index = file_index;
        }

        /* figure out how much we can read this pass */
        uint64_t left_in_piece = metainfo.piece_size(piece) - piece_pos;
        uint64_t left_in_file = file_length - file_pos;
        uint64_t bytes_this_pass = std::min(left_in_file, left_in_piece);
        bytes_this_pass = std::min(bytes_this_pass, uint64_t(std::size(buffer)));

        /* read a bit */
        if (fd != TR_BAD_SYS_FILE)
        {
            auto num_read = uint64_t{};
            if (tr_sys_file_read_at(fd, std::data(buffer), bytes_this_pass, file_pos, &num_read) && num_read > 0U)
            {
                bytes_this_pass = num_read;
                sha.add(std::data(buffer), bytes_this_pass);
            }
        }

        /* move our offsets */
        left_in_piece -= bytes_this_pass;
        left_in_file -= bytes_this_pass;
        piece_pos += bytes_this_pass;
        file_pos += bytes_this_pass;

        /* We want all on_file_error() calls for a file index to be consecutive. Therefore
         * don't add files for which an error has already been emitted. We don't need to report
         * piece errors for missing or wrong length files.
         */
        if (left_in_file == 0 && file_valid)
        {
            files_ending_in_current_piece.push_back(file_index);
        }

        /* if we're finishing a piece... */
        if (left_in_piece == 0U)
        {
            auto const has_piece = sha.finish() == metainfo.piece_hash(piece);
            verify_mediator.on_piece_checked(piece, has_piece);

            if (has_piece)
            {
                for (auto const file : files_ending_in_current_piece)
                {
                    verify_mediator.on_file_ok(file);
                }
            }
            else
            {
                auto const error_msg{ fmt::format("Piece {:d} hash fail", piece) };
                for (auto const file : files_ending_in_current_piece)
                {
                    verify_mediator.on_file_error(file, error_msg);
                }

                /* Don't report invalid piece for files we can't open. The file error
                 * has already been reported. */
                if (left_in_file != 0 && fd != TR_BAD_SYS_FILE)
                {
                    verify_mediator.on_file_error(file_index, error_msg);
                }
                file_valid = false;
            }

            if (sleep_per_seconds_during_verify > std::chrono::milliseconds::zero())
            {
                /* sleeping even just a few msec per second goes a long
                 * way towards reducing IO load... */
                if (auto const now = current_time_secs(); last_slept_at != now)
                {
                    last_slept_at = now;
                    std::this_thread::sleep_for(sleep_per_seconds_during_verify);
                }
            }

            sha.clear();
            ++piece;
            piece_pos = 0U;
            files_ending_in_current_piece.clear();
        }

        /* if we're finishing a file... */
        if (left_in_file == 0U)
        {
            if (fd != TR_BAD_SYS_FILE)
            {
                tr_sys_file_close(fd);
                fd = TR_BAD_SYS_FILE;
            }

            ++file_index;
            file_pos = 0U;
        }
    }

    /* cleanup */
    if (fd != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(fd);
    }

    verify_mediator.on_verify_done(abort_flag);
}

void tr_verify_worker::verify_thread_func()
{
    for (;;)
    {
        {
            auto const lock = std::scoped_lock{ verify_mutex_ };

            if (stop_current_)
            {
                stop_current_ = false;
                stop_current_cv_.notify_one();
            }

            if (std::empty(todo_))
            {
                current_node_.reset();
                verify_thread_id_.reset();
                return;
            }

            current_node_ = std::move(todo_.extract(std::begin(todo_)).value());
        }

        verify_torrent(*current_node_->mediator_, stop_current_, sleep_per_seconds_during_verify_);
    }
}

void tr_verify_worker::add(std::unique_ptr<Mediator> mediator, tr_priority_t priority)
{
    auto const lock = std::scoped_lock{ verify_mutex_ };

    mediator->on_verify_queued();
    todo_.emplace(std::move(mediator), priority);

    if (!verify_thread_id_)
    {
        auto thread = std::thread(&tr_verify_worker::verify_thread_func, this);
        verify_thread_id_ = thread.get_id();
        thread.detach();
    }
}

void tr_verify_worker::remove(tr_sha1_digest_t const& info_hash)
{
    auto lock = std::unique_lock(verify_mutex_);

    if (current_node_ && current_node_->matches(info_hash))
    {
        stop_current_ = true;
        stop_current_cv_.wait(lock, [this]() { return !stop_current_; });
    }
    else if (auto const iter = std::find_if(
                 std::begin(todo_),
                 std::end(todo_),
                 [&info_hash](auto const& node) { return node.matches(info_hash); });
             iter != std::end(todo_))
    {
        iter->mediator_->on_verify_done(true /*aborted*/);
        todo_.erase(iter);
    }
}

tr_verify_worker::~tr_verify_worker()
{
    {
        auto const lock = std::scoped_lock{ verify_mutex_ };
        stop_current_ = true;
        todo_.clear();
    }

    while (verify_thread_id_.has_value())
    {
        std::this_thread::sleep_for(20ms);
    }
}

void tr_verify_worker::set_sleep_per_seconds_during_verify(std::chrono::milliseconds const sleep_per_seconds_during_verify)
{
    sleep_per_seconds_during_verify_ = sleep_per_seconds_during_verify;
}

int tr_verify_worker::Node::compare(Node const& that) const noexcept
{
    // prefer higher-priority torrents
    if (priority_ != that.priority_)
    {
        return priority_ > that.priority_ ? -1 : 1;
    }

    // prefer smaller torrents, since they will verify faster
    auto const& metainfo = mediator_->metainfo();
    auto const& that_metainfo = that.mediator_->metainfo();
    if (metainfo.total_size() != that_metainfo.total_size())
    {
        return metainfo.total_size() < that_metainfo.total_size() ? -1 : 1;
    }

    // uniqueness check
    auto const& this_hash = metainfo.info_hash();
    auto const& that_hash = that_metainfo.info_hash();
    if (this_hash != that_hash)
    {
        return this_hash < that_hash ? -1 : 1;
    }

    return 0;
}

class SynchronousVerifyMediator final : public tr_verify_worker::Mediator
{
public:
    explicit SynchronousVerifyMediator(
        tr_torrent_metainfo const& metainfo,
        std::string_view const data_dir,
        std::function<void(tr_file_index_t, bool, std::string_view)> file_status_cb)
        : metainfo_{ metainfo }
        , data_dir_{ data_dir }
        , file_status_cb_{ std::move(file_status_cb) }
    {
    }

    ~SynchronousVerifyMediator() override = default;

    SynchronousVerifyMediator(SynchronousVerifyMediator const&) = delete;
    SynchronousVerifyMediator(SynchronousVerifyMediator&&) = delete;
    SynchronousVerifyMediator& operator=(SynchronousVerifyMediator const&) = delete;
    SynchronousVerifyMediator& operator=(SynchronousVerifyMediator&&) = delete;

    [[nodiscard]] tr_torrent_metainfo const& metainfo() const override
    {
        return metainfo_;
    }

    [[nodiscard]] std::optional<tr_torrent_files::FoundFile> find_file(tr_file_index_t file_index, tr_error* error)
        const override
    {
        auto filename = tr_pathbuf{};
        filename.assign(data_dir_, '/', metainfo_.file_subpath(file_index));
        auto const info = tr_sys_path_get_info(filename, 0, error);
        if (info)
        {
            return tr_torrent_files::FoundFile{ *info, std::move(filename), std::size(data_dir_) };
        }
        else
        {
            return {};
        }
    }

    void on_verify_queued() override
    {
    }

    void on_verify_started() override
    {
        all_valid = true;
    }

    void on_piece_checked(tr_piece_index_t /*piece*/, bool /*has_piece*/) override
    {
    }

    void on_verify_done(bool /*aborted*/) override
    {
    }

    void on_file_ok(tr_file_index_t index) override
    {
        file_status_cb_(index, true, "");
    }

    void on_file_error(tr_file_index_t index, std::string_view error) override
    {
        file_status_cb_(index, false, error);
        all_valid = false;
    }

    [[nodiscard]] bool torrent_is_valid() const
    {
        return all_valid;
    }

private:
    tr_torrent_metainfo const& metainfo_;
    std::string_view const data_dir_;
    std::function<void(tr_file_index_t, bool, std::string_view)> file_status_cb_;
    bool all_valid{ false };
};

bool tr_torrentSynchronousVerify(
    tr_torrent_metainfo const& metainfo,
    std::string_view const data_dir,
    std::function<void(tr_file_index_t, bool, std::string_view)> file_status_cb)
{
    std::atomic<bool> const& abort_flag = false;
    SynchronousVerifyMediator mediator(metainfo, data_dir, std::move(file_status_cb));
    tr_verify_worker::verify_torrent(mediator, abort_flag, std::chrono::milliseconds::zero());
    return mediator.torrent_is_valid();
}
