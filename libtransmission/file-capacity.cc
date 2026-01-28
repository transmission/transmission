// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <cstdint> // int64_t
#include <cstdio> // FILE
#include <optional>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <unistd.h> /* getuid() */
#include <sys/types.h> /* types needed by quota.h */
#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <ufs/ufs/quota.h> /* quotactl() */
#elif defined(__DragonFly__)
#include <vfs/ufs/quota.h> /* quotactl */
#elif defined(__NetBSD__)
#include <sys/param.h>
#ifndef statfs
#define statfs statvfs
#endif
#elif defined(__sun)
#include <sys/fs/ufs_quota.h> /* quotactl */
#else
#include <sys/quota.h> /* quotactl() */
#endif
#if !defined(btodb) && defined(QIF_DQBLKSIZE_BITS)
#define btodb(num) ((num) >> QIF_DQBLKSIZE_BITS)
#endif
#ifdef HAVE_GETMNTENT
#ifdef __sun
#include <fcntl.h>
#include <stdio.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#define _PATH_MOUNTED MNTTAB
#else
#include <mntent.h>
#include <paths.h> /* _PATH_MOUNTED */
#endif
#else /* BSD derived systems */
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#endif
#endif

#ifdef __APPLE__
#ifndef HAVE_SYS_STATVFS_H
#define HAVE_SYS_STATVFS_H
#endif
#ifndef HAVE_STATVFS
#define HAVE_STATVFS
#endif
#endif

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

#ifdef HAVE_XFS_XFS_H
#define HAVE_XQM
#include <xfs/xqm.h>
#endif

#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/tr-macros.h"
#include "libtransmission/utils.h" // tr_win32_utf8_to_native

