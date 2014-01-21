/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_PLATFORM_QUOTA_H
#define TR_PLATFORM_QUOTA_H

/**
 * @addtogroup tr_session Session
 * @{
 */

struct tr_device_info
{
  char * path;
  char * device;
  char * fstype;
};

struct tr_device_info * tr_device_info_create (const char * path);

/** If the disk quota is enabled and readable, this returns how much is available in the quota.
    Otherwise, it returns how much is available on the disk, or -1 on error. */
int64_t tr_device_info_get_free_space (const struct tr_device_info * info);

void tr_device_info_free (struct tr_device_info * info);

/** @} */

#endif
