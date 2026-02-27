// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/clients.h>
#include <libtransmission/peer-mgr.h>
#include <libtransmission/peer-msgs.h>
#include <libtransmission/quark.h>
#include <libtransmission/values.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
class PeerMsgsTest : public SessionTest
{
};

namespace
{

class PeerMsgsHarness final : public tr_peerMsgs
{
public:
    PeerMsgsHarness(tr_torrent const& tor, std::shared_ptr<tr_peer_info> peer_info_in, tr_peer_id_t peer_id)
        : tr_peerMsgs{ tor, std::move(peer_info_in), peer_id, false, false, false }
    {
    }

    void set_user_agent_for_test(std::string value)
    {
        set_user_agent(std::move(value));
    }

    [[nodiscard]] tr::Values::Speed get_piece_speed(uint64_t /*now*/, tr_direction /*direction*/) const override
    {
        return {};
    }

    [[nodiscard]] std::string display_name() const override
    {
        return "PeerMsgsHarness";
    }

    [[nodiscard]] tr_bitfield const& has() const noexcept override
    {
        return have_;
    }

    [[nodiscard]] size_t active_req_count(tr_direction /*direction*/) const noexcept override
    {
        return 0U;
    }

    void request_blocks(tr_block_span_t const* /*block_spans*/, size_t /*n_spans*/) override
    {
    }

    void ban() override
    {
    }

    [[nodiscard]] tr_socket_address socket_address() const override
    {
        return peer_info->listen_socket_address();
    }

    void set_choke(bool peer_is_choked) override
    {
        set_peer_choked(peer_is_choked);
    }

    void set_interested(bool client_is_interested) override
    {
        set_client_interested(client_is_interested);
    }

    void pulse() override
    {
    }

    void on_torrent_got_metainfo() noexcept override
    {
    }

    void on_piece_completed(tr_piece_index_t /*piece*/) override
    {
    }

private:
    tr_bitfield have_{ 0U };
};

[[nodiscard]] auto make_peer_info()
{
    return std::make_shared<tr_peer_info>(tr_address{}, 0, TR_PEER_FROM_PEX, std::function<tr_port()>{});
}

[[nodiscard]] auto make_unique_peer_id_prefix()
{
    auto const hex = fmt::format("{:07x}", static_cast<unsigned>(tr_time() & 0x0FFFFFFF));
    auto prefix = std::array<char, 8>{};
    prefix[0] = 'z';
    std::copy_n(std::data(hex), 7, std::begin(prefix) + 1);
    return prefix;
}

} // namespace

TEST_F(PeerMsgsTest, userAgentStringsAreNotInterned)
{
    static_assert(std::is_same_v<decltype(std::declval<tr_peerMsgs const&>().user_agent()), std::string const&>);

    auto const* const tor = zeroTorrentMagnetInit();
    ASSERT_NE(tor, nullptr);

    auto peer_id = tr_peer_id_t{};
    auto const prefix = make_unique_peer_id_prefix();
    std::copy_n(std::begin(prefix), std::size(prefix), std::begin(peer_id));
    std::fill(std::begin(peer_id) + static_cast<ptrdiff_t>(std::size(prefix)), std::end(peer_id), 'x');

    auto client_buf = std::array<char, 128>{};
    tr_clientForId(std::data(client_buf), std::size(client_buf), peer_id);
    auto const client_name = std::string_view{ std::data(client_buf) };
    ASSERT_FALSE(std::empty(client_name));
    ASSERT_FALSE(tr_quark_lookup(client_name).has_value());

    auto msgs = PeerMsgsHarness{ *tor, make_peer_info(), peer_id };
    EXPECT_EQ(client_name, msgs.user_agent());
    EXPECT_FALSE(tr_quark_lookup(client_name).has_value());

    auto const ltep_user_agent = fmt::format("leak-probe-ltep-{}", client_name);
    ASSERT_FALSE(tr_quark_lookup(ltep_user_agent).has_value());
    msgs.set_user_agent_for_test(ltep_user_agent);
    EXPECT_EQ(ltep_user_agent, msgs.user_agent());
    EXPECT_FALSE(tr_quark_lookup(ltep_user_agent).has_value());
}

} // namespace tr::test
