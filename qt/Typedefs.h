#pragma once

#include <unordered_set>

#include <libtransmission/transmission.h>

using torrent_ids_t = std::unordered_set<tr_torrent_id_t>;
