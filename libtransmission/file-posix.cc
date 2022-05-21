// This file Copyright © 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#undef _GNU_SOURCE
#define _GNU_SOURCE // NOLINT

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits> /* PATH_MAX */
#include <cstdint> /* SIZE_MAX */
#include <cstdio>
#include <cstring>
#include <string_view>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h> /* O_LARGEFILE, posix_fadvise(), [posix_]fallocate(), fcntl() */
#include <libgen.h> /* basename(), dirname() */
#include <sys/file.h> /* flock() */
#include <sys/mman.h> /* mmap(), munmap() */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> /* lseek(), write(), ftruncate(), pread(), pwrite(), pathconf(), etc */

#ifdef HAVE_XFS_XFS_H
#include <xfs/xfs.h>
#endif

/* OS-specific file copy (copy_file_range, sendfile64, or copyfile). */
#if defined(__linux__)
#include <linux/version.h>
/* Linux's copy_file_range(2) is buggy prior to 5.3. */
#if defined(HAVE_COPY_FILE_RANGE) && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
#define USE_COPY_FILE_RANGE
#elif defined(HAVE_SENDFILE64)
#include <sys/sendfile.h>
#define USE_SENDFILE64
#endif
#elif defined(__APPLE__) && defined(HAVE_COPYFILE)
#include <copyfile.h>
#ifndef COPYFILE_CLONE /* macos < 10.12 */
#define COPYFILE_CLONE 0
#endif
#define USE_COPYFILE
#elif defined(HAVE_COPY_FILE_RANGE)
/* Presently this is only FreeBSD 13+. */
#define USE_COPY_FILE_RANGE
#endif /* __linux__ */

#include <fmt/format.h>

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "tr-assert.h"
#include "tr-strbuf.h"
#include "utils.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_SEQUENTIAL
#define O_SEQUENTIAL 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* don't use pread/pwrite on old versions of uClibc because they're buggy.
 * https://trac.transmissionbt.com/ticket/3826 */
#if defined(__UCLIBC__) && !TR_UCLIBC_CHECK_VERSION(0, 9, 28)
#undef HAVE_PREAD
#undef HAVE_PWRITE
#endif

#ifdef __APPLE__
#ifndef HAVE_PREAD
#define HAVE_PREAD
#endif
#ifndef HAVE_PWRITE
#define HAVE_PWRITE
#endif
#ifndef HAVE_MKDTEMP
#define HAVE_MKDTEMP
#endif
#endif

using namespace std::literals;

static void set_system_error(tr_error** error, int code)
{
    if (error == nullptr)
    {
        return;
    }

    tr_error_set(error, code, tr_strerror(code));
}

static void set_system_error_if_file_found(tr_error** error, int code)
{
    if (code != ENOENT)
    {
        set_system_error(error, code);
    }
}

static void stat_to_sys_path_info(struct stat const* sb, tr_sys_path_info* info)
{
    if (S_ISREG(sb->st_mode))
    {
        info->type = TR_SYS_PATH_IS_FILE;
    }
    else if (S_ISDIR(sb->st_mode))
    {
        info->type = TR_SYS_PATH_IS_DIRECTORY;
    }
    else
    {
        info->type = TR_SYS_PATH_IS_OTHER;
    }

    info->size = (uint64_t)sb->st_size;
    info->last_modified_at = sb->st_mtime;
}

static void set_file_for_single_pass(tr_sys_file_t handle)
{
    /* Set hints about the lookahead buffer and caching. It's okay
       for these to fail silently, so don't let them affect errno */

    int const err = errno;

    if (handle == TR_BAD_SYS_FILE)
    {
        return;
    }

#ifdef HAVE_POSIX_FADVISE

    (void)posix_fadvise(handle, 0, 0, POSIX_FADV_SEQUENTIAL);

#endif

#ifdef __APPLE__

    (void)fcntl(handle, F_RDAHEAD, 1);
    (void)fcntl(handle, F_NOCACHE, 1);

#endif

    errno = err;
}

#ifndef HAVE_MKDIRP

static bool create_path_require_dir(char const* path, tr_error** error)
{
    struct stat sb;

    if (stat(path, &sb) == -1)
    {
        set_system_error(error, errno);
        return false;
    }

    if ((sb.st_mode & S_IFMT) != S_IFDIR)
    {
        tr_error_set(error, ENOTDIR, fmt::format(FMT_STRING("File is in the way: {:s}"), path));
        return false;
    }

    return true;
}

