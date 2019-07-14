/*
 * This file Copyright (C) 2013-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#undef _GNU_SOURCE
#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h> /* O_LARGEFILE, posix_fadvise(), [posix_]fallocate(), fcntl() */
#include <libgen.h> /* basename(), dirname() */
#include <limits.h> /* PATH_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h> /* flock() */
#include <sys/mman.h> /* mmap(), munmap() */
#include <sys/stat.h>
#include <unistd.h> /* lseek(), write(), ftruncate(), pread(), pwrite(), pathconf(), etc */

#ifdef HAVE_XFS_XFS_H
#include <xfs/xfs.h>
#endif

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "platform.h"
#include "tr-assert.h"
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

static void set_system_error(tr_error** error, int code)
{
    if (error == NULL)
    {
        return;
    }

    tr_error_set_literal(error, code, tr_strerror(code));
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
        tr_error_set(error, ENOTDIR, _("File \"%s\" is in the way"), path);
        return false;
    }

    return true;
}

static bool create_path(char const* path_in, int permissions, tr_error** error)
{
    /* make a temporary copy of path */
    char* path = tr_strdup(path_in);

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

    char* pp;
    bool ret = false;
    tr_error* my_error = NULL;

    /* Go one level up on each iteration and attempt to create */
    for (pp = path_end; pp != NULL; pp = strrchr(p, TR_PATH_DELIMITER))
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

            goto failure;
        }

        if (errno != ENOENT)
        {
            set_system_error(&my_error, errno);
            goto failure;
        }
    }

    if (ret && pp == path_end)
    {
        goto cleanup;
    }

    /* Go one level down on each iteration and attempt to create */
    for (pp = pp == NULL ? p + strlen(p) : pp; pp < path_end; pp += strlen(pp))
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
        goto cleanup;
    }

failure:

    TR_ASSERT(!ret);
    TR_ASSERT(my_error != NULL);

    tr_logAddError(_("Couldn't create \"%1$s\": %2$s"), path, my_error->message);
    tr_error_propagate(error, &my_error);

cleanup:

    TR_ASSERT(my_error == NULL);

    tr_free(path);
    return ret;
}

#endif

bool tr_sys_path_exists(char const* path, tr_error** error)
{
    TR_ASSERT(path != NULL);

    bool ret = access(path, F_OK) != -1;

    if (!ret)
    {
        set_system_error_if_file_found(error, errno);
    }

    return ret;
}

