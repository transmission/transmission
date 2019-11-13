#pragma once

#include <unordered_set>
using torrent_ids_t = std::unordered_set<int>;

/***
****  UNITS
***/

#define DISABLE_PREDEFINED_UNITS
#define ENABLE_PREDEFINED_DATA_TRANSFER_RATE_UNITS
#define ENABLE_PREDEFINED_DATA_UNITS
#define ENABLE_PREDEFINED_TIME_UNITS
#define UNIT_LIB_DEFAULT_TYPE int64_t
#define UNIT_LIB_DISABLE_IOSTREAM
#include <units.h>
#undef DISABLE_PREDEFINED_UNITS
#undef ENABLE_PREDEFINED_DATA_TRANSFER_RATE_UNITS
#undef ENABLE_PREDEFINED_DATA_UNITS
#undef ENABLE_PREDEFINED_TIME_UNITS
#undef UNIT_LIB_DEFAULT_TYPE
#undef UNIT_LIB_DISABLE_IOSTREAM

using Bps_t = units::data_transfer_rate::bytes_per_second_t;
using bytes_t = units::data::byte_t;
using KBps_t = units::data_transfer_rate::kilobytes_per_second_t;
using minutes_t = units::time::minute_t;
using seconds_t = units::time::second_t;
using namespace units::literals;
