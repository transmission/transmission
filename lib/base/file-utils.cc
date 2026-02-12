// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>
#include <vector>

#include <algorithm>
#include <iterator>

#ifndef _WIN32
#include <sys/stat.h> /* umask() */
#endif

#include <fmt/format.h>

#include "lib/base/error-types.h"
#include "lib/base/error.h"
#include "lib/base/file-utils.h"
#include "lib/base/file.h"
#include "lib/base/i18n.h"
#include "lib/base/log.h"
#include "lib/base/mime-types.h"
#include "lib/base/string-utils.h"
#include "lib/base/tr-strbuf.h"

using namespace std::literals;

bool tr_file_read(std::string_view filename, std::vector<char>& contents, tr_error* error)
{
    auto const szfilename = tr_pathbuf{ filename };

    /* try to stat the file */
    auto local_error = tr_error{};
    if (error == nullptr)
    {
        error = &local_error;
    }

    auto const info = tr_sys_path_get_info(szfilename, 0, error);
    if (*error)
    {
        tr_logAddError(
            fmt::format(
                fmt::runtime(_("Couldn't read '{path}': {error} ({error_code})")),
                fmt::arg("path", filename),
                fmt::arg("error", error->message()),
                fmt::arg("error_code", error->code())));
        return false;
    }

    if (!info || !info->isFile())
    {
        tr_logAddError(fmt::format(fmt::runtime(_("Couldn't read '{path}': Not a regular file")), fmt::arg("path", filename)));
        error->set(TR_ERROR_EISDIR, "Not a regular file"sv);
        return false;
    }

    /* Load the torrent file into our buffer */
    auto const fd = tr_sys_file_open(szfilename, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, error);
    if (fd == TR_BAD_SYS_FILE)
    {
        tr_logAddError(
            fmt::format(
                fmt::runtime(_("Couldn't read '{path}': {error} ({error_code})")),
                fmt::arg("path", filename),
                fmt::arg("error", error->message()),
                fmt::arg("error_code", error->code())));
        return false;
    }

    contents.resize(info->size);
    if (!tr_sys_file_read(fd, std::data(contents), info->size, nullptr, error))
    {
        tr_logAddError(
            fmt::format(
                fmt::runtime(_("Couldn't read '{path}': {error} ({error_code})")),
                fmt::arg("path", filename),
                fmt::arg("error", error->message()),
                fmt::arg("error_code", error->code())));
        tr_sys_file_close(fd);
        return false;
    }

    tr_sys_file_close(fd);
    return true;
}

bool tr_file_save(std::string_view filename, std::string_view contents, tr_error* error)
{
    // follow symlinks to find the "real" file, to make sure the temporary
    // we build with tr_sys_file_open_temp() is created on the right partition
    if (auto const realname = tr_sys_path_resolve(filename); !std::empty(realname) && realname != filename)
    {
        return tr_file_save(realname, contents, error);
    }

    // Write it to a temp file first.
    // This is a safeguard against edge cases, e.g. disk full, crash while writing, etc.
    auto tmp = tr_pathbuf{ filename, ".tmp.XXXXXX"sv };
    auto const fd = tr_sys_file_open_temp(std::data(tmp), error);
    if (fd == TR_BAD_SYS_FILE)
    {
        return false;
    }
#ifndef _WIN32
    // set file mode per settings umask()
    {
        auto const val = ::umask(0);
        ::umask(val);
        fchmod(fd, 0666 & ~val);
    }
#endif

    // Save the contents. This might take >1 pass.
    auto ok = true;
    while (!std::empty(contents))
    {
        auto n_written = uint64_t{};
        if (!tr_sys_file_write(fd, std::data(contents), std::size(contents), &n_written, error))
        {
            ok = false;
            break;
        }
        contents.remove_prefix(n_written);
    }

    // If we saved it to disk successfully, move it from '.tmp' to the correct filename
    if (!tr_sys_file_close(fd, error) || !ok || !tr_sys_path_rename(tmp, tr_pathbuf{ filename }, error))
    {
        return false;
    }

    tr_logAddTrace(fmt::format("Saved '{}'", filename));
    return true;
}

bool tr_file_move(std::string_view oldpath, std::string_view newpath, bool allow_copy, tr_error* error)
{
    auto local_error = tr_error{};
    if (error == nullptr)
    {
        error = &local_error;
    }

    // make sure the old file exists
    auto const info = tr_sys_path_get_info(oldpath, 0, error);
    if (!info)
    {
        error->prefix_message("Unable to get information on old file: ");
        return false;
    }
    if (!info->isFile())
    {
        error->set(TR_ERROR_EINVAL, "Old path does not point to a file."sv);
        return false;
    }

    // ensure the target directory exists
    if (!tr_sys_dir_create(tr_sys_path_dirname(newpath), TR_SYS_DIR_CREATE_PARENTS, 0777, error))
    {
        error->prefix_message("Unable to create directory for new file: ");
        return false;
    }

    if (allow_copy)
    {
        // they might be on the same filesystem...
        if (tr_sys_path_rename(oldpath, newpath))
        {
            return true;
        }
    }
    else
    {
        // do the actual moving
        if (tr_sys_path_rename(oldpath, newpath, error))
        {
            return true;
        }
        error->prefix_message("Unable to move file: ");
        return false;
    }

    /* Otherwise, copy the file. */
    if (!tr_sys_path_copy(oldpath, newpath, error))
    {
        error->prefix_message("Unable to copy: ");
        return false;
    }

    if (auto log_error = tr_error{}; !tr_sys_path_remove(oldpath, &log_error))
    {
        tr_logAddError(
            fmt::format(
                fmt::runtime(_("Couldn't remove '{path}': {error} ({error_code})")),
                fmt::arg("path", oldpath),
                fmt::arg("error", log_error.message()),
                fmt::arg("error_code", log_error.code())));
    }

    return true;
}

// --- mime-type

std::string_view tr_get_mime_type_for_filename(std::string_view filename)
{
    auto constexpr Compare = [](mime_type_suffix const& entry, auto const& suffix)
    {
        return entry.suffix < suffix;
    };

    if (auto const pos = filename.rfind('.'); pos != std::string_view::npos)
    {
        auto const suffix_lc = tr_strlower(filename.substr(pos + 1));
        auto const it = std::lower_bound(std::begin(MimeTypeSuffixes), std::end(MimeTypeSuffixes), suffix_lc, Compare);
        if (it != std::end(MimeTypeSuffixes) && suffix_lc == it->suffix)
        {
            std::string_view mime_type = it->mime_type;

            // https://github.com/transmission/transmission/issues/5965#issuecomment-1704421231
            // An mp4 file's correct mime-type depends on the codecs used in the file,
            // which we have no way of inspecting and which might not be downloaded yet.
            // Let's use `video/mp4` since that's by far the most common use case for torrents.
            if (mime_type == "application/mp4")
            {
                mime_type = "video/mp4";
            }

            return mime_type;
        }
    }

    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Basics_of_HTTP/MIME_types/Common_types
    // application/octet-stream is the default value.
    // An unknown file type should use this type.
    auto constexpr Fallback = "application/octet-stream"sv;
    return Fallback;
}
