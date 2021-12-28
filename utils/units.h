/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cstddef>

static auto constexpr MemK = size_t{ 1024 };
static char constexpr MemKStr[] = "KiB";
static char constexpr MemMStr[] = "MiB";
static char constexpr MemGStr[] = "GiB";
static char constexpr MemTStr[] = "TiB";

static auto constexpr DiskK = size_t{ 1000 };
static char constexpr DiskKStr[] = "kB";
static char constexpr DiskMStr[] = "MB";
static char constexpr DiskGStr[] = "GB";
static char constexpr DiskTStr[] = "TB";

static auto constexpr SpeedK = size_t{ 1000 };
static char constexpr SpeedKStr[] = "kB/s";
static char constexpr SpeedMStr[] = "MB/s";
static char constexpr SpeedGStr[] = "GB/s";
static char constexpr SpeedTStr[] = "TB/s";
