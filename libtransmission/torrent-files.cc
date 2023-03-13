// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::find()
#include <cctype>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "transmission.h"

#include "error.h"
#include "log.h"
#include "torrent-files.h"
#include "utils.h"

using namespace std::literals;

namespace
{

using file_func_t = std::function<void(char const* filename)>;

bool isFolder(std::string_view path)
{
    auto const info = tr_sys_path_get_info(path);
    return info && info->isFolder();
}

bool isEmptyFolder(char const* path)
{
    if (!isFolder(path))
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

void depthFirstWalk(char const* path, file_func_t const& func, std::optional<int> max_depth = {})
{
    if (isFolder(path) && (!max_depth || *max_depth > 0))
    {
        if (auto const odir = tr_sys_dir_open(path); odir != TR_BAD_SYS_DIR)
        {
            char const* name_cstr = nullptr;
            while ((name_cstr = tr_sys_dir_read_name(odir)) != nullptr)
            {
                auto const name = std::string_view{ name_cstr };
                if (name == "." || name == "..")
                {
                    continue;
                }

                depthFirstWalk(tr_pathbuf{ path, '/', name }.c_str(), func, max_depth ? *max_depth - 1 : max_depth);
            }

            tr_sys_dir_close(odir);
        }
    }

    func(path);
}

bool isJunkFile(std::string_view filename)
{
    auto const base = tr_sys_path_basename(filename);

#ifdef __APPLE__
    // check for resource forks. <http://web.archive.org/web/20101010051608/http://support.apple.com/kb/TA20578>
    if (tr_strvStartsWith(base, "._"sv))
    {
        return true;
    }
#endif

    auto constexpr Files = std::array<std::string_view, 3>{
        ".DS_Store"sv,
        "Thumbs.db"sv,
        "desktop.ini"sv,
    };

    return std::find(std::begin(Files), std::end(Files), base) != std::end(Files);
}

} // unnamed namespace

// ---

std::optional<tr_torrent_files::FoundFile> tr_torrent_files::find(
    tr_file_index_t file_index,
    std::string_view const* paths,
    size_t n_paths) const
{
    auto filename = tr_pathbuf{};
    auto const& subpath = path(file_index);

    for (size_t path_idx = 0; path_idx < n_paths; ++path_idx)
    {
        auto const base = paths[path_idx];

        filename.assign(base, '/', subpath);
        if (auto const info = tr_sys_path_get_info(filename); info)
        {
            return FoundFile{ *info, std::move(filename), std::size(base) };
        }

        filename.assign(base, '/', subpath, PartialFileSuffix);
        if (auto const info = tr_sys_path_get_info(filename); info)
        {
            return FoundFile{ *info, std::move(filename), std::size(base) };
        }
    }

    return {};
}

bool tr_torrent_files::hasAnyLocalData(std::string_view const* paths, size_t n_paths) const
{
    for (tr_file_index_t i = 0, n = fileCount(); i < n; ++i)
    {
        if (find(i, paths, n_paths))
        {
            return true;
        }
    }

    return false;
}

// ---

bool tr_torrent_files::move(
    std::string_view old_parent_in,
    std::string_view parent_in,
    double volatile* setme_progress,
    std::string_view parent_name,
    tr_error** error) const
{
    if (setme_progress != nullptr)
    {
        *setme_progress = 0.0;
    }

    auto const old_parent = tr_pathbuf{ old_parent_in };
    auto const parent = tr_pathbuf{ parent_in };
    tr_logAddTrace(fmt::format(FMT_STRING("Moving files from '{:s}' to '{:s}'"), old_parent, parent), parent_name);

    if (tr_sys_path_is_same(old_parent, parent))
    {
        return true;
    }

    if (!tr_sys_dir_create(parent, TR_SYS_DIR_CREATE_PARENTS, 0777, error))
    {
        return false;
    }

    auto const paths = std::array<std::string_view, 1>{ old_parent.sv() };

    auto const total_size = totalSize();
    auto err = bool{};
    auto bytes_moved = uint64_t{};

    for (tr_file_index_t i = 0, n = fileCount(); i < n; ++i)
    {
        auto const found = find(i, std::data(paths), std::size(paths));
        if (!found)
        {
            continue;
        }

        auto const& old_path = found->filename();
        auto const path = tr_pathbuf{ parent, '/', found->subpath() };
        tr_logAddTrace(fmt::format(FMT_STRING("Found file #{:d} '{:s}'"), i, old_path), parent_name);

        if (tr_sys_path_is_same(old_path, path))
        {
            continue;
        }

        tr_logAddTrace(fmt::format(FMT_STRING("Moving file #{:d} to '{:s}'"), i, old_path, path), parent_name);
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

    // after moving the files, remove any leftover empty directories
    if (!err)
    {
        auto const remove_empty_directories = [](char const* filename)
        {
            if (isEmptyFolder(filename))
            {
                tr_sys_path_remove(filename, nullptr);
            }
        };

        remove(old_parent, parent_name, remove_empty_directories);
    }

    return !err;
}

// ---

/**
 * This convoluted code does something (seemingly) simple:
 * remove the torrent's local files.
 *
 * Fun complications:
 * 1. Try to preserve the directory hierarchy in the recycle bin.
 * 2. If there are nontorrent files, don't delete them...
 * 3. ...unless the other files are "junk", such as .DS_Store
 */
void tr_torrent_files::remove(std::string_view parent_in, std::string_view tmpdir_prefix, FileFunc const& func) const
{
    auto const parent = tr_pathbuf{ parent_in };

    // don't try to delete local data if the directory's gone missing
    if (!tr_sys_path_exists(parent))
    {
        return;
    }

    // make a tmpdir
    auto tmpdir = tr_pathbuf{ parent, '/', tmpdir_prefix, "__XXXXXX"sv };
    tr_sys_dir_create_temp(std::data(tmpdir));

    // move the local data to the tmpdir
    auto const paths = std::array<std::string_view, 1>{ parent.sv() };
    for (tr_file_index_t idx = 0, n_files = fileCount(); idx < n_files; ++idx)
    {
        if (auto const found = find(idx, std::data(paths), std::size(paths)); found)
        {
            tr_moveFile(found->filename(), tr_pathbuf{ tmpdir, '/', found->subpath() });
        }
    }

    // Make a list of the top-level torrent files & folders
    // because we'll need it below in the 'remove junk' phase
    auto const path = tr_pathbuf{ parent, '/', tmpdir_prefix };
    auto top_files = std::set<std::string>{ std::string{ path } };
    depthFirstWalk(
        tmpdir,
        [&parent, &tmpdir, &top_files](char const* filename)
        {
            if (tmpdir != filename)
            {
                top_files.emplace(tr_pathbuf{ parent, '/', tr_sys_path_basename(filename) });
            }
        },
        1);

    auto const func_wrapper = [&tmpdir, &func](char const* filename)
    {
        if (tmpdir != filename)
        {
            func(filename);
        }
    };

    // Remove the tmpdir.
    // Since `func` might send files to a recycle bin, try to preserve
    // the folder hierarchy by removing top-level files & folders first.
    // But that can fail -- e.g. `func` might refuse to remove nonempty
    // directories -- so plan B is to remove everything bottom-up.
    depthFirstWalk(tmpdir, func_wrapper, 1);
    depthFirstWalk(tmpdir, func_wrapper);
    tr_sys_path_remove(tmpdir);

    // OK we've removed the local data.
    // What's left are empty folders, junk, and user-generated files.
    // Remove the first two categories and leave the third alone.
    auto const remove_junk = [](char const* filename)
    {
        if (isEmptyFolder(filename) || isJunkFile(filename))
        {
            tr_sys_path_remove(filename);
        }
    };
    for (auto const& filename : top_files)
    {
        depthFirstWalk(filename.c_str(), remove_junk);
    }
}

namespace
{

// https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file
// Do not use the following reserved names for the name of a file:
// CON, PRN, AUX, NUL, COM1, COM2, COM3, COM4, COM5, COM6, COM7, COM8,
// COM9, LPT1, LPT2, LPT3, LPT4, LPT5, LPT6, LPT7, LPT8, and LPT9.
// Also avoid these names followed immediately by an extension;
// for example, NUL.txt is not recommended.
[[nodiscard]] bool isReservedFile(std::string_view in) noexcept
{
    if (std::empty(in))
    {
        return false;
    }

    // Shortcut to avoid extra work below.
    // All the paths below involve filenames that begin with one of these chars
    static auto constexpr ReservedFilesBeginWithOneOf = "ACLNP"sv;
    if (ReservedFilesBeginWithOneOf.find(toupper(in.front())) == std::string_view::npos)
    {
        return false;
    }

    auto in_upper = tr_pathbuf{ in };
    std::transform(std::begin(in_upper), std::end(in_upper), std::begin(in_upper), [](auto ch) { return toupper(ch); });
    auto const in_upper_sv = in_upper.sv();

    static auto constexpr ReservedNames = std::array<std::string_view, 22>{
        "AUX"sv,  "CON"sv,  "NUL"sv,  "PRN"sv, //
        "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv, "COM8"sv, "COM9"sv, //
        "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv, //
    };
    if (std::find(std::begin(ReservedNames), std::end(ReservedNames), in_upper_sv) != std::end(ReservedNames))
    {
        return true;
    }

    static auto constexpr ReservedPrefixes = std::array<std::string_view, 22>{
        "AUX."sv,  "CON."sv,  "NUL."sv,  "PRN."sv, //
        "COM1."sv, "COM2."sv, "COM3."sv, "COM4."sv, "COM5."sv, "COM6."sv, "COM7."sv, "COM8."sv, "COM9."sv, //
        "LPT1."sv, "LPT2."sv, "LPT3."sv, "LPT4."sv, "LPT5."sv, "LPT6."sv, "LPT7."sv, "LPT8."sv, "LPT9."sv, //
    };
    return std::any_of(
        std::begin(ReservedPrefixes),
        std::end(ReservedPrefixes),
        [in_upper_sv](auto const& prefix) { return tr_strvStartsWith(in_upper_sv, prefix); });
}

// https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
// Use any character in the current code page for a name, including Unicode
// characters and characters in the extended character set (128–255),
// except for the following:
[[nodiscard]] auto constexpr isReservedChar(char ch) noexcept
{
    switch (ch)
    {
    case '"':
    case '*':
    case '/':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
        return true;
    default:
        return false;
    }
}

void appendSanitizedComponent(std::string_view in, tr_pathbuf& out)
{
    // remove leading and trailing spaces
    in = tr_strvStrip(in);

    // remove trailing periods
    while (tr_strvEndsWith(in, '.'))
    {
        in.remove_suffix(1);
    }

    if (isReservedFile(in))
    {
        out.append('_');
    }

    // replace reserved characters with an underscore
    static auto constexpr AddChar = [](auto ch)
    {
        return isReservedChar(ch) ? '_' : ch;
    };
    std::transform(std::begin(in), std::end(in), std::back_inserter(out), AddChar);
}

} // namespace

void tr_torrent_files::makeSubpathPortable(std::string_view path, tr_pathbuf& append_me)
{
    auto segment = std::string_view{};
    while (tr_strvSep(&path, &segment, '/'))
    {
        appendSanitizedComponent(segment, append_me);
        append_me.append('/');
    }

    if (auto const n = std::size(append_me); n > 0)
    {
        append_me.resize(n - 1); // remove trailing slash
    }
}
