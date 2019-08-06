/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

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

    if (fp == NULL)
    {
        return NULL;
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

    struct mntent* mnt;

    fp = setmntent(_PATH_MOUNTED, "r");

    if (fp == NULL)
    {
        return NULL;
    }

    while ((mnt = getmntent(fp)) != NULL)
    {
        if (tr_strcmp0(path, mnt->mnt_dir) == 0)
        {
            break;
        }
    }

    endmntent(fp);
    return mnt != NULL ? mnt->mnt_fsname : NULL;

#endif

#else /* BSD derived systems */

    int n;
    struct statfs* mnt;

    n = getmntinfo(&mnt, MNT_WAIT);

    if (n == 0)
    {
        return NULL;
    }

    for (int i = 0; i < n; i++)
    {
        if (tr_strcmp0(path, mnt[i].f_mntonname) == 0)
        {
            return mnt[i].f_mntfromname;
        }
    }

    return NULL;

#endif
}

static char const* getfstype(char const* device)
{
#ifdef HAVE_GETMNTENT

    FILE* fp;

#ifdef __sun

    struct mnttab mnt;
    fp = fopen(_PATH_MOUNTED, "r");

    if (fp == NULL)
    {
        return NULL;
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

    struct mntent* mnt;

    fp = setmntent(_PATH_MOUNTED, "r");

    if (fp == NULL)
    {
        return NULL;
    }

    while ((mnt = getmntent(fp)) != NULL)
    {
        if (tr_strcmp0(device, mnt->mnt_fsname) == 0)
        {
            break;
        }
    }

    endmntent(fp);
    return mnt != NULL ? mnt->mnt_type : NULL;

#endif

#else /* BSD derived systems */

    int n;
    struct statfs* mnt;

    n = getmntinfo(&mnt, MNT_WAIT);

    if (n == 0)
    {
        return NULL;
    }

    for (int i = 0; i < n; i++)
    {
        if (tr_strcmp0(device, mnt[i].f_mntfromname) == 0)
        {
            return mnt[i].f_fstypename;
        }
    }

    return NULL;

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

        if (device != NULL)
        {
            break;
        }

        c = strrchr(dir, '/');

        if (c != NULL)
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

#include <quota.h>

static int64_t getquota(char const* device)
{
    struct quotahandle* qh;
    struct quotakey qk;
    struct quotaval qv;
    int64_t limit;
    int64_t freespace;
    int64_t spaceused;

    qh = quota_open(device);

    if (qh == NULL)
    {
        return -1;
    }

    qk.qk_idtype = QUOTA_IDTYPE_USER;
    qk.qk_id = getuid();
    qk.qk_objtype = QUOTA_OBJTYPE_BLOCKS;

    if (quota_get(qh, &qk, &qv) == -1)
    {
        quota_close(qh);
        return -1;
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
        return -1;
    }

    spaceused = qv.qv_usage;
    quota_close(qh);

    freespace = limit - spaceused;
    return freespace < 0 ? 0 : freespace;
}

#else

static int64_t getquota(char const* device)
{
#if defined(__DragonFly__)
    struct ufs_dqblk dq;
#else
    struct dqblk dq;
#endif
    int64_t limit;
    int64_t freespace;
    int64_t spaceused;

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    if (quotactl(device, QCMD(Q_GETQUOTA, USRQUOTA), getuid(), (caddr_t)&dq) == 0)
    {
#elif defined(__sun)
    struct quotctl op;
    int fd = open(device, O_RDONLY);

    if (fd < 0)
    {
        return -1;
    }

    op.op = Q_GETQUOTA;
    op.uid = getuid();
    op.addr = (caddr_t)&dq;

    if (ioctl(fd, Q_QUOTACTL, &op) == 0)
    {
        close(fd);
#else
    if (quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device, getuid(), (caddr_t)&dq) == 0)
    {
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
            return -1;
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
        return freespace < 0 ? 0 : freespace;
#else
        return freespace < 0 ? 0 : (freespace * 1024);
#endif
    }

#if defined(__sun)
    close(fd);
#endif

    /* something went wrong */
    return -1;
}

#endif

#ifdef HAVE_XQM

static int64_t getxfsquota(char* device)
{
    int64_t limit;
    int64_t freespace;
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
            return -1;
        }

        freespace = limit - (dq.d_bcount >> 1);
        return freespace < 0 ? 0 : (freespace * 1024);
    }

    /* something went wrong */
    return -1;
}

#endif /* HAVE_XQM */

#endif /* _WIN32 */

static int64_t tr_getQuotaFreeSpace(struct tr_device_info const* info)
{
    int64_t ret = -1;

#ifndef _WIN32

    if (info->fstype != NULL && evutil_ascii_strcasecmp(info->fstype, "xfs") == 0)
    {
#ifdef HAVE_XQM
        ret = getxfsquota(info->device);
#endif
    }
    else
    {
        ret = getquota(info->device);
    }

#else /* _WIN32 */

    (void)info;

#endif /* _WIN32 */

    return ret;
}

static int64_t tr_getDiskFreeSpace(char const* path)
{
#ifdef _WIN32

    int64_t ret = -1;
    wchar_t* wide_path;

    wide_path = tr_win32_utf8_to_native(path, -1);

    if (wide_path != NULL)
    {
        ULARGE_INTEGER freeBytesAvailable;

        if (GetDiskFreeSpaceExW(wide_path, &freeBytesAvailable, NULL, NULL))
        {
            ret = freeBytesAvailable.QuadPart;
        }

        tr_free(wide_path);
    }

    return ret;

#elif defined(HAVE_STATVFS)

    struct statvfs buf;
    return statvfs(path, &buf) ? -1 : (int64_t)buf.f_bavail * (int64_t)buf.f_frsize;

#else

#warning FIXME: not implemented

    return -1;

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
    if (info != NULL)
    {
        tr_free(info->fstype);
        tr_free(info->device);
        tr_free(info->path);
        tr_free(info);
    }
}

int64_t tr_device_info_get_free_space(struct tr_device_info const* info)
{
    int64_t free_space;

    if (info == NULL || info->path == NULL)
    {
        errno = EINVAL;
        free_space = -1;
    }
    else
    {
        free_space = tr_getQuotaFreeSpace(info);

        if (free_space < 0)
        {
            free_space = tr_getDiskFreeSpace(info->path);
        }
    }

    return free_space;
}

/***
****
***/
