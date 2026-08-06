// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdio> // stderr
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/crypto-utils.h> // tr_base64_encode()
#include <libtransmission/file.h> // tr_sys_path_resolve()
#include <libtransmission/magnet-metainfo.h>
#include <libtransmission/torrent-metainfo.h> // tr_torrent_metainfo
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h> // tr_file_read()
#include <libtransmission/web-utils.h> // tr_urlIsValid()

#include "libtransmission-app/interop-names.h"
#include "libtransmission-app/interop.h"

using namespace std::string_view_literals;

namespace tr::interop
{

std::string canonical_config_dir(std::string_view const config_dir)
{
    if (auto path = tr_sys_path_resolve(config_dir); !std::empty(path))
    {
        return path;
    }

    // The dir does not exist yet, so there is nothing to resolve.
    // Absolute is the best we can do.
    if (tr_sys_path_is_relative(config_dir))
    {
        return std::string{ tr_pathbuf{ tr_sys_dir_get_current(), '/', config_dir } };
    }

    return std::string{ config_dir };
}

std::string canonical_config_dir_created(std::string_view const config_dir)
{
    // 0777 defers to the process umask, as every other config-dir create in
    // libtransmission does.
    tr_sys_dir_create(std::string{ config_dir }, TR_SYS_DIR_CREATE_PARENTS, 0777);
    return canonical_config_dir(config_dir);
}

std::string com_config_moniker_item(std::string_view const config_dir)
{
    return std::string{ ComConfigMonikerPrefix } + canonical_config_dir(config_dir);
}

std::string activation_token()
{
    for (auto const* const key : { "XDG_ACTIVATION_TOKEN", "DESKTOP_STARTUP_ID" })
    {
        if (auto token = tr_env_get_string(key); !std::empty(token))
        {
            return token;
        }
    }

    return {};
}

bool is_metainfo_link(std::string_view const arg)
{
    return tr_urlIsValid(arg) || tr_magnet_metainfo{}.parseMagnet(arg);
}

namespace
{
// A local name: a path, or the file:// URI a desktop's Exec=%U hands us.
[[nodiscard]] std::string local_filename(std::string_view const arg)
{
    auto constexpr Scheme = "file://"sv;
    if (!tr_strv_starts_with(arg, Scheme))
    {
        return std::string{ arg };
    }

    auto filename = tr_urlPercentDecode(arg.substr(std::size(Scheme)));

#ifdef _WIN32
    // `file:///C:/dir` leaves `/C:/dir` here. That first slash separates the URI's empty
    // authority from its path. It is not part of the path, and CreateFileW rejects it.
    if (std::size(filename) > 2U && filename.front() == '/' && filename[2U] == ':')
    {
        filename.erase(0U, 1U);
    }
#endif

    return filename;
}
} // namespace

std::optional<std::string> encode_metainfo_arg(std::string_view const arg)
{
    if (is_metainfo_link(arg))
    {
        return std::string{ arg };
    }

    if (auto contents = std::vector<char>{}; tr_file_read(local_filename(arg), contents))
    {
        return tr_base64_encode({ std::data(contents), std::size(contents) });
    }

    return {};
}

std::vector<std::string> encode_metainfo_args(std::vector<std::string> const& args)
{
    auto metainfos = std::vector<std::string>{};
    metainfos.reserve(std::size(args));

    for (auto const& arg : args)
    {
        if (auto encoded = encode_metainfo_arg(arg))
        {
            metainfos.push_back(std::move(*encoded));
        }
        else
        {
            fmt::print(stderr, "Skipping '{:s}': not a torrent file, URL or magnet link.\n", arg);
        }
    }

    return metainfos;
}

std::optional<std::string> decode_metainfo_torrent(std::string_view const metainfo)
{
    // Decoding alone proves nothing. base64's alphabet includes '/', so a path decodes to
    // bytes as readily as a torrent does. Only parsing tells them apart.
    auto contents = tr_base64_decode(metainfo);
    if (std::empty(contents) || !tr_torrent_metainfo{}.parse_benc(contents))
    {
        return {};
    }

    return contents;
}

} // namespace tr::interop
