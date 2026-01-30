// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <filesystem>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/error.h"
#include "libtransmission/file.h"

std::string tr_sys_path_resolve(std::string_view path, tr_error* error)
{
    auto ec = std::error_code{};
    auto const canonical_path = std::filesystem::canonical(tr_u8path(path), ec);

    if (ec)
    {
        if (error != nullptr)
        {
            error->set(ec.value(), ec.message());
        }

        return {};
    }

    auto const u8_path = canonical_path.u8string();
    return { std::begin(u8_path), std::end(u8_path) };
}

bool tr_sys_path_exists(std::string_view path, tr_error* error)
{
    auto ec = std::error_code{};
    auto const exists = std::filesystem::exists(tr_u8path(path), ec);

    if (ec && ec != std::errc::no_such_file_or_directory && error != nullptr)
    {
        error->set(ec.value(), ec.message());
    }

    return exists;
}

bool tr_sys_path_is_same(std::string_view path1, std::string_view path2, tr_error* error)
{
    auto ec = std::error_code{};
    auto const same = std::filesystem::equivalent(tr_u8path(path1), tr_u8path(path2), ec);

    if (!ec)
    {
        return same;
    }

    if (ec != std::errc::no_such_file_or_directory && error != nullptr)
    {
        error->set(ec.value(), ec.message());
    }

    return false;
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

    if (error != nullptr)
    {
        error->set(ec.value(), ec.message());
    }
    return {};
}