namespace
{
struct tr_device_info
{
    std::string path;
    std::string device;
    std::string fstype;
};

#ifndef _WIN32

[[nodiscard]] char const* getdev(std::string_view path)
{
#ifdef HAVE_GETMNTENT

#ifdef __sun

    FILE* const fp = fopen(_PATH_MOUNTED, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    struct mnttab mnt;
    while (getmntent(fp, &mnt) != -1)
    {
        if (mnt.mnt_mountp != nullptr && path == mnt.mnt_mountp)
        {
            break;
        }
    }

    fclose(fp);
    return mnt.mnt_special;

#else

    FILE* const fp = setmntent(_PATH_MOUNTED, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    struct mntent const* mnt = nullptr;
    while ((mnt = getmntent(fp)) != nullptr)
    {
        if (mnt->mnt_dir != nullptr && path == mnt->mnt_dir)
        {
            break;
        }
    }

    endmntent(fp);
    return mnt != nullptr ? mnt->mnt_fsname : nullptr;

#endif

#else /* BSD derived systems */

    struct statfs* mnt = nullptr;
    int const n = getmntinfo(&mnt, MNT_WAIT);
    if (n == 0)
    {
        return nullptr;
    }

    for (int i = 0; i < n; i++)
    {
        if (path == mnt[i].f_mntonname)
        {
            return mnt[i].f_mntfromname;
        }
    }

    return nullptr;

#endif
}

[[nodiscard]] char const* getfstype(std::string_view device)
{
#ifdef HAVE_GETMNTENT

#ifdef __sun

    FILE* const fp = fopen(_PATH_MOUNTED, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    struct mnttab mnt;
    while (getmntent(fp, &mnt) != -1)
    {
        if (device == mnt.mnt_mountp)
        {
            break;
        }
    }

    fclose(fp);
    return mnt.mnt_fstype;

#else

    FILE* const fp = setmntent(_PATH_MOUNTED, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    struct mntent const* mnt = nullptr;
    while ((mnt = getmntent(fp)) != nullptr)
    {
        if (device == mnt->mnt_fsname)
        {
            break;
        }
    }

    endmntent(fp);
    return mnt != nullptr ? mnt->mnt_type : nullptr;

#endif

#else /* BSD derived systems */

    struct statfs* mnt = nullptr;
    int const n = getmntinfo(&mnt, MNT_WAIT);
    if (n == 0)
    {
        return nullptr;
    }

    for (int i = 0; i < n; i++)
    {
        if (device == mnt[i].f_mntfromname)
        {
            return mnt[i].f_fstypename;
        }
    }

    return nullptr;

#endif
}

std::string getblkdev(std::string_view path)
{
    for (;;)
    {
        if (auto const* const device = getdev(path); device != nullptr)
        {
            return device;
        }

        auto const pos = path.rfind('/');
        if (pos == std::string_view::npos)
        {
            return {};
        }

        path = path.substr(0, pos);
    }
}

#if defined(__NetBSD__) && __NetBSD_Version__ >= 600000000

extern "C"
{
#include <quota.h>
}

[[nodiscard]] tr_sys_path_capacity getquota(char const* device)
{
    struct quotahandle* qh;
    struct quotakey qk;
    struct quotaval qv;
    struct tr_sys_path_capacity disk_space = { -1, -1 };

    qh = quota_open(device);

    if (qh == nullptr)
    {
        return disk_space;
    }

    qk.qk_idtype = QUOTA_IDTYPE_USER;
    qk.qk_id = getuid();
    qk.qk_objtype = QUOTA_OBJTYPE_BLOCKS;

    if (quota_get(qh, &qk, &qv) == -1)
    {
        quota_close(qh);
        return disk_space;
    }

    int64_t limit;
    if (qv.qv_softlimit > 0)
    {
        limit = qv.qv_softlimit;
    }
    else if (qv.qv_hardlimit > 0)
    {
        limit = qv.qv_hardlimit;
    }
    else
    {
        quota_close(qh);
        return disk_space;
    }

    int64_t const spaceused = qv.qv_usage;
    quota_close(qh);

    int64_t const freespace = limit - spaceused;
    disk_space.free = freespace < 0 ? 0 : freespace;
    disk_space.total = limit;
    return disk_space;
}

#else

[[nodiscard]] std::optional<tr_sys_path_capacity> getquota(char const* device)
{
#if defined(__DragonFly__)
    struct ufs_dqblk dq = {};
#else
    struct dqblk dq = {};
#endif

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    if (quotactl(device, QCMD(Q_GETQUOTA, USRQUOTA), getuid(), reinterpret_cast<caddr_t>(&dq)) != 0)
    {
        return {};
    }
#elif defined(__sun)
    int fd = open(device, O_RDONLY);
    if (fd < 0)
    {
        return {};
    }

    struct quotctl op;
    op.op = Q_GETQUOTA;
    op.uid = getuid();
    op.addr = reinterpret_cast<caddr_t>(&dq);

    if (ioctl(fd, Q_QUOTACTL, &op) != 0)
    {
        close(fd);
        return {};
    }

    close(fd);
#else
    if (quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device, getuid(), reinterpret_cast<caddr_t>(&dq)) != 0)
    {
        return {};
    }
#endif

    auto limit = uint64_t{};
    if (dq.dqb_bsoftlimit > 0U)
    {
        /* Use soft limit first */
        limit = dq.dqb_bsoftlimit;
    }
    else if (dq.dqb_bhardlimit > 0U)
    {
        limit = dq.dqb_bhardlimit;
    }
    else
    {
        // No quota enabled for this user
        return {};
    }

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    uint64_t const spaceused = dq.dqb_curblocks >> 1U;
#elif defined(__APPLE__)
    uint64_t const spaceused = dq.dqb_curbytes;
#elif defined(__UCLIBC__) && !TR_UCLIBC_CHECK_VERSION(1, 0, 18)
    uint64_t const spaceused = btodb(dq.dqb_curblocks);
#elif defined(__sun) || (defined(_LINUX_QUOTA_VERSION) && _LINUX_QUOTA_VERSION < 2)
    uint64_t const spaceused = dq.dqb_curblocks >> 1U;
#else
    uint64_t const spaceused = btodb(dq.dqb_curspace);
#endif

    auto const freespace = limit > spaceused ? limit - spaceused : 0U;

#ifdef __APPLE__
    return tr_sys_path_capacity{ .free = freespace, .total = limit };
#else
    return tr_sys_path_capacity{ .free = freespace << 10U, .total = limit << 10U };
#endif
}

#endif

#ifdef HAVE_XQM

[[nodiscard]] std::optional<tr_sys_path_capacity> getxfsquota(char const* device)
{
    if (struct fs_disk_quota dq; quotactl(QCMD(Q_XGETQUOTA, USRQUOTA), device, getuid(), reinterpret_cast<caddr_t>(&dq)) != 0)
    {
        // something went wrong
        return {};
    }

    auto limit = uint64_t{};
    if (dq.d_blk_softlimit > 0U)
    {
        /* Use soft limit first */
        limit = dq.d_blk_softlimit >> 1U;
    }
    else if (dq.d_blk_hardlimit > 0U)
    {
        limit = dq.d_blk_hardlimit >> 1U;
    }
    else
    {
        // No quota enabled for this user
        return {};
    }

    auto const spaceused = dq.d_bcount >> 1U;
    auto const freespace = limit > spaceused ? limit - spaceused : 0U;
    return tr_sys_path_capacity{ .free = freespace << 10U, .total = limit << 10U };
}

#endif /* HAVE_XQM */

#endif /* _WIN32 */

[[nodiscard]] std::optional<tr_sys_path_capacity> get_quota_space([[maybe_unused]] tr_device_info const& info)
{
#ifndef _WIN32

    if (tr_strlower(info.fstype) == "xfs")
    {
#ifdef HAVE_XQM
        return getxfsquota(info.device.c_str());
#endif
    }
    else
    {
        return getquota(info.device.c_str());
    }

#endif /* _WIN32 */

    return {};
}

[[nodiscard]] std::optional<tr_sys_path_capacity> getDiskSpace(char const* path)
{
#ifdef _WIN32

    if (auto const wide_path = tr_win32_utf8_to_native(path); !std::empty(wide_path))
    {
        ULARGE_INTEGER free_bytes_available;
        ULARGE_INTEGER total_bytes_available;

        if (GetDiskFreeSpaceExW(wide_path.c_str(), &free_bytes_available, &total_bytes_available, nullptr) != 0)
        {
            ret.free = free_bytes_available.QuadPart;
            ret.total = total_bytes_available.QuadPart;
            return tr_sys_path_capacity{ .free = free_bytes_available.QuadPart, .total = total_bytes_available.QuadPart };
        }
    }

#elif defined(HAVE_STATVFS)

    if (struct statvfs buf = {}; statvfs(path, &buf) == 0)
    {
        return tr_sys_path_capacity{ .free = buf.f_bavail * buf.f_frsize, .total = buf.f_blocks * buf.f_frsize };
    }

#else

#warning FIXME: not implemented

#endif

    return {};
}

tr_device_info tr_device_info_create(std::string_view path)
{
    auto out = tr_device_info{};
    out.path = path;
#ifndef _WIN32
    out.device = getblkdev(out.path);
    auto const* const fstype = getfstype(out.path);
    out.fstype = fstype != nullptr ? fstype : "";
#endif
    return out;
}

std::optional<tr_sys_path_capacity> tr_device_info_get_disk_space(struct tr_device_info const& info)
{
    if (std::empty(info.path))
    {
        errno = EINVAL;
        return {};
    }

    if (auto space = get_quota_space(info); space.has_value())
    {
        return space;
    }

    return getDiskSpace(info.path.c_str());
}

} // namespace

std::optional<tr_sys_path_capacity> tr_sys_path_get_capacity(std::string_view path, tr_error* error)
{
    auto local_error = tr_error{};
    if (error == nullptr)
    {
        error = &local_error;
    }

    auto const info = tr_sys_path_get_info(path, 0, &local_error);
    if (!info)
    {
        return {};
    }

    if (!info->isFolder())
    {
        error->set_from_errno(ENOTDIR);
        return {};
    }

    auto const device = tr_device_info_create(path);
    if (auto capacity = tr_device_info_get_disk_space(device); capacity.has_value())
    {
        return capacity;
    }

    error->set_from_errno(EINVAL);
    return {};
}
