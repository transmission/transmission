/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

/**
 * @addtogroup tr_session Session
 * @{
 */

struct tr_device_info
{
    char* path;
    char* device;
    char* fstype;
};

struct tr_disk_space
{
   int64_t  free;
   int64_t  total;
};

struct tr_device_info* tr_device_info_create(char const* path);

/** Values represents the total space on disk.
    If the disk quota (free space) is enabled and readable, this returns how much is available in the quota.
    Otherwise, it returns how much is available on the disk, or { -1, -1 } on error. */
struct tr_disk_space tr_device_info_get_disk_space(struct tr_device_info const* info);

void tr_device_info_free(struct tr_device_info* info);

/** @} */