static bool create_path(char const* path_in, int permissions, tr_error** error)
{
    /* make a temporary copy of path */
    char* const path = tr_strdup(path_in);

    /* walk past the root */
    char* p = path;

    while (*p == TR_PATH_DELIMITER)
    {
        ++p;
    }

    char* path_end = p + strlen(p);

    while (path_end > path && *path_end == TR_PATH_DELIMITER)
    {
        --path_end;
    }

    char* pp = nullptr;
    bool ret = false;
    tr_error* my_error = nullptr;

    /* Go one level up on each iteration and attempt to create */
    for (pp = path_end; pp != nullptr; pp = strrchr(p, TR_PATH_DELIMITER))
    {
        *pp = '\0';

        ret = mkdir(path, permissions) != -1;

        if (ret)
        {
            break;
        }

        if (errno == EEXIST)
        {
            ret = create_path_require_dir(path, &my_error);

            if (ret)
            {
                break;
            }

            goto FAILURE;
        }

        if (errno != ENOENT)
        {
            set_system_error(&my_error, errno);
            goto FAILURE;
        }
    }

    if (ret && pp == path_end)
    {
        goto CLEANUP;
    }

    /* Go one level down on each iteration and attempt to create */
    for (pp = pp == nullptr ? p + strlen(p) : pp; pp < path_end; pp += strlen(pp))
    {
        *pp = TR_PATH_DELIMITER;

        if (mkdir(path, permissions) == -1)
        {
            break;
        }
    }

    ret = create_path_require_dir(path, &my_error);

    if (ret)
    {
        goto CLEANUP;
    }

FAILURE:

    TR_ASSERT(!ret);
    TR_ASSERT(my_error != nullptr);

    tr_logAddError(fmt::format(
        _("Couldn't create '{path}': {error} ({error_code})"),
        fmt::arg("path", path),
        fmt::arg("error", my_error->message),
        fmt::arg("error_code", my_error->code)));
    tr_error_propagate(error, &my_error);

CLEANUP:

    TR_ASSERT(my_error == nullptr);

    tr_free(path);
    return ret;
}

#endif

bool tr_sys_path_exists(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    bool ret = access(path, F_OK) != -1;

    if (!ret)
    {
        set_system_error_if_file_found(error, errno);
    }

    return ret;
}

