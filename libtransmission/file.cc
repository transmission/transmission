// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <filesystem>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/error.h"
#include "libtransmission/file.h"

namespace
{

void maybe_set_error(tr_error* error, std::error_code const& ec)
{
    if (error != nullptr && ec)
    {
        error->set(ec.value(), ec.message());
    }
}

} // namespace

std::string tr_sys_path_resolve(std::string_view path, tr_error* error)
{
    auto ec = std::error_code{};
    auto const canonical_path = std::filesystem::canonical(tr_u8path(path), ec);

    if (ec)
    {
        maybe_set_error(error, ec);
        return {};
    }

    auto const u8_path = canonical_path.u8string();
    return { std::begin(u8_path), std::end(u8_path) };
}

bool tr_sys_path_is_relative(std::string_view path)
{
#ifdef _WIN32
    auto const is_slash = [](char ch)
    {
        return ch == '/' || ch == '\\';
    };

    if (std::size(path) >= 2 && is_slash(path[0]) && path[1] == path[0])
    {
        return false;
    }

    if (std::size(path) == 2 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':')
    {
        return false;
    }

    if (std::size(path) > 2 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 && path[1] == ':' && is_slash(path[2]))
    {
        return false;
    }

    return true;
#else
    return std::empty(path) || path.front() != '/';
#endif
}

bool tr_sys_path_exists(std::string_view path, tr_error* error)
{
    auto ec = std::error_code{};
    auto const exists = std::filesystem::exists(tr_u8path(path), ec);
    maybe_set_error(error, ec);
    return exists;
}

std::optional<tr_sys_path_info> tr_sys_path_get_info(std::string_view path, int flags, tr_error* error)
{
    auto const filesystem_path = tr_u8path(path);

    auto ec = std::error_code{};
    auto const status = (flags & TR_SYS_PATH_NO_FOLLOW) != 0 ? std::filesystem::symlink_status(filesystem_path, ec) :
                                                               std::filesystem::status(filesystem_path, ec);

    if (ec || status.type() == std::filesystem::file_type::not_found)
    {
        maybe_set_error(error, ec ? ec : std::make_error_code(std::errc::no_such_file_or_directory));
        return {};
    }

    auto info = tr_sys_path_info{};

    if (std::filesystem::is_regular_file(status))
    {
        info.type = TR_SYS_PATH_IS_FILE;
        info.size = std::filesystem::file_size(filesystem_path, ec);
        if (ec)
        {
            maybe_set_error(error, ec);
            return {};
        }
    }
    else if (std::filesystem::is_directory(status))
    {
        info.type = TR_SYS_PATH_IS_DIRECTORY;
        info.size = 0;
    }
    else
    {
        info.type = TR_SYS_PATH_IS_OTHER;
        info.size = 0;
    }

    auto const ftime = std::filesystem::last_write_time(filesystem_path, ec);
    if (ec)
    {
        maybe_set_error(error, ec);
        return {};
    }

    // TODO: use std::chrono::clock_cast when available.
    // https://github.com/llvm/llvm-project/issues/166050
    auto const sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    info.last_modified_at = std::chrono::system_clock::to_time_t(sctp);

    return info;
}

bool tr_sys_path_is_same(std::string_view path1, std::string_view path2, tr_error* error)
{
    auto const u8path1 = tr_u8path(path1);
    auto const u8path2 = tr_u8path(path2);

    // std::filesystem::equivalent() returns an unspecified error
    // when either path doesn't exist. libstdc++ and libc++ chose
    // different errors. So let's check `exists` here for consistency.
    auto ec = std::error_code{};
    if (!std::filesystem::exists(u8path1, ec) || !std::filesystem::exists(u8path2, ec))
    {
        maybe_set_error(error, ec);
        return false;
    }

    auto const same = std::filesystem::equivalent(u8path1, u8path2, ec);
    maybe_set_error(error, ec);
    return same;
}

bool tr_sys_dir_create(std::string_view path, int flags, [[maybe_unused]] int permissions, tr_error* error)
{
    auto const filesystem_path = tr_u8path(path);
    auto const parents = (flags & TR_SYS_DIR_CREATE_PARENTS) != 0;

#ifndef _WIN32
    auto missing = std::vector<std::filesystem::path>{};
    if (parents && permissions != 0)
    {
        auto current = std::filesystem::path{};
        for (auto const& part : filesystem_path)
        {
            current /= part;
            auto check_ec = std::error_code{};
            if (std::filesystem::is_directory(current, check_ec))
            {
                continue;
            }

            if (check_ec && check_ec != std::errc::no_such_file_or_directory)
            {
                maybe_set_error(error, check_ec);
                return false;
            }

            missing.emplace_back(current);
        }
    }
#endif

    auto ec = std::error_code{};
    if (std::filesystem::is_directory(filesystem_path, ec))
    {
        return true;
    }

    if (ec && ec != std::errc::no_such_file_or_directory)
    {
        maybe_set_error(error, ec);
        return false;
    }

    ec = {};
    if (parents)
    {
        std::filesystem::create_directories(filesystem_path, ec);
    }
    else
    {
        std::filesystem::create_directory(filesystem_path, ec);
    }

    if (ec)
    {
        maybe_set_error(error, ec);
        return false;
    }

#ifndef _WIN32
    if (permissions != 0)
    {
        auto const apply_permissions = [&](std::filesystem::path const& target)
        {
            auto perm_ec = std::error_code{};
            std::filesystem::permissions(
                target,
                static_cast<std::filesystem::perms>(permissions),
                std::filesystem::perm_options::replace,
                perm_ec);
            if (perm_ec)
            {
                maybe_set_error(error, perm_ec);
                return false;
            }

            return true;
        };

        if (parents)
        {
            for (auto const& created_path : missing)
            {
                if (!apply_permissions(created_path))
                {
                    return false;
                }
            }
        }
        else if (!apply_permissions(filesystem_path))
        {
            return false;
        }
    }
#endif

    return true;
}

std::vector<std::string> tr_sys_dir_get_files(
    std::string_view folder,
    std::function<bool(std::string_view)> const& test,
    tr_error* error)
{
    if (auto const info = tr_sys_path_get_info(folder); !info || !info->isFolder())
    {
        return {};
    }

    auto const odir = tr_sys_dir_open(folder, error);
    if (odir == TR_BAD_SYS_DIR)
    {
        return {};
    }

    auto filenames = std::vector<std::string>{};
    for (;;)
    {
        char const* const name = tr_sys_dir_read_name(odir, error);

        if (name == nullptr)
        {
            tr_sys_dir_close(odir, error);
            return filenames;
        }

        if (test(name))
        {
            filenames.emplace_back(name);
        }
    }
}

std::optional<std::filesystem::space_info> tr_sys_path_get_capacity(std::filesystem::path const& path, tr_error* error)
{
    auto ec = std::error_code{};
    auto space = std::filesystem::space(path, ec);
    if (!ec)
    {
        return space;
    }

    maybe_set_error(error, ec);
    return {};
}
