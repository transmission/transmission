/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "rpcimpl.h"
#include "utils.h"
#include "variant.h"

#include "test-fixtures.h"

#include <algorithm>
#include <array>
#include <set>
#include <vector>

namespace libtransmission
{

namespace test
{

using RpcTest = SessionTest;

TEST_F(RpcTest, list)
{
    size_t len;
    int64_t i;
    char const* str;
    tr_variant top;

    tr_rpc_parse_list_str(&top, "12", TR_BAD_SIZE);
    EXPECT_TRUE(tr_variantIsInt(&top));
    EXPECT_TRUE(tr_variantGetInt(&top, &i));
    EXPECT_EQ(12, i);
    tr_variantFree(&top);

    tr_rpc_parse_list_str(&top, "12", 1);
    EXPECT_TRUE(tr_variantIsInt(&top));
    EXPECT_TRUE(tr_variantGetInt(&top, &i));
    EXPECT_EQ(1, i);
    tr_variantFree(&top);

    tr_rpc_parse_list_str(&top, "6,7", TR_BAD_SIZE);
    EXPECT_TRUE(tr_variantIsList(&top));
    EXPECT_EQ(2, tr_variantListSize(&top));
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&top, 0), &i));
    EXPECT_EQ(6, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&top, 1), &i));
    EXPECT_EQ(7, i);
    tr_variantFree(&top);

    tr_rpc_parse_list_str(&top, "asdf", TR_BAD_SIZE);
    EXPECT_TRUE(tr_variantIsString(&top));
    EXPECT_TRUE(tr_variantGetStr(&top, &str, &len));
    EXPECT_EQ(4, len);
    EXPECT_STREQ("asdf", str);
    tr_variantFree(&top);

    tr_rpc_parse_list_str(&top, "1,3-5", TR_BAD_SIZE);
    EXPECT_TRUE(tr_variantIsList(&top));
    EXPECT_EQ(4, tr_variantListSize(&top));
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&top, 0), &i));
    EXPECT_EQ(1, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&top, 1), &i));
    EXPECT_EQ(3, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&top, 2), &i));
    EXPECT_EQ(4, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&top, 3), &i));
    EXPECT_EQ(5, i);
    tr_variantFree(&top);
}

/***
****
***/

TEST_F(RpcTest, sessionGet)
{
    auto const rpc_response_func = [] (tr_session* /*session*/, tr_variant* response, void* setme) noexcept
    {
        *static_cast<tr_variant*>(setme) = *response;
        tr_variantInitBool(response, false);
    };

    auto* tor = zeroTorrentInit();
    EXPECT_NE(nullptr, tor);

    tr_variant request;
    tr_variantInitDict(&request, 1);
    tr_variantDictAddStr(&request, TR_KEY_method, "session-get");
    tr_variant response;
    tr_rpc_request_exec_json(session_, &request, rpc_response_func, &response);
    tr_variantFree(&request);

    EXPECT_TRUE(tr_variantIsDict(&response));
    tr_variant* args;
    EXPECT_TRUE(tr_variantDictFindDict(&response, TR_KEY_arguments, &args));

    // what we expected
    auto const expected_keys = std::array<tr_quark, 52>{
        TR_KEY_alt_speed_down,
        TR_KEY_alt_speed_enabled,
        TR_KEY_alt_speed_time_begin,
        TR_KEY_alt_speed_time_day,
        TR_KEY_alt_speed_time_enabled,
        TR_KEY_alt_speed_time_end,
        TR_KEY_alt_speed_up,
        TR_KEY_anti_brute_force_enabled,
        TR_KEY_anti_brute_force_threshold,
        TR_KEY_blocklist_enabled,
        TR_KEY_blocklist_size,
        TR_KEY_blocklist_url,
        TR_KEY_cache_size_mb,
        TR_KEY_config_dir,
        TR_KEY_dht_enabled,
        TR_KEY_download_dir,
        TR_KEY_download_dir_free_space,
        TR_KEY_download_queue_enabled,
        TR_KEY_download_queue_size,
        TR_KEY_encryption,
        TR_KEY_idle_seeding_limit,
        TR_KEY_idle_seeding_limit_enabled,
        TR_KEY_incomplete_dir,
        TR_KEY_incomplete_dir_enabled,
        TR_KEY_lpd_enabled,
        TR_KEY_peer_limit_global,
        TR_KEY_peer_limit_per_torrent,
        TR_KEY_peer_port,
        TR_KEY_peer_port_random_on_start,
        TR_KEY_pex_enabled,
        TR_KEY_port_forwarding_enabled,
        TR_KEY_queue_stalled_enabled,
        TR_KEY_queue_stalled_minutes,
        TR_KEY_rename_partial_files,
        TR_KEY_rpc_version,
        TR_KEY_rpc_version_minimum,
        TR_KEY_script_torrent_done_enabled,
        TR_KEY_script_torrent_done_filename,
        TR_KEY_seed_queue_enabled,
        TR_KEY_seed_queue_size,
        TR_KEY_seedRatioLimit,
        TR_KEY_seedRatioLimited,
        TR_KEY_session_id,
        TR_KEY_speed_limit_down,
        TR_KEY_speed_limit_down_enabled,
        TR_KEY_speed_limit_up,
        TR_KEY_speed_limit_up_enabled,
        TR_KEY_start_added_torrents,
        TR_KEY_trash_original_torrent_files,
        TR_KEY_units,
        TR_KEY_utp_enabled,
        TR_KEY_version
    };

    // what we got
    std::set<tr_quark> actual_keys;
    tr_quark key;
    tr_variant* val;
    size_t n = 0;
    while ((tr_variantDictChild(args, n++, &key, &val)))
    {
        actual_keys.insert(key);
    }

    auto missing_keys = std::vector<tr_quark>{};
    std::set_difference(std::begin(expected_keys), std::end(expected_keys),
        std::begin(actual_keys), std::end(actual_keys),
        std::inserter(missing_keys, std::begin(missing_keys)));
    EXPECT_EQ(decltype(missing_keys) {}, missing_keys);

    auto unexpected_keys = std::vector<tr_quark>{};
    std::set_difference(std::begin(actual_keys), std::end(actual_keys),
        std::begin(expected_keys), std::end(expected_keys),
        std::inserter(unexpected_keys, std::begin(unexpected_keys)));
    EXPECT_EQ(decltype(unexpected_keys) {}, unexpected_keys);

    // cleanup
    tr_variantFree(&response);
    tr_torrentRemove(tor, false, nullptr);
}

} // namespace test

} // namespace libtransmission
