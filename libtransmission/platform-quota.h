// This file Copyright © 2013-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int64_t
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