bool tr_sys_path_get_info(char const* path, int flags, tr_sys_path_info* info, tr_error** error)
{
    TR_ASSERT(path != nullptr);
    TR_ASSERT(info != nullptr);

    bool ret = false;
    struct stat sb;

    if ((flags & TR_SYS_PATH_NO_FOLLOW) == 0)
    {
        ret = stat(path, &sb) != -1;
    }
    else
    {
        ret = lstat(path, &sb) != -1;
    }

    if (ret)
    {
        stat_to_sys_path_info(&sb, info);
    }
    else
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_path_is_relative(std::string_view path)
{
    return std::empty(path) || path.front() != '/';
}

bool tr_sys_path_is_same(char const* path1, char const* path2, tr_error** error)
{
    TR_ASSERT(path1 != nullptr);
    TR_ASSERT(path2 != nullptr);

    bool ret = false;
    struct stat sb1;
    struct stat sb2;

    if (stat(path1, &sb1) != -1 && stat(path2, &sb2) != -1)
    {
        ret = sb1.st_dev == sb2.st_dev && sb1.st_ino == sb2.st_ino;
    }
    else
    {
        set_system_error_if_file_found(error, errno);
    }

    return ret;
}

char* tr_sys_path_resolve(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    char* ret = nullptr;

#if defined(HAVE_CANONICALIZE_FILE_NAME)

    ret = canonicalize_file_name(path);

#endif

    if (ret == nullptr)
    {
        char tmp[PATH_MAX];
        ret = realpath(path, tmp);

        if (ret != nullptr)
        {
            ret = tr_strdup(ret);
        }
    }

    if (ret == nullptr)
    {
        set_system_error(error, errno);
    }

    return ret;
}

std::string tr_sys_path_basename(std::string_view path, tr_error** error)
{
    auto tmp = tr_pathbuf{ path };

    if (char const* ret = basename(std::data(tmp)); ret != nullptr)
    {
        return ret;
    }

    set_system_error(error, errno);
    return {};
}

// This function is adapted from Node.js's path.posix.dirname() function,
// which is copyrighted by Joyent, Inc. and other Node contributors
// and is distributed under MIT (SPDX:MIT) license.
std::string_view tr_sys_path_dirname(std::string_view path)
{
    auto const len = std::size(path);

    if (len == 0U)
    {
        return "."sv;
    }

    auto const has_root = path[0] == '/';
    auto end = std::string_view::npos;
    auto matched_slash = bool{ true };

    for (auto i = len - 1; i >= 1U; --i)
    {
        if (path[i] == '/')
        {
            if (!matched_slash)
            {
                end = i;
                break;
            }
        }
        else
        {
            // We saw the first non-path separator
            matched_slash = false;
        }
    }

    if (end == std::string_view::npos)
    {
        return has_root ? "/"sv : "."sv;
    }

    if (has_root && end == 1)
    {
        return "//"sv;
    }

    return path.substr(0, end);
}

bool tr_sys_path_rename(char const* src_path, char const* dst_path, tr_error** error)
{
    TR_ASSERT(src_path != nullptr);
    TR_ASSERT(dst_path != nullptr);

    bool const ret = rename(src_path, dst_path) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

/* We try to do a fast (in-kernel) copy using a variety of non-portable system
 * calls. If the current implementation does not support in-kernel copying, we
 * use a user-space fallback instead. */
bool tr_sys_path_copy(char const* src_path, char const* dst_path, tr_error** error)
{
    TR_ASSERT(src_path != nullptr);
    TR_ASSERT(dst_path != nullptr);

#if defined(USE_COPYFILE)
    if (copyfile(src_path, dst_path, nullptr, COPYFILE_CLONE | COPYFILE_ALL) < 0)
    {
        set_system_error(error, errno);
        return false;
    }

    return true;

#else /* USE_COPYFILE */

    /* Other OSes require us to copy between file descriptors, so open them. */
    tr_sys_file_t in = tr_sys_file_open(src_path, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, error);
    if (in == TR_BAD_SYS_FILE)
    {
        tr_error_prefix(error, "Unable to open source file: ");
        return false;
    }

    tr_sys_path_info info;
    if (!tr_sys_file_get_info(in, &info, error))
    {
        tr_error_prefix(error, "Unable to get information on source file: ");
        tr_sys_file_close(in);
        return false;
    }

    tr_sys_file_t out = tr_sys_file_open(dst_path, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0666, error);
    if (out == TR_BAD_SYS_FILE)
    {
        tr_error_prefix(error, "Unable to open destination file: ");
        tr_sys_file_close(in);
        return false;
    }

    uint64_t file_size = info.size;

#if defined(USE_COPY_FILE_RANGE) || defined(USE_SENDFILE64)

    while (file_size > 0)
    {
        size_t const chunk_size = std::min(file_size, uint64_t{ SSIZE_MAX });
        ssize_t const copied =
#ifdef USE_COPY_FILE_RANGE
            copy_file_range(in, nullptr, out, nullptr, chunk_size, 0);
#elif defined(USE_SENDFILE64)
            sendfile64(out, in, nullptr, chunk_size);
#else
#error File copy mechanism not implemented.
#endif
        TR_ASSERT(copied == -1 || copied >= 0); /* -1 for error; some non-negative value otherwise. */

        if (copied == -1)
        {
            set_system_error(error, errno);
            break;
        }

        TR_ASSERT(copied >= 0 && ((uint64_t)copied) <= file_size);
        TR_ASSERT(copied >= 0 && ((uint64_t)copied) <= chunk_size);
        file_size -= copied;
    }

#else /* USE_COPY_FILE_RANGE || USE_SENDFILE64 */

    /* Fallback to user-space copy. */

    size_t const buflen = 1024 * 1024; /* 1024 KiB buffer */
    auto* buf = static_cast<char*>(tr_malloc(buflen));

    while (file_size > 0)
    {
        uint64_t const chunk_size = std::min(file_size, uint64_t{ buflen });
        uint64_t bytes_read;
        uint64_t bytes_written;

        if (!tr_sys_file_read(in, buf, chunk_size, &bytes_read, error))
        {
            break;
        }

        if (!tr_sys_file_write(out, buf, bytes_read, &bytes_written, error))
        {
            break;
        }

        TR_ASSERT(bytes_read == bytes_written);
        TR_ASSERT(bytes_written <= file_size);
        file_size -= bytes_written;
    }

    /* cleanup */
    tr_free(buf);

#endif /* USE_COPY_FILE_RANGE || USE_SENDFILE64 */

    /* cleanup */
    tr_sys_file_close(out);
    tr_sys_file_close(in);

    if (file_size != 0)
    {
        tr_error_prefix(error, "Unable to read/write: ");
        return false;
    }

    return true;

#endif /* USE_COPYFILE */
}

bool tr_sys_path_remove(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    bool const ret = remove(path) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

char* tr_sys_path_native_separators(char* path)
{
    return path;
}

tr_sys_file_t tr_sys_file_get_std(tr_std_sys_file_t std_file, tr_error** error)
{
    tr_sys_file_t ret = TR_BAD_SYS_FILE;

    switch (std_file)
    {
    case TR_STD_SYS_FILE_IN:
        ret = STDIN_FILENO;
        break;

    case TR_STD_SYS_FILE_OUT:
        ret = STDOUT_FILENO;
        break;

    case TR_STD_SYS_FILE_ERR:
        ret = STDERR_FILENO;
        break;

    default:
        TR_ASSERT_MSG(false, fmt::format(FMT_STRING("unknown standard file {:d}"), std_file));
        set_system_error(error, EINVAL);
    }

    return ret;
}

tr_sys_file_t tr_sys_file_open(char const* path, int flags, int permissions, tr_error** error)
{
    TR_ASSERT(path != nullptr);
    TR_ASSERT((flags & (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE)) != 0);

    struct native_map_item
    {
        int symbolic_mask;
        int symbolic_value;
        int native_value;
    };

    auto constexpr native_map = std::array<native_map_item, 8>{
        { { TR_SYS_FILE_READ | TR_SYS_FILE_WRITE, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE, O_RDWR },
          { TR_SYS_FILE_READ | TR_SYS_FILE_WRITE, TR_SYS_FILE_READ, O_RDONLY },
          { TR_SYS_FILE_READ | TR_SYS_FILE_WRITE, TR_SYS_FILE_WRITE, O_WRONLY },
          { TR_SYS_FILE_CREATE, TR_SYS_FILE_CREATE, O_CREAT },
          { TR_SYS_FILE_CREATE_NEW, TR_SYS_FILE_CREATE_NEW, O_CREAT | O_EXCL },
          { TR_SYS_FILE_APPEND, TR_SYS_FILE_APPEND, O_APPEND },
          { TR_SYS_FILE_TRUNCATE, TR_SYS_FILE_TRUNCATE, O_TRUNC },
          { TR_SYS_FILE_SEQUENTIAL, TR_SYS_FILE_SEQUENTIAL, O_SEQUENTIAL } }
    };

    int native_flags = O_BINARY | O_LARGEFILE | O_CLOEXEC;

    for (auto const& item : native_map)
    {
        if ((flags & item.symbolic_mask) == item.symbolic_value)
        {
            native_flags |= item.native_value;
        }
    }

    tr_sys_file_t const ret = open(path, native_flags, permissions);

    if (ret != TR_BAD_SYS_FILE)
    {
        if ((flags & TR_SYS_FILE_SEQUENTIAL) != 0)
        {
            set_file_for_single_pass(ret);
        }
    }
    else
    {
        set_system_error(error, errno);
    }

    return ret;
}

tr_sys_file_t tr_sys_file_open_temp(char* path_template, tr_error** error)
{
    TR_ASSERT(path_template != nullptr);

    tr_sys_file_t const ret = mkstemp(path_template);

    if (ret == TR_BAD_SYS_FILE)
    {
        set_system_error(error, errno);
    }

    set_file_for_single_pass(ret);

    return ret;
}

bool tr_sys_file_close(tr_sys_file_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool const ret = close(handle) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_get_info(tr_sys_file_t handle, tr_sys_path_info* info, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(info != nullptr);

    struct stat sb;
    bool const ret = fstat(handle, &sb) != -1;

    if (ret)
    {
        stat_to_sys_path_info(&sb, info);
    }
    else
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_seek(tr_sys_file_t handle, int64_t offset, tr_seek_origin_t origin, uint64_t* new_offset, tr_error** error)
{
    static_assert(TR_SEEK_SET == SEEK_SET, "values should match");
    static_assert(TR_SEEK_CUR == SEEK_CUR, "values should match");
    static_assert(TR_SEEK_END == SEEK_END, "values should match");

    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(origin == TR_SEEK_SET || origin == TR_SEEK_CUR || origin == TR_SEEK_END);

    bool ret = false;

    off_t const my_new_offset = lseek(handle, offset, origin);
    static_assert(sizeof(*new_offset) >= sizeof(my_new_offset));

    if (my_new_offset != -1)
    {
        if (new_offset != nullptr)
        {
            *new_offset = my_new_offset;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_read(tr_sys_file_t handle, void* buffer, uint64_t size, uint64_t* bytes_read, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    bool ret = false;

    ssize_t const my_bytes_read = read(handle, buffer, size);
    static_assert(sizeof(*bytes_read) >= sizeof(my_bytes_read));

    if (my_bytes_read != -1)
    {
        if (bytes_read != nullptr)
        {
            *bytes_read = my_bytes_read;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_read_at(
    tr_sys_file_t handle,
    void* buffer,
    uint64_t size,
    uint64_t offset,
    uint64_t* bytes_read,
    tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);
    /* seek requires signed offset, so it should be in mod range */
    TR_ASSERT(offset < UINT64_MAX / 2);

    bool ret = false;

#ifdef HAVE_PREAD

    ssize_t const my_bytes_read = pread(handle, buffer, size, offset);

#else

    ssize_t const my_bytes_read = lseek(handle, offset, SEEK_SET) == -1 ? -1 : read(handle, buffer, size);

#endif

    static_assert(sizeof(*bytes_read) >= sizeof(my_bytes_read));

    if (my_bytes_read > 0)
    {
        if (bytes_read != nullptr)
        {
            *bytes_read = my_bytes_read;
        }

        ret = true;
    }
    else if (my_bytes_read == -1)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_write(tr_sys_file_t handle, void const* buffer, uint64_t size, uint64_t* bytes_written, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    bool ret = false;

    ssize_t const my_bytes_written = write(handle, buffer, size);
    static_assert(sizeof(*bytes_written) >= sizeof(my_bytes_written));

    if (my_bytes_written != -1)
    {
        if (bytes_written != nullptr)
        {
            *bytes_written = my_bytes_written;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_write_at(
    tr_sys_file_t handle,
    void const* buffer,
    uint64_t size,
    uint64_t offset,
    uint64_t* bytes_written,
    tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);
    /* seek requires signed offset, so it should be in mod range */
    TR_ASSERT(offset < UINT64_MAX / 2);

    bool ret = false;

#ifdef HAVE_PWRITE

    ssize_t const my_bytes_written = pwrite(handle, buffer, size, offset);

#else

    ssize_t const my_bytes_written = lseek(handle, offset, SEEK_SET) == -1 ? -1 : write(handle, buffer, size);

#endif

    static_assert(sizeof(*bytes_written) >= sizeof(my_bytes_written));

    if (my_bytes_written != -1)
    {
        if (bytes_written != nullptr)
        {
            *bytes_written = my_bytes_written;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_flush(tr_sys_file_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool ret = fsync(handle) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_truncate(tr_sys_file_t handle, uint64_t size, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool ret = ftruncate(handle, size) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_advise(
    [[maybe_unused]] tr_sys_file_t handle,
    [[maybe_unused]] uint64_t offset,
    [[maybe_unused]] uint64_t size,
    [[maybe_unused]] tr_sys_file_advice_t advice,
    [[maybe_unused]] tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(size > 0);
    TR_ASSERT(advice == TR_SYS_FILE_ADVICE_WILL_NEED || advice == TR_SYS_FILE_ADVICE_DONT_NEED);

    bool ret = true;

#if defined(HAVE_POSIX_FADVISE)

    int const native_advice = advice == TR_SYS_FILE_ADVICE_WILL_NEED ?
        POSIX_FADV_WILLNEED :
        (advice == TR_SYS_FILE_ADVICE_DONT_NEED ? POSIX_FADV_DONTNEED : POSIX_FADV_NORMAL);

    TR_ASSERT(native_advice != POSIX_FADV_NORMAL);

    if (int const code = posix_fadvise(handle, offset, size, native_advice); code != 0)
    {
        set_system_error(error, code);
        ret = false;
    }

#elif defined(__APPLE__)

    if (advice == TR_SYS_FILE_ADVICE_WILL_NEED)
    {
        auto radv = radvisory{};
        radv.ra_offset = offset;
        radv.ra_count = size;

        ret = fcntl(handle, F_RDADVISE, &radv) != -1;

        if (!ret)
        {
            set_system_error(error, errno);
        }
    }

#endif

    return ret;
}

namespace
{

#ifdef HAVE_FALLOCATE64
bool preallocate_fallocate64(tr_sys_file_t handle, uint64_t size)
{
    return fallocate64(handle, 0, 0, size) == 0;
}
#endif

#ifdef HAVE_XFS_XFS_H
bool full_preallocate_xfs(tr_sys_file_t handle, uint64_t size)
{
    if (platform_test_xfs_fd(handle) == 0) // true if on xfs filesystem
    {
        return false;
    }

    xfs_flock64_t fl;
    fl.l_whence = 0;
    fl.l_start = 0;
    fl.l_len = size;

    // The blocks are allocated, but not zeroed, and the file size does not change
    bool ok = xfsctl(nullptr, handle, XFS_IOC_RESVSP64, &fl) != -1;

    if (ok)
    {
        ok = ftruncate(handle, size) == 0;
    }

    return ok;
}
#endif

#ifdef __APPLE__
bool full_preallocate_apple(tr_sys_file_t handle, uint64_t size)
{
    fstore_t fst;

    fst.fst_flags = F_ALLOCATEALL;
    fst.fst_posmode = F_PEOFPOSMODE;
    fst.fst_offset = 0;
    fst.fst_length = size;
    fst.fst_bytesalloc = 0;

    bool ok = fcntl(handle, F_PREALLOCATE, &fst) != -1;

    if (ok)
    {
        ok = ftruncate(handle, size) == 0;
    }

    return ok;
}
#endif

#ifdef HAVE_POSIX_FALLOCATE
bool full_preallocate_posix(tr_sys_file_t handle, uint64_t size)
{
    return posix_fallocate(handle, 0, size) == 0;
}
#endif

} // unnamed namespace

bool tr_sys_file_preallocate(tr_sys_file_t handle, uint64_t size, int flags, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    using prealloc_func = bool (*)(tr_sys_file_t, uint64_t);

    // these approaches are fast and should be tried first
    auto approaches = std::vector<prealloc_func>{
#ifdef HAVE_FALLOCATE64
        preallocate_fallocate64
#endif
    };

    // these approaches are sometimes slower in some settings (e.g.
    // a slow zeroing of all the preallocated space) so only use them
    // if specified by `flags`
    if ((flags & TR_SYS_FILE_PREALLOC_SPARSE) == 0)
    {
        // TODO: these functions haven't been reviewed in awhile.
        // It's possible that some are faster now & should be promoted
        // to 'always try' and/or replaced with fresher platform API.
        approaches.insert(
            std::end(approaches),
            {
#ifdef HAVE_XFS_XFS_H
                full_preallocate_xfs,
#endif
#ifdef __APPLE__
                full_preallocate_apple,
#endif
#ifdef HAVE_POSIX_FALLOCATE
                full_preallocate_posix,
#endif
            });
    }

    for (auto& approach : approaches) // try until one of them works
    {
        errno = 0;

        if (auto const success = approach(handle, size); success)
        {
            return success;
        }

        if (errno == ENOSPC) // disk full, so subsequent approaches will fail too
        {
            break;
        }
    }

    set_system_error(error, errno);
    return false;
}

void* tr_sys_file_map_for_reading(tr_sys_file_t handle, uint64_t offset, uint64_t size, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(size > 0);

    void* ret = mmap(nullptr, size, PROT_READ, MAP_SHARED, handle, offset);

    if (ret == MAP_FAILED) // NOLINT(performance-no-int-to-ptr)
    {
        set_system_error(error, errno);
        ret = nullptr;
    }

    return ret;
}

bool tr_sys_file_unmap(void const* address, uint64_t size, tr_error** error)
{
    TR_ASSERT(address != nullptr);
    TR_ASSERT(size > 0);

    bool ret = munmap((void*)address, size) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_lock([[maybe_unused]] tr_sys_file_t handle, [[maybe_unused]] int operation, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT((operation & ~(TR_SYS_FILE_LOCK_SH | TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB | TR_SYS_FILE_LOCK_UN)) == 0);
    TR_ASSERT(
        !!(operation & TR_SYS_FILE_LOCK_SH) + !!(operation & TR_SYS_FILE_LOCK_EX) + !!(operation & TR_SYS_FILE_LOCK_UN) == 1);

    bool ret = false;

#if defined(F_OFD_SETLK)

    struct flock fl = {};

    switch (operation & (TR_SYS_FILE_LOCK_SH | TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_UN))
    {
    case TR_SYS_FILE_LOCK_SH:
        fl.l_type = F_RDLCK;
        break;

    case TR_SYS_FILE_LOCK_EX:
        fl.l_type = F_WRLCK;
        break;

    case TR_SYS_FILE_LOCK_UN:
        fl.l_type = F_UNLCK;
        break;
    }

    fl.l_whence = SEEK_SET;

    do
    {
        ret = fcntl(handle, (operation & TR_SYS_FILE_LOCK_NB) != 0 ? F_OFD_SETLK : F_OFD_SETLKW, &fl) != -1;
    } while (!ret && errno == EINTR);

    if (!ret && errno == EAGAIN)
    {
        errno = EWOULDBLOCK;
    }

#elif defined(HAVE_FLOCK)

    int native_operation = 0;

    if ((operation & TR_SYS_FILE_LOCK_SH) != 0)
    {
        native_operation |= LOCK_SH;
    }

    if ((operation & TR_SYS_FILE_LOCK_EX) != 0)
    {
        native_operation |= LOCK_EX;
    }

    if ((operation & TR_SYS_FILE_LOCK_NB) != 0)
    {
        native_operation |= LOCK_NB;
    }

    if ((operation & TR_SYS_FILE_LOCK_UN) != 0)
    {
        native_operation |= LOCK_UN;
    }

    do
    {
        ret = flock(handle, native_operation) != -1;
    } while (!ret && errno == EINTR);

#else

    errno = ENOSYS;
    ret = false;

#endif

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

char* tr_sys_dir_get_current(tr_error** error)
{
    char* ret = getcwd(nullptr, 0);

    if (ret == nullptr && (errno == EINVAL || errno == ERANGE))
    {
        size_t size = PATH_MAX;
        char* tmp = nullptr;

        do
        {
            tmp = tr_renew(char, tmp, size);

            if (tmp == nullptr)
            {
                break;
            }

            ret = getcwd(tmp, size);
            size += 2048;
        } while (ret == nullptr && errno == ERANGE);

        if (ret == nullptr)
        {
            int const err = errno;
            tr_free(tmp);
            errno = err;
        }
    }

    if (ret == nullptr)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_dir_create(char const* path, int flags, int permissions, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    bool ret = false;
    tr_error* my_error = nullptr;

    if ((flags & TR_SYS_DIR_CREATE_PARENTS) != 0)
    {
#ifdef HAVE_MKDIRP
        ret = mkdirp(path, permissions) != -1;
#else
        ret = create_path(path, permissions, &my_error);
#endif
    }
    else
    {
        ret = mkdir(path, permissions) != -1;
    }

    if (!ret && errno == EEXIST)
    {
        struct stat sb;

        if (stat(path, &sb) != -1 && S_ISDIR(sb.st_mode))
        {
            tr_error_clear(&my_error);
            ret = true;
        }
        else
        {
            errno = EEXIST;
        }
    }

    if (!ret)
    {
        if (my_error != nullptr)
        {
            tr_error_propagate(error, &my_error);
        }
        else
        {
            set_system_error(error, errno);
        }
    }

    return ret;
}

bool tr_sys_dir_create_temp(char* path_template, tr_error** error)
{
    TR_ASSERT(path_template != nullptr);

#ifdef HAVE_MKDTEMP

    bool const ret = mkdtemp(path_template) != nullptr;

#else

    bool const ret = mktemp(path_template) != nullptr && mkdir(path_template, 0700) != -1;

#endif

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

tr_sys_dir_t tr_sys_dir_open(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    DIR* ret = opendir(path);

    if (ret == nullptr)
    {
        set_system_error(error, errno);
        return TR_BAD_SYS_DIR;
    }

    return (tr_sys_dir_t)ret;
}

char const* tr_sys_dir_read_name(tr_sys_dir_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_DIR);

    char const* ret = nullptr;

    errno = 0;

    if (auto const* const entry = readdir((DIR*)handle); entry != nullptr)
    {
        ret = entry->d_name;
    }
    else if (errno != 0)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_dir_close(tr_sys_dir_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_DIR);

    bool ret = closedir((DIR*)handle) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}
