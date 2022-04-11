// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

struct tr_error;

struct tr_files
{
public:
    tr_files(tr_files const&) = delete;
    tr_files& operator= (tr_files const&) = delete;

    [[nodiscard]] constexpr auto empty() const noexcept
    {
        return std::empty(files_);
    }

    [[nodiscard]] constexpr auto fileCount() const noexcept
    {
        return std::size(files_);
    }

    [[nodiscard]] uint64_t size(tr_file_index_t i) const;

    [[nodiscard]] std::string const& subpath(tr_file_index_t i) const;

    void setSubpath(tr_file_index_t i, std::string_view subpath);

protected:
    void add(std::string_view subpath, uint64_t size);

private:

    struct file_t
    {
    public:
        [[nodiscard]] std::string const& path() const noexcept
        {
            return path_;
        }

        void setSubpath(std::string_view subpath)
        {
            path_ = subpath;
        }

        [[nodiscard]] uint64_t size() const noexcept
        {
            return size_;
        }

        file_t(std::string_view path, uint64_t size)
            : path_{ path }
            , size_{ size }
        {
        }

    private:
        std::string path_;
        uint64_t size_ = 0;
    };

    std::vector<file_t> files_;
};

struct tr_file_info
{
    [[nodiscard]] static std::string sanitizePath(std::string_view path);

    [[nodiscard]] static bool isPortable(std::string_view path)
    {
        return sanitizePath(path) == path;
    }
};
