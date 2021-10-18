/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <event2/util.h> /* evutil_ascii_strcasecmp() */

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

#include "transmission.h"
#include "tr-macros.h"
#include "utils.h"
#include "platform-quota.h"

/***
****
***/

#ifndef _WIN32

static char const* getdev(char const* path)
{
#ifdef HAVE_GETMNTENT

    FILE* fp;

#ifdef __sun

    struct mnttab mnt;
    fp = fopen(_PATH_MOUNTED, "r");

    if (fp == nullptr)
    {
        return nullptr;
    }

    while (getmntent(fp, &mnt) != -1)
    {
        if (tr_strcmp0(path, mnt.mnt_mountp) == 0)
        {
            break;
        }
    }

    fclose(fp);
    return mnt.mnt_special;

#else

    struct mntent const* mnt;

    fp = setmntent(_PATH_MOUNTED, "r");

    if (fp == nullptr)
    {
        return nullptr;
    }

    while ((mnt = getmntent(fp)) != nullptr)
    {
        if (tr_strcmp0(path, mnt->mnt_dir) == 0)
        {
            break;
        }
    }

    endmntent(fp);
    return mnt != nullptr ? mnt->mnt_fsname : nullptr;

#endif

#else /* BSD derived systems */

    int n;
    struct statfs* mnt;

    n = getmntinfo(&mnt, MNT_WAIT);

    if (n == 0)
    {
        return nullptr;
    }

    for (int i = 0; i < n; i++)
    {
        if (tr_strcmp0(path, mnt[i].f_mntonname) == 0)
        {
            return mnt[i].f_mntfromname;
        }
    }

    return nullptr;

#endif
}

static char const* getfstype(char const* device)
{
#ifdef HAVE_GETMNTENT

    FILE* fp;

#ifdef __sun

    struct mnttab mnt;
    fp = fopen(_PATH_MOUNTED, "r");

    if (fp == nullptr)
    {
        return nullptr;
    }

    while (getmntent(fp, &mnt) != -1)
    {
        if (tr_strcmp0(device, mnt.mnt_mountp) == 0)
        {
            break;
        }
    }

    fclose(fp);
    return mnt.mnt_fstype;

#else

    struct mntent const* mnt;

    fp = setmntent(_PATH_MOUNTED, "r");

    if (fp == nullptr)
    {
        return nullptr;
    }

    while ((mnt = getmntent(fp)) != nullptr)
    {
        if (tr_strcmp0(device, mnt->mnt_fsname) == 0)
        {
            break;
        }
    }

    endmntent(fp);
    return mnt != nullptr ? mnt->mnt_type : nullptr;

#endif

#else /* BSD derived systems */

    int n;
    struct statfs* mnt;

    n = getmntinfo(&mnt, MNT_WAIT);

    if (n == 0)
    {
        return nullptr;
    }

    for (int i = 0; i < n; i++)
    {
        if (tr_strcmp0(device, mnt[i].f_mntfromname) == 0)
        {
            return mnt[i].f_fstypename;
        }
    }

    return nullptr;

#endif
}

static char const* getblkdev(char const* path)
{
    char* c;
    char* dir;
    char const* device;

    dir = tr_strdup(path);

    for (;;)
    {
        device = getdev(dir);

        if (device != nullptr)
        {
            break;
        }

        c = strrchr(dir, '/');

        if (c != nullptr)
        {
            *c = '\0';
        }
        else
        {
            break;
        }
    }

    tr_free(dir);
    return device;
}

#if defined(__NetBSD__) && __NetBSD_Version__ >= 600000000

extern "C"
{
#include <quota.h>
}

struct tr_disk_space getquota(char const* device)
{
    struct quotahandle* qh;
    struct quotakey qk;
    struct quotaval qv;
    struct tr_disk_space disk_space = { -1, -1 };
    int64_t limit;
    int64_t freespace;
    int64_t spaceused;

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

    spaceused = qv.qv_usage;
    quota_close(qh);

    freespace = limit - spaceused;
    disk_space.free = freespace < 0 ? 0 : freespace;
    disk_space.total = limit;
    return disk_space;
}

#else

static struct tr_disk_space getquota(char const* device)
{
#if defined(__DragonFly__)
    struct ufs_dqblk dq;
#else
    struct dqblk dq;
#endif
    int64_t limit;
    int64_t freespace;
    struct tr_disk_space disk_space = { -1, -1 };
    int64_t spaceused;

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    if (quotactl(device, QCMD(Q_GETQUOTA, USRQUOTA), getuid(), (caddr_t)&dq) != 0)
    {
        return disk_space;
    }
#elif defined(__sun)
    struct quotctl op;
    int fd = open(device, O_RDONLY);

    if (fd < 0)
    {
        return disk_space;
    }

    op.op = Q_GETQUOTA;
    op.uid = getuid();
    op.addr = (caddr_t)&dq;

    if (ioctl(fd, Q_QUOTACTL, &op) != 0)
    {
        close(fd);
        return disk_space;
    }

    close(fd);
#else
    if (quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device, getuid(), (caddr_t)&dq) != 0)
    {
        return disk_space;
    }
