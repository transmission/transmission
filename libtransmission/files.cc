// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <optional>
#include <string>
#include <string_view>

#include "transmission.h"

#include "files.h"

bool tr_files::empty() const noexcept
{
    return std::empty(files_);
}

size_t tr_files::size() const noexcept
{
    return std::size(files_);
}

uint64_t tr_files::size(tr_file_index_t file_index) const
{
    return files_.at(file_index).size_;
}

std::string const& tr_files::path(tr_file_index_t file_index) const
{
    return files_.at(file_index).path_;
}

void tr_files::setPath(tr_file_index_t file_index, std::string_view path)
{
    files_.at(file_index).setPath(path);
}

void tr_files::reserve(size_t n_files)
{
    files_.reserve(n_files);
}

void tr_files::shrinkToFit()
{
    files_.shrink_to_fit();
}

tr_file_index_t tr_files::add(std::string_view path, uint64_t size)
{
    auto const file_index = static_cast<tr_file_index_t>(std::size(files_));
    files_.emplace_back(path, size);
    return file_index;
}

void tr_files::clear() noexcept
{
    files_.clear();
}

std::optional<tr_files::FoundFile> tr_files::find(
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
