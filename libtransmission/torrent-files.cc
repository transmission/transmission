// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "transmission.h"

#include "error.h"
#include "log.h"
#include "torrent-files.h"
#include "utils.h"

///

std::optional<tr_torrent_files::FoundFile> tr_torrent_files::find(
    tr_file_index_t file_index,
    std::string_view const* search_paths,
    size_t n_paths) const
{
    auto filename = tr_pathbuf{};
    auto file_info = tr_sys_path_info{};
    auto const& subpath = path(file_index);

    for (size_t path_idx = 0; path_idx < n_paths; ++path_idx)
    {
        auto const base = search_paths[path_idx];

        filename.assign(base, '/', subpath);
        if (tr_sys_path_get_info(filename, 0, &file_info))
        {
            return FoundFile{ file_info, std::move(filename), std::size(base) };
        }

        filename.assign(filename, base, '/', subpath, PartialFileSuffix);
        if (tr_sys_path_get_info(filename, 0, &file_info))
        {
            return FoundFile{ file_info, std::move(filename), std::size(base) };
        }
    }

    return {};
}

bool tr_torrent_files::hasAnyLocalData(std::string_view const* search_paths, size_t n_paths) const
{
    for (tr_file_index_t i = 0, n = fileCount(); i < n; ++i)
    {
        if (find(i, search_paths, n_paths))
        {
            return true;
        }
    }

    return false;
}

///

bool tr_torrent_files::move(
    std::string_view old_top_in,
    std::string_view top_in,
    double volatile* setme_progress,
    std::string_view log_name,
    tr_error** error) const
{
    if (setme_progress != nullptr)
    {
        *setme_progress = 0.0;
    }

    auto const old_top = tr_pathbuf{ old_top_in };
    auto const top = tr_pathbuf{ top_in };
    tr_logAddTrace(fmt::format(FMT_STRING("Moving files from '{:s}' to '{:s}'"), old_top, top), log_name);

    if (tr_sys_path_is_same(old_top, top))
    {
        return true;
    }

    if (!tr_sys_dir_create(top, TR_SYS_DIR_CREATE_PARENTS, 0777, error))
    {
        return false;
    }

    auto const search_paths = std::array<std::string_view, 1>{ old_top.sv() };

    auto const total_size = totalSize();
    auto err = bool{};
    auto bytes_moved = uint64_t{};

    for (tr_file_index_t i = 0, n = fileCount(); i < n; ++i)
    {
        auto const found = find(i, std::data(search_paths), std::size(search_paths));
        if (!found)
        {
            continue;
        }

        auto const& old_path = found->filename();
        auto const path = tr_pathbuf{ top, '/', found->subpath() };
        tr_logAddTrace(fmt::format(FMT_STRING("Found file #{:d} '{:s}'"), i, old_path), log_name);

        if (tr_sys_path_is_same(old_path, path))
        {
            continue;
        }

        tr_logAddTrace(fmt::format(FMT_STRING("Moving file #{:d} to '{:s}'"), i, old_path, path), log_name);
        if (!tr_moveFile(old_path, path, error))
        {
            err = true;
            break;
        }

        if (setme_progress != nullptr && total_size > 0U)
        {
            bytes_moved += fileSize(i);
            *setme_progress = static_cast<double>(bytes_moved) / total_size;
        }
    }

    if (!err)
    {
        // FIXME
        // remove away the leftover subdirectories in the old location */
        // tr_torrentDeleteLocalData(tor, tr_sys_path_remove);
    }

    return !err;
}