#endif

    if (dq.dqb_bsoftlimit > 0)
    {
        /* Use soft limit first */
        limit = dq.dqb_bsoftlimit;
    }
    else if (dq.dqb_bhardlimit > 0)
    {
        limit = dq.dqb_bhardlimit;
    }
    else
    {
        /* No quota enabled for this user */
        return disk_space;
    }

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    spaceused = (int64_t)dq.dqb_curblocks >> 1;
#elif defined(__APPLE__)
    spaceused = (int64_t)dq.dqb_curbytes;
#elif defined(__UCLIBC__) && !TR_UCLIBC_CHECK_VERSION(1, 0, 18)
    spaceused = (int64_t)btodb(dq.dqb_curblocks);
#elif defined(__sun) || (defined(_LINUX_QUOTA_VERSION) && _LINUX_QUOTA_VERSION < 2)
    spaceused = (int64_t)dq.dqb_curblocks >> 1;
#else
    spaceused = btodb(dq.dqb_curspace);
#endif

    freespace = limit - spaceused;

#ifdef __APPLE__
    disk_space.free = freespace < 0 ? 0 : freespace;
    disk_space.total = limit < 0 ? 0 : limit;
    return disk_space;
#else
    disk_space.free = freespace < 0 ? 0 : (freespace * 1024);
    disk_space.total = limit < 0 ? 0 : (limit * 1024);
    return disk_space;
#endif
}

#endif

#ifdef HAVE_XQM

static struct tr_disk_space getxfsquota(char* device)
{
    int64_t limit;
    int64_t freespace;
    struct tr_disk_space disk_space = { -1, -1 };
    struct fs_disk_quota dq;

    if (quotactl(QCMD(Q_XGETQUOTA, USRQUOTA), device, getuid(), (caddr_t)&dq) == 0)
    {
        if (dq.d_blk_softlimit > 0)
        {
            /* Use soft limit first */
            limit = dq.d_blk_softlimit >> 1;
        }
        else if (dq.d_blk_hardlimit > 0)
        {
            limit = dq.d_blk_hardlimit >> 1;
        }
        else
        {
            /* No quota enabled for this user */
            return disk_space;
        }

        freespace = limit - (dq.d_bcount >> 1);
        freespace = freespace < 0 ? 0 : (freespace * 1024);
        limit = limit * 1024;
        disk_space.free = freespace;
        disk_space.total = limit;
        return disk_space;
    }

    /* something went wrong */
    return disk_space;
}

#endif /* HAVE_XQM */

#endif /* _WIN32 */

static struct tr_disk_space tr_getQuotaSpace([[maybe_unused]] struct tr_device_info const* info)
{
    struct tr_disk_space ret = { -1, -1 };

#ifndef _WIN32

    if (info->fstype != nullptr && evutil_ascii_strcasecmp(info->fstype, "xfs") == 0)
    {
#ifdef HAVE_XQM
        ret = getxfsquota(info->device);
#endif
    }
    else
    {
        ret = getquota(info->device);
    }

#endif /* _WIN32 */

    return ret;
}

static struct tr_disk_space tr_getDiskSpace(char const* path)
{
#ifdef _WIN32

    struct tr_disk_space ret = { -1, -1 };
    wchar_t* wide_path;

    wide_path = tr_win32_utf8_to_native(path, -1);

    if (wide_path != nullptr)
    {
        ULARGE_INTEGER freeBytesAvailable;
        ULARGE_INTEGER totalBytesAvailable;

        if (GetDiskFreeSpaceExW(wide_path, &freeBytesAvailable, &totalBytesAvailable, nullptr))
        {
            ret.free = freeBytesAvailable.QuadPart;
            ret.total = totalBytesAvailable.QuadPart;
        }

        tr_free(wide_path);
    }

    return ret;

#elif defined(HAVE_STATVFS)

    struct statvfs buf;
    return statvfs(path, &buf) ?
        (struct tr_disk_space){ -1, -1 } :
        (struct tr_disk_space){ (int64_t)buf.f_bavail * (int64_t)buf.f_frsize, (int64_t)buf.f_blocks * (int64_t)buf.f_frsize };

#else

#warning FIXME: not implemented

    return { -1, -1 };

#endif
}

struct tr_device_info* tr_device_info_create(char const* path)
{
    struct tr_device_info* info;

    info = tr_new0(struct tr_device_info, 1);
    info->path = tr_strdup(path);

#ifndef _WIN32
    info->device = tr_strdup(getblkdev(path));
    info->fstype = tr_strdup(getfstype(path));
#endif

    return info;
}

void tr_device_info_free(struct tr_device_info* info)
{
    if (info != nullptr)
    {
        tr_free(info->fstype);
        tr_free(info->device);
        tr_free(info->path);
        tr_free(info);
    }
}

struct tr_disk_space tr_device_info_get_disk_space(struct tr_device_info const* info)
{
    struct tr_disk_space space;

    if (info == nullptr || info->path == nullptr)
    {
        errno = EINVAL;
        space.free = -1;
        space.total = -1;
    }
    else
    {
        space = tr_getQuotaSpace(info);

        if (space.free < 0 || space.total < 0)
        {
            space = tr_getDiskSpace(info->path);
        }
    }

    return space;
}

/***
****
***/
