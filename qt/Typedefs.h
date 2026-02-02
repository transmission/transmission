#pragma once

#include <set>
#include <unordered_set>

#include <libtransmission/transmission.h>

using torrent_ids_t = std::unordered_set<tr_torrent_id_t>;

using file_indices_t = std::set<int>;
