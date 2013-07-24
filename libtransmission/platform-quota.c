/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* getuid() */
#include <event2/util.h> /* evutil_ascii_strcasecmp () */

#ifndef WIN32
 #include <sys/types.h> /* types needed by quota.h */
 #if defined(__FreeBSD__) || defined(__OpenBSD__)
  #include <ufs/ufs/quota.h> /* quotactl() */
 #elif defined (__sun)
  #include <sys/fs/ufs_quota.h> /* quotactl */
 #else
  #include <sys/quota.h> /* quotactl() */
 #endif
 #ifdef HAVE_GETMNTENT
  #ifdef __sun
   #include <sys/types.h>
   #include <sys/stat.h>
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

#ifdef SYS_DARWIN
 #define HAVE_SYS_STATVFS_H
 #define HAVE_STATVFS
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

#ifndef WIN32
static const char *
getdev (const char * path)
{
#ifdef HAVE_GETMNTENT

  FILE * fp;

#ifdef __sun
  struct mnttab mnt;
  fp = fopen(_PATH_MOUNTED, "r");
  if (fp == NULL)
    return NULL;

  while (getmntent(fp, &mnt))
    if (!tr_strcmp0 (path, mnt.mnt_mountp))
      break;
  fclose(fp);
  return mnt.mnt_special;
#else
  struct mntent * mnt;

  fp = setmntent(_PATH_MOUNTED, "r");
  if (fp == NULL)
    return NULL;

  while ((mnt = getmntent(fp)) != NULL)
    if (!tr_strcmp0 (path, mnt->mnt_dir))
      break;

  endmntent(fp);
  return mnt ? mnt->mnt_fsname : NULL;
#endif
#else /* BSD derived systems */

  int i;
  int n;
  struct statfs * mnt;

  n = getmntinfo(&mnt, MNT_WAIT);
  if (!n)
    return NULL;

  for (i=0; i<n; i++)
    if (!tr_strcmp0 (path, mnt[i].f_mntonname))
      break;

  return (i < n) ? mnt[i].f_mntfromname : NULL;

#endif
}

static const char *
getfstype (const char * device)
{

#ifdef HAVE_GETMNTENT

  FILE * fp;
#ifdef __sun
  struct mnttab mnt;
  fp = fopen(_PATH_MOUNTED, "r");
  if (fp == NULL)
    return NULL;
  while (getmntent(fp, &mnt))
    if (!tr_strcmp0 (device, mnt.mnt_mountp))
      break;
  fclose(fp);
  return mnt.mnt_fstype;
#else
  struct mntent *mnt;

  fp = setmntent (_PATH_MOUNTED, "r");
  if (fp == NULL)
    return NULL;

  while ((mnt = getmntent (fp)) != NULL)
    if (!tr_strcmp0 (device, mnt->mnt_fsname))
      break;

  endmntent(fp);
  return mnt ? mnt->mnt_type : NULL;
#endif
#else /* BSD derived systems */

  int i;
  int n;
  struct statfs *mnt;

  n = getmntinfo(&mnt, MNT_WAIT);
  if (!n)
    return NULL;

  for (i=0; i<n; i++)
    if (!tr_strcmp0 (device, mnt[i].f_mntfromname))
      break;

  return (i < n) ? mnt[i].f_fstypename : NULL;

#endif
}

static const char *
getblkdev (const char * path)
{
  char * c;
  char * dir;
  const char * device;

  dir = tr_strdup(path);

  for (;;)
    {
      device = getdev (dir);
      if (device != NULL)
        break;

      c = strrchr (dir, '/');
      if (c != NULL)
        *c = '\0';
      else
         break;
    }

  tr_free (dir);
  return device;
}

