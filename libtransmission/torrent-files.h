// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "file.h"
#include "tr-strbuf.h"

/**
 * A simple ordered collection of files.
 */
struct tr_torrent_files
{
public:
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] uint64_t size(tr_file_index_t) const;
    [[nodiscard]] std::string const& path(tr_file_index_t) const;

    void setPath(tr_file_index_t, std::string_view path);

    void reserve(size_t);
    void shrinkToFit();
    void clear() noexcept;
    tr_file_index_t add(std::string_view path, uint64_t size);

    struct FoundFile : public tr_sys_path_info
    {
    public:
        FoundFile(tr_sys_path_info info, tr_pathbuf&& filename_in, size_t base_len_in)
            : tr_sys_path_info{ info }
            , filename_{ std::move(filename_in) }
            , base_len_{ base_len_in }
        {
        }

        [[nodiscard]] constexpr auto const& filename() const noexcept
        {
            // /home/foo/Downloads/torrent/01-file-one.txt
            return filename_;
        }

        [[nodiscard]] constexpr auto base() const noexcept
        {
            // /home/foo/Downloads
            return filename_.sv().substr(0, base_len_);
        }

        [[nodiscard]] constexpr auto subpath() const noexcept
        {
            // torrent/01-file-one.txt
            return filename_.sv().substr(base_len_ + 1);
        }

    private:
        tr_pathbuf filename_;
        size_t base_len_;
    };

    [[nodiscard]] std::optional<FoundFile> find(tr_file_index_t, std::string_view const* search_paths, size_t n_paths) const;
    [[nodiscard]] bool hasAnyLocalData(std::string_view const* search_paths, size_t n_paths) const;

    static constexpr std::string_view PartialFileSuffix = ".part";

private:
    struct file_t
    {
    public:
        void setPath(std::string_view subpath)
        {
            path_ = subpath;
        }

        file_t(std::string_view path, uint64_t size)
            : path_{ path }
            , size_{ size }
        {
        }

        std::string path_;
        uint64_t size_ = 0;
    };

    std::vector<file_t> files_;
};