bool tr_sys_path_get_info(char const* path, int flags, tr_sys_path_info* info, tr_error** error)
{
    TR_ASSERT(path != NULL);
    TR_ASSERT(info != NULL);

    bool ret;
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

bool tr_sys_path_is_relative(char const* path)
{
    TR_ASSERT(path != NULL);

    return path[0] != '/';
}

bool tr_sys_path_is_same(char const* path1, char const* path2, tr_error** error)
{
    TR_ASSERT(path1 != NULL);
    TR_ASSERT(path2 != NULL);

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
    TR_ASSERT(path != NULL);

    char* ret = NULL;
    char* tmp = NULL;

#if defined(HAVE_CANONICALIZE_FILE_NAME)

    ret = canonicalize_file_name(path);

#endif

#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L

    /* Better safe than sorry: realpath() officially supports NULL as destination
       starting off POSIX.1-2008. */

    if (ret == NULL)
    {
        ret = realpath(path, NULL);
    }

#endif

    if (ret == NULL)
    {
        tmp = tr_new(char, PATH_MAX);
        ret = realpath(path, tmp);

        if (ret != NULL)
        {
            ret = tr_strdup(ret);
        }
    }

    if (ret == NULL)
    {
        set_system_error(error, errno);
    }

    tr_free(tmp);

    return ret;
}

char* tr_sys_path_basename(char const* path, tr_error** error)
{
    TR_ASSERT(path != NULL);

    char* ret = NULL;
    char* tmp;

    tmp = tr_strdup(path);
    ret = basename(tmp);

    if (ret != NULL)
    {
        ret = tr_strdup(ret);
    }
    else
    {
        set_system_error(error, errno);
    }

    tr_free(tmp);

    return ret;
}

char* tr_sys_path_dirname(char const* path, tr_error** error)
{
    TR_ASSERT(path != NULL);

    char* tmp = tr_strdup(path);
    char* ret = dirname(tmp);

    if (ret != NULL)
    {
        ret = tr_strdup(ret);
    }
    else
    {
        set_system_error(error, errno);
    }

    tr_free(tmp);

    return ret;
}

bool tr_sys_path_rename(char const* src_path, char const* dst_path, tr_error** error)
{
    TR_ASSERT(src_path != NULL);
    TR_ASSERT(dst_path != NULL);

    bool ret = rename(src_path, dst_path) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_path_remove(char const* path, tr_error** error)
{
    TR_ASSERT(path != NULL);

    bool ret = remove(path) != -1;

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
        TR_ASSERT_MSG(false, "unknown standard file %d", (int)std_file);
        set_system_error(error, EINVAL);
    }

    return ret;
}

tr_sys_file_t tr_sys_file_open(char const* path, int flags, int permissions, tr_error** error)
{
    TR_ASSERT(path != NULL);
    TR_ASSERT((flags & (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE)) != 0);

    tr_sys_file_t ret;
    int native_flags = 0;

    if ((flags & (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE)) == (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE))
    {
        native_flags |= O_RDWR;
    }
    else if ((flags & TR_SYS_FILE_READ) != 0)
    {
        native_flags |= O_RDONLY;
    }
    else if ((flags & TR_SYS_FILE_WRITE) != 0)
    {
        native_flags |= O_WRONLY;
    }

    native_flags |=
        ((flags & TR_SYS_FILE_CREATE) != 0 ? O_CREAT : 0) |
        ((flags & TR_SYS_FILE_CREATE_NEW) != 0 ? O_CREAT | O_EXCL : 0) |
        ((flags & TR_SYS_FILE_APPEND) != 0 ? O_APPEND : 0) |
        ((flags & TR_SYS_FILE_TRUNCATE) != 0 ? O_TRUNC : 0) |
        ((flags & TR_SYS_FILE_SEQUENTIAL) != 0 ? O_SEQUENTIAL : 0) |
        O_BINARY | O_LARGEFILE | O_CLOEXEC;

    ret = open(path, native_flags, permissions);

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
    TR_ASSERT(path_template != NULL);

    tr_sys_file_t ret = mkstemp(path_template);

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

    bool ret = close(handle) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_get_info(tr_sys_file_t handle, tr_sys_path_info* info, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(info != NULL);

    struct stat sb;
    bool ret = fstat(handle, &sb) != -1;

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
    TR_STATIC_ASSERT(TR_SEEK_SET == SEEK_SET, "values should match");
    TR_STATIC_ASSERT(TR_SEEK_CUR == SEEK_CUR, "values should match");
    TR_STATIC_ASSERT(TR_SEEK_END == SEEK_END, "values should match");

    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(origin == TR_SEEK_SET || origin == TR_SEEK_CUR || origin == TR_SEEK_END);

    bool ret = false;
    off_t my_new_offset;

    TR_STATIC_ASSERT(sizeof(*new_offset) >= sizeof(my_new_offset), "");

    my_new_offset = lseek(handle, offset, origin);

    if (my_new_offset != -1)
    {
        if (new_offset != NULL)
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
    TR_ASSERT(buffer != NULL || size == 0);

    bool ret = false;
    ssize_t my_bytes_read;

    TR_STATIC_ASSERT(sizeof(*bytes_read) >= sizeof(my_bytes_read), "");

    my_bytes_read = read(handle, buffer, size);

    if (my_bytes_read != -1)
    {
        if (bytes_read != NULL)
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

bool tr_sys_file_read_at(tr_sys_file_t handle, void* buffer, uint64_t size, uint64_t offset, uint64_t* bytes_read,
    tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != NULL || size == 0);
    /* seek requires signed offset, so it should be in mod range */
    TR_ASSERT(offset < UINT64_MAX / 2);

    bool ret = false;
    ssize_t my_bytes_read;

    TR_STATIC_ASSERT(sizeof(*bytes_read) >= sizeof(my_bytes_read), "");

#ifdef HAVE_PREAD

    my_bytes_read = pread(handle, buffer, size, offset);

#else

    if (lseek(handle, offset, SEEK_SET) != -1)
    {
        my_bytes_read = read(handle, buffer, size);
    }
    else
    {
        my_bytes_read = -1;
    }

#endif

    if (my_bytes_read != -1)
    {
        if (bytes_read != NULL)
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

bool tr_sys_file_write(tr_sys_file_t handle, void const* buffer, uint64_t size, uint64_t* bytes_written, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != NULL || size == 0);

    bool ret = false;
    ssize_t my_bytes_written;

    TR_STATIC_ASSERT(sizeof(*bytes_written) >= sizeof(my_bytes_written), "");

    my_bytes_written = write(handle, buffer, size);

    if (my_bytes_written != -1)
    {
        if (bytes_written != NULL)
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

bool tr_sys_file_write_at(tr_sys_file_t handle, void const* buffer, uint64_t size, uint64_t offset, uint64_t* bytes_written,
    tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != NULL || size == 0);
    /* seek requires signed offset, so it should be in mod range */
    TR_ASSERT(offset < UINT64_MAX / 2);

    bool ret = false;
    ssize_t my_bytes_written;

    TR_STATIC_ASSERT(sizeof(*bytes_written) >= sizeof(my_bytes_written), "");

#ifdef HAVE_PWRITE

    my_bytes_written = pwrite(handle, buffer, size, offset);

#else

    if (lseek(handle, offset, SEEK_SET) != -1)
    {
        my_bytes_written = write(handle, buffer, size);
    }
    else
    {
        my_bytes_written = -1;
    }

#endif

    if (my_bytes_written != -1)
    {
        if (bytes_written != NULL)
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

bool tr_sys_file_advise(tr_sys_file_t handle, uint64_t offset, uint64_t size, tr_sys_file_advice_t advice, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(size > 0);
    TR_ASSERT(advice == TR_SYS_FILE_ADVICE_WILL_NEED || advice == TR_SYS_FILE_ADVICE_DONT_NEED);

    bool ret = true;

#if defined(HAVE_POSIX_FADVISE)

    int const native_advice = advice == TR_SYS_FILE_ADVICE_WILL_NEED ? POSIX_FADV_WILLNEED :
        (advice == TR_SYS_FILE_ADVICE_DONT_NEED ? POSIX_FADV_DONTNEED : POSIX_FADV_NORMAL);

    TR_ASSERT(native_advice != POSIX_FADV_NORMAL);

    int const code = posix_fadvise(handle, offset, size, native_advice);

    if (code != 0)
    {
        set_system_error(error, code);
        ret = false;
    }

#elif defined(__APPLE__)

    if (advice != TR_SYS_FILE_ADVICE_WILL_NEED)
    {
        goto skip_darwin_fcntl;
    }

    struct radvisory const radv =
    {
        .ra_offset = offset,
        .ra_count = size
    };

    ret = fcntl(handle, F_RDADVISE, &radv) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

skip_darwin_fcntl:

#else

    (void)handle;
    (void)offset;
    (void)size;
    (void)advice;
    (void)error;

#endif

    return ret;
}

bool tr_sys_file_preallocate(tr_sys_file_t handle, uint64_t size, int flags, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool ret = false;

    errno = 0;

#ifdef HAVE_FALLOCATE64

    /* fallocate64 is always preferred, so try it first */
    ret = fallocate64(handle, 0, 0, size) != -1;

    if (ret || errno == ENOSPC)
    {
        goto out;
    }

#endif

    if ((flags & TR_SYS_FILE_PREALLOC_SPARSE) == 0)
    {
        int code = errno;

#ifdef HAVE_XFS_XFS_H

        if (platform_test_xfs_fd(handle))
        {
            xfs_flock64_t fl;

            fl.l_whence = 0;
            fl.l_start = 0;
            fl.l_len = size;

            ret = xfsctl(NULL, handle, XFS_IOC_RESVSP64, &fl) != -1;

            if (ret)
            {
                ret = ftruncate(handle, size) != -1;
            }

            code = errno;

            if (ret || code == ENOSPC)
            {
                goto non_sparse_out;
            }
        }

#endif

#ifdef __APPLE__

        {
            fstore_t fst;

            fst.fst_flags = F_ALLOCATEALL;
            fst.fst_posmode = F_PEOFPOSMODE;
            fst.fst_offset = 0;
            fst.fst_length = size;
            fst.fst_bytesalloc = 0;

            ret = fcntl(handle, F_PREALLOCATE, &fst) != -1;

            if (ret)
            {
                ret = ftruncate(handle, size) != -1;
            }

            code = errno;

            if (ret || code == ENOSPC)
            {
                goto non_sparse_out;
            }
        }

#endif

#ifdef HAVE_POSIX_FALLOCATE

        code = posix_fallocate(handle, 0, size);
        ret = code == 0;

#endif

#if defined(HAVE_XFS_XFS_H) || defined(__APPLE__)
non_sparse_out:
#endif
        errno = code;
    }

#ifdef HAVE_FALLOCATE64
out:
#endif

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

void* tr_sys_file_map_for_reading(tr_sys_file_t handle, uint64_t offset, uint64_t size, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(size > 0);

    void* ret = mmap(NULL, size, PROT_READ, MAP_SHARED, handle, offset);

    if (ret == MAP_FAILED)
    {
        set_system_error(error, errno);
        ret = NULL;
    }

    return ret;
}

bool tr_sys_file_unmap(void const* address, uint64_t size, tr_error** error)
{
    TR_ASSERT(address != NULL);
    TR_ASSERT(size > 0);

    bool ret = munmap((void*)address, size) != -1;

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_file_lock(tr_sys_file_t handle, int operation, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT((operation & ~(TR_SYS_FILE_LOCK_SH | TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB | TR_SYS_FILE_LOCK_UN)) == 0);
    TR_ASSERT(!!(operation & TR_SYS_FILE_LOCK_SH) + !!(operation & TR_SYS_FILE_LOCK_EX) +
        !!(operation & TR_SYS_FILE_LOCK_UN) == 1);

    bool ret;

#if defined(F_OFD_SETLK)

    struct flock fl = { 0 };

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
    }
    while (!ret && errno == EINTR);

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
    }
    while (!ret && errno == EINTR);

#else

    (void)handle;
    (void)operation;

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
    char* ret;

    ret = getcwd(NULL, 0);

    if (ret == NULL && (errno == EINVAL || errno == ERANGE))
    {
        size_t size = PATH_MAX;
        char* tmp = NULL;

        do
        {
            tmp = tr_renew(char, tmp, size);

            if (tmp == NULL)
            {
                break;
            }

            ret = getcwd(tmp, size);
            size += 2048;
        }
        while (ret == NULL && errno == ERANGE);

        if (ret == NULL)
        {
            int const err = errno;
            tr_free(tmp);
            errno = err;
        }
    }

    if (ret == NULL)
    {
        set_system_error(error, errno);
    }

    return ret;
}

bool tr_sys_dir_create(char const* path, int flags, int permissions, tr_error** error)
{
    TR_ASSERT(path != NULL);

    bool ret;
    tr_error* my_error = NULL;

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
        if (my_error != NULL)
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
    TR_ASSERT(path_template != NULL);

    bool ret;

#ifdef HAVE_MKDTEMP

    ret = mkdtemp(path_template) != NULL;

#else

    ret = mktemp(path_template) != NULL && mkdir(path_template, 0700) != -1;

#endif

    if (!ret)
    {
        set_system_error(error, errno);
    }

    return ret;
}

tr_sys_dir_t tr_sys_dir_open(char const* path, tr_error** error)
{
    TR_ASSERT(path != NULL);

    DIR* ret = opendir(path);

    if (ret == NULL)
    {
        set_system_error(error, errno);
        return TR_BAD_SYS_DIR;
    }

    return (tr_sys_dir_t)ret;
}

char const* tr_sys_dir_read_name(tr_sys_dir_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_DIR);

    char const* ret = NULL;

    errno = 0;
    struct dirent* entry = readdir((DIR*)handle);

    if (entry != NULL)
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