static int64_t
getquota (const char * device)
{
  struct dqblk dq;
  int64_t limit;
  int64_t freespace;
  int64_t spaceused;

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(SYS_DARWIN)
  if (quotactl(device, QCMD(Q_GETQUOTA, USRQUOTA), getuid(), (caddr_t) &dq) == 0)
    {
#elif defined(__sun)
  struct quotctl  op; 
  int fd = open(device, O_RDONLY); 
  if (fd < 0) 
    return -1; 
  op.op = Q_GETQUOTA; 
  op.uid = getuid(); 
  op.addr = (caddr_t) &dq; 
  if (ioctl(fd, Q_QUOTACTL, &op) == 0)
    {
      close(fd);
#else
  if (quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device, getuid(), (caddr_t) &dq) == 0)
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
#if defined(__FreeBSD__) || defined(__OpenBSD__)
      spaceused = (int64_t) dq.dqb_curblocks >> 1;
#elif defined(SYS_DARWIN)
      spaceused = (int64_t) dq.dqb_curbytes;
#elif defined(__UCLIBC__)
      spaceused = (int64_t) btodb(dq.dqb_curblocks);
#elif defined(__sun) || (_LINUX_QUOTA_VERSION < 2)
      spaceused = (int64_t) dq.dqb_curblocks >> 1;
#else
      spaceused = btodb(dq.dqb_curspace);
#endif
      freespace = limit - spaceused;
#ifdef SYS_DARWIN
      return (freespace < 0) ? 0 : freespace;
#else
      return (freespace < 0) ? 0 : freespace * 1024;
#endif
    }
#if defined(__sun)
  close(fd);
#endif
  /* something went wrong */
  return -1;
}

#ifdef HAVE_XQM
static int64_t
getxfsquota (char * device)
{
  int64_t limit;
  int64_t freespace;
  struct fs_disk_quota dq;

  if (quotactl(QCMD(Q_XGETQUOTA, USRQUOTA), device, getuid(), (caddr_t) &dq) == 0)
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
      return (freespace < 0) ? 0 : freespace * 1024;
    }

  /* something went wrong */
  return -1;
}
#endif /* HAVE_XQM */
#endif /* WIN32 */

static int64_t
tr_getQuotaFreeSpace (const struct tr_device_info * info)
{
  int64_t ret = -1;

#ifndef WIN32

  if (info->fstype && !evutil_ascii_strcasecmp(info->fstype, "xfs"))
    {
#ifdef HAVE_XQM
      ret = getxfsquota (info->device);
#endif
    }
  else
    {
      ret = getquota (info->device);
    }
#endif /* WIN32 */

  return ret;
}

static int64_t
tr_getDiskFreeSpace (const char * path)
{
#ifdef WIN32

  uint64_t freeBytesAvailable = 0;
  return GetDiskFreeSpaceEx (path, &freeBytesAvailable, NULL, NULL)
    ? (int64_t)freeBytesAvailable
    : -1;

#elif defined(HAVE_STATVFS)

  struct statvfs buf;
  return statvfs(path, &buf) ? -1 : (int64_t)buf.f_bavail * (int64_t)buf.f_frsize;

#else

  #warning FIXME: not implemented
  return -1;

#endif
}

struct tr_device_info *
tr_device_info_create (const char * path)
{
  struct tr_device_info * info;

  info = tr_new0 (struct tr_device_info, 1);
  info->path = tr_strdup (path);
#ifndef WIN32
  info->device = tr_strdup (getblkdev (path));
  info->fstype = tr_strdup (getfstype (path));
#endif

  return info;
}

void
tr_device_info_free (struct tr_device_info * info)
{
  if (info != NULL)
    {
      tr_free (info->fstype);
      tr_free (info->device);
      tr_free (info->path);
      tr_free (info);
    }
}

int64_t
tr_device_info_get_free_space (const struct tr_device_info * info)
{
  int64_t free_space;

  if ((info == NULL) || (info->path == NULL))
    {
      errno = EINVAL;
      free_space = -1;
    }
  else
    {
      free_space = tr_getQuotaFreeSpace (info);

      if (free_space < 0)
        free_space = tr_getDiskFreeSpace (info->path);
    }

  return free_space;
}

/***
****
***/
