// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility> // for std::move()

#include <fmt/format.h>

#include "libtransmission/transmission.h"

#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/relocate.h"
#include "libtransmission/torrent-files.h"
#include "libtransmission/tr-macros.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/utils.h"

using namespace std::chrono_literals;

namespace
{

bool is_folder(std::string_view path)
{
    auto const info = tr_sys_path_get_info(path);
    return info && info->isFolder();
}

bool is_empty_folder(char const* path)
{
    if (!is_folder(path))
    {
        return false;
    }

    if (auto const odir = tr_sys_dir_open(path); odir != TR_BAD_SYS_DIR)
    {
        char const* name_cstr = nullptr;
        while ((name_cstr = tr_sys_dir_read_name(odir)) != nullptr)
        {
            auto const name = std::string_view{ name_cstr };
            if (name != "." && name != "..")
            {
                tr_sys_dir_close(odir);
                return false;
            }
        }
        tr_sys_dir_close(odir);
    }

    return true;
}

void remove_empty_directories_recursive(std::string_view path)
{
    if (!is_folder(path))
    {
        return;
    }

    if (auto const odir = tr_sys_dir_open(path); odir != TR_BAD_SYS_DIR)
    {
        char const* name_cstr = nullptr;
        while ((name_cstr = tr_sys_dir_read_name(odir)) != nullptr)
        {
            auto const name = std::string_view{ name_cstr };
            if (name != "." && name != "..")
            {
                remove_empty_directories_recursive(tr_pathbuf{ path, '/', name });
            }
        }
        tr_sys_dir_close(odir);
    }

    if (is_empty_folder(std::string{ path }.c_str()))
    {
        tr_sys_path_remove(path, nullptr);
    }
}

} // unnamed namespace

void tr_relocate_worker::relocate_torrent(Mediator& mediator, std::atomic<bool> const& abort_flag)
{
    auto const parent_name = mediator.name();
    tr_logAddTrace(fmt::format("relocate_torrent: starting file I/O for '{}'", parent_name), parent_name);

    mediator.on_relocate_started();

    auto const& files = mediator.files();
    auto const old_parent = tr_pathbuf{ mediator.old_path() };
    auto const new_parent = tr_pathbuf{ mediator.new_path() };

    tr_logAddTrace(
        fmt::format("relocate_torrent: moving {} files from '{}' to '{}'", files.file_count(), old_parent, new_parent),
        parent_name);

    // If the paths are the same, nothing to do
    if (tr_sys_path_is_same(old_parent, new_parent))
    {
        mediator.on_relocate_done(false, std::nullopt);
        return;
    }

    // Create the destination directory
    auto error = tr_error{};
    if (!tr_sys_dir_create(new_parent, TR_SYS_DIR_CREATE_PARENTS, 0777, &error))
    {
        mediator.on_relocate_done(false, std::string{ error.message() });
        return;
    }

    auto const paths = std::array<std::string_view, 1>{ old_parent.sv() };
    auto move_error = std::optional<std::string>{};

    for (tr_file_index_t i = 0, n = files.file_count(); i < n && !abort_flag; ++i)
    {
        auto const found = files.find(i, std::data(paths), std::size(paths));
        if (!found)
        {
            mediator.on_file_relocated(i, true); // File doesn't exist at old location, consider it "moved"
            continue;
        }

        auto const& old_path = found->filename();
        auto const new_path = tr_pathbuf{ new_parent, '/', found->subpath() };

        tr_logAddTrace(fmt::format("Found file #{} '{}'", i, old_path), parent_name);

        if (tr_sys_path_is_same(old_path, new_path))
        {
            mediator.on_file_relocated(i, true);
            continue;
        }

        tr_logAddTrace(fmt::format("Moving file #{} to '{}'", i, new_path), parent_name);

        auto file_error = tr_error{};
        if (!tr_file_move(old_path, new_path, true, &file_error))
        {
            move_error = std::string{ file_error.message() };
            mediator.on_file_relocated(i, false);
            break;
        }

        mediator.on_file_relocated(i, true);
    }

    // If we were aborted or had an error, report it
    if (abort_flag)
    {
        mediator.on_relocate_done(true, std::nullopt);
        return;
    }

    if (move_error)
    {
        mediator.on_relocate_done(false, move_error);
        return;
    }

    // After moving the files successfully, remove any leftover empty directories
    remove_empty_directories_recursive(old_parent);

    mediator.on_relocate_done(false, std::nullopt);
}

void tr_relocate_worker::relocate_thread_func()
{
    for (;;)
    {
        {
            auto const lock = std::scoped_lock{ relocate_mutex_ };

            if (stop_current_)
            {
                stop_current_ = false;
                stop_current_cv_.notify_one();
            }

            if (std::empty(todo_))
            {
                current_node_.reset();
                relocate_thread_id_.reset();
                return;
            }

            current_node_ = std::move(todo_.extract(std::begin(todo_)).value());
        }

        relocate_torrent(*current_node_->mediator_, stop_current_);
    }
}

void tr_relocate_worker::add(std::unique_ptr<Mediator> mediator)
{
    auto const lock = std::scoped_lock{ relocate_mutex_ };

    mediator->on_relocate_queued();
    todo_.emplace(std::move(mediator));

    if (!relocate_thread_id_)
    {
        auto thread = std::thread(&tr_relocate_worker::relocate_thread_func, this);
        relocate_thread_id_ = thread.get_id();
        thread.detach();
    }
}

void tr_relocate_worker::remove(tr_torrent_id_t const tor_id)
{
    auto lock = std::unique_lock(relocate_mutex_);

    if (current_node_ && current_node_->matches(tor_id))
    {
        stop_current_ = true;
        stop_current_cv_.wait(lock, [this]() { return !stop_current_; });
    }
    else if (auto const iter = std::find_if(
                 std::begin(todo_),
                 std::end(todo_),
                 [tor_id](auto const& node) { return node.matches(tor_id); });
             iter != std::end(todo_))
    {
        iter->mediator_->on_relocate_done(true /*aborted*/, std::nullopt);
        todo_.erase(iter);
    }
}

tr_relocate_worker::~tr_relocate_worker()
{
    {
        auto const lock = std::scoped_lock{ relocate_mutex_ };
        stop_current_ = true;
        todo_.clear();
    }

    while (relocate_thread_id_.has_value())
    {
        std::this_thread::sleep_for(20ms);
    }
}

int tr_relocate_worker::Node::compare(Node const& that) const noexcept
{
    // Order by torrent ID for consistency
    auto const this_id = mediator_->torrent_id();
    auto const that_id = that.mediator_->torrent_id();
    if (this_id != that_id)
    {
        return this_id < that_id ? -1 : 1;
    }

    return 0;
}
