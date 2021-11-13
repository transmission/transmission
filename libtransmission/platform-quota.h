/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/**
 * @addtogroup tr_session Session
 * @{
 */

struct tr_device_info
{
    std::string path;
    std::string device;
    std::string fstype;
};

struct tr_disk_space
{
    int64_t free;
    int64_t total;
};

tr_device_info tr_device_info_create(std::string_view path);

/** Values represents the total space on disk.
    If the disk quota (free space) is enabled and readable, this returns how much is available in the quota.
    Otherwise, it returns how much is available on the disk, or { -1, -1 } on error. */
tr_disk_space tr_device_info_get_disk_space(tr_device_info const& info);

/** @} */
