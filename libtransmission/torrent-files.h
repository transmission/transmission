// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm> // std::sort()
#include <cstddef>
#include <cstdint> // uint64_t
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "transmission.h"

#include "file.h"
#include "tr-strbuf.h"

struct tr_error;

/**
 * A simple collection of files & utils for finding them, moving them, etc.
 */
struct tr_torrent_files
{
public:
    [[nodiscard]] TR_CONSTEXPR20 bool empty() const noexcept
    {
        return std::empty(files_);
    }

    [[nodiscard]] TR_CONSTEXPR20 size_t fileCount() const noexcept
    {
        return std::size(files_);
    }

    [[nodiscard]] TR_CONSTEXPR20 uint64_t fileSize(tr_file_index_t file_index) const
    {
        return files_.at(file_index).size_;
    }

    [[nodiscard]] constexpr auto totalSize() const noexcept
    {
        return total_size_;
    }

    [[nodiscard]] TR_CONSTEXPR20 std::string const& path(tr_file_index_t file_index) const
    {
        return files_.at(file_index).path_;
    }

    void setPath(tr_file_index_t file_index, std::string_view path)
    {
        files_.at(file_index).setPath(path);
    }

    void insertSubpathPrefix(std::string_view path)
    {
        auto const buf = tr_pathbuf{ path, '/' };

        for (auto& file : files_)
        {
            file.path_.insert(0, buf.sv());
            file.path_.shrink_to_fit();
        }
    }

    void reserve(size_t n_files)
    {
        files_.reserve(n_files);
    }

    void shrinkToFit()
    {
        files_.shrink_to_fit();
    }

    TR_CONSTEXPR20 void clear() noexcept
    {
        files_.clear();
        total_size_ = uint64_t{};
    }

    [[nodiscard]] auto sortedByPath() const
    {
        auto ret = std::vector<std::pair<std::string /*path*/, uint64_t /*size*/>>{};
        ret.reserve(std::size(files_));
        std::transform(
            std::begin(files_),
            std::end(files_),
            std::back_inserter(ret),
            [](auto const& in) { return std::make_pair(in.path_, in.size_); });

        std::sort(std::begin(ret), std::end(ret), [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });

        return ret;
    }

    tr_file_index_t add(std::string_view path, uint64_t file_size)
    {
        auto const ret = static_cast<tr_file_index_t>(std::size(files_));
        files_.emplace_back(path, file_size);
        total_size_ += file_size;
        return ret;
    }

    bool move(
        std::string_view old_parent_in,
        std::string_view parent_in,
        double volatile* setme_progress,
        std::string_view parent_name = "",
        tr_error** error = nullptr) const;

    using FileFunc = std::function<void(char const* filename)>;
    void remove(std::string_view parent_in, std::string_view tmpdir_prefix, FileFunc const& func) const;

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

    [[nodiscard]] std::optional<FoundFile> find(tr_file_index_t file, std::string_view const* paths, size_t n_paths) const;
    [[nodiscard]] bool hasAnyLocalData(std::string_view const* paths, size_t n_paths) const;

    static void makeSubpathPortable(std::string_view path, tr_pathbuf& append_me);

    [[nodiscard]] static auto makeSubpathPortable(std::string_view path)
    {
        auto tmp = tr_pathbuf{};
        makeSubpathPortable(path, tmp);
        return std::string{ tmp.sv() };
    }

    [[nodiscard]] static bool isSubpathPortable(std::string_view path)
    {
        return makeSubpathPortable(path) == path;
    }

    static constexpr std::string_view PartialFileSuffix = ".part";

private:
    struct file_t
    {
    public:
        void setPath(std::string_view subpath)
        {
            if (path_ != subpath)
            {
                path_ = subpath;
                path_.shrink_to_fit();
            }
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
    uint64_t total_size_ = 0;
};
