// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#define LIBTRANSMISSION_PEER_MODULE

#include "transmission.h"

#include "bandwidth.h"
#include "peer-mgr-request-allocator.h"
#include "peer-msgs.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

using namespace std::literals;

class RequestAllocatorTest : public ::testing::Test
{
protected:
    template<typename T>
    std::string quarkGetString(T i)
    {
        size_t len;
        char const* const str = tr_quark_get_string(tr_quark(i), &len);
        EXPECT_EQ(strlen(str), len);
        return std::string(str, len);
    }
};

using PeerKey = std::string_view;
using PoolKey = std::string_view;
using RequestAllocator = BlockRequestAllocator<PeerKey, PoolKey>;

class MockMediator final : public RequestAllocator::Mediator
{
public:
    virtual ~MockMediator() = default;

    [[nodiscard]] std::vector<PeerKey> peers() const override
    {
        auto peer_keys = std::vector<PeerKey>{};
        peer_keys.reserve(std::size(peer_to_pools_));
        for (auto const& [peer_key, pools] : peer_to_pools_)
        {
            peer_keys.push_back(peer_key);
        }
        return peer_keys;
    }

    [[nodiscard]] size_t pendingReqCount(PeerKey peer) const override
    {
        return pending_reqs_.at(peer);
    }

    [[nodiscard]] std::vector<PoolKey> pools(PeerKey peer) const override
    {
        return peer_to_pools_.at(peer);
    }

    [[nodiscard]] size_t poolBlockLimit(PoolKey pool) const override
    {
        return pool_limit_.at(pool);
    }

    std::map<PeerKey, std::vector<PoolKey>> peer_to_pools_;
    std::map<PeerKey, size_t> pending_reqs_;
    std::map<PoolKey, size_t> pool_limit_;
};

TEST_F(RequestAllocatorTest, distributesEvenlyWhenAllElseIsEqual)
{
    static auto constexpr Peers = std::array<std::string_view, 3> { "peer-a"sv, "peer-b"sv, "peer-c"sv };
    static auto constexpr SessionPool = "session"sv;

    auto mediator = MockMediator();
    for (auto& peer : Peers)
    {
        mediator.peer_to_pools_.try_emplace(peer, std::vector<std::string_view>{ SessionPool });
        mediator.pending_reqs_.try_emplace(peer, size_t{});
    }
    mediator.pool_limit_.emplace(SessionPool, std::numeric_limits<size_t>::max());

    static auto constexpr AllocCount = 12;
    auto allocation = RequestAllocator::allocateBlockReqs(mediator, AllocCount);
    std::sort(std::begin(allocation), std::end(allocation));
    auto const expected = std::vector<std::pair<PeerKey, size_t>>{
        std::make_pair(Peers[0], AllocCount / std::size(Peers)),
        std::make_pair(Peers[1], AllocCount / std::size(Peers)),
        std::make_pair(Peers[2], AllocCount / std::size(Peers))
    };
    EXPECT_EQ(expected, allocation);
}

TEST_F(RequestAllocatorTest, honorsSpeedLimits)
{
    auto mediator = MockMediator();
    mediator.peer_to_pools_.try_emplace("torrent-1-peer-a"sv, std::vector<std::string_view>{ "torrent-1-pool"sv, "session-pool"sv });
    mediator.peer_to_pools_.try_emplace("torrent-1-peer-b"sv, std::vector<std::string_view>{ "torrent-1-pool"sv, "session-pool"sv });
    mediator.peer_to_pools_.try_emplace("torrent-2-peer-a"sv, std::vector<std::string_view>{ "session-pool"sv }); // no speed limit for torrent 2
    mediator.pending_reqs_.try_emplace("torrent-1-peer-a", size_t{});
    mediator.pending_reqs_.try_emplace("torrent-1-peer-b", size_t{});
    mediator.pending_reqs_.try_emplace("torrent-2-peer-a", size_t{});
    mediator.pool_limit_.try_emplace("torrent-1-pool"sv, 50);
    mediator.pool_limit_.try_emplace("session-pool"sv, 100);

    auto allocation = RequestAllocator::allocateBlockReqs(mediator, 1000);
    std::sort(std::begin(allocation), std::end(allocation));
    auto const expected = std::vector<std::pair<PeerKey, size_t>>{
        std::make_pair("torrent-1-peer-a"sv, 25),
        std::make_pair("torrent-1-peer-b"sv, 25),
        std::make_pair("torrent-2-peer-a"sv, 50)
    };
    EXPECT_EQ(expected, allocation);
}

TEST_F(RequestAllocatorTest, considersBacklog)
{
    auto mediator = MockMediator();
    mediator.peer_to_pools_.try_emplace("torrent-1-peer-a"sv, std::vector<std::string_view>{ "torrent-1-pool"sv, "session-pool"sv });
    mediator.peer_to_pools_.try_emplace("torrent-1-peer-b"sv, std::vector<std::string_view>{ "torrent-1-pool"sv, "session-pool"sv });
    mediator.peer_to_pools_.try_emplace("torrent-2-peer-a"sv, std::vector<std::string_view>{ "session-pool"sv }); // no speed limit for torrent 2
    mediator.pending_reqs_.try_emplace("torrent-1-peer-a", 10);
    mediator.pending_reqs_.try_emplace("torrent-1-peer-b", size_t{});
    mediator.pending_reqs_.try_emplace("torrent-2-peer-a", size_t{});
    mediator.pool_limit_.try_emplace("torrent-1-pool"sv, 50);
    mediator.pool_limit_.try_emplace("session-pool"sv, 100);

    auto allocation = RequestAllocator::allocateBlockReqs(mediator, 1000);
    std::sort(std::begin(allocation), std::end(allocation));
    auto const expected = std::vector<std::pair<PeerKey, size_t>>{
        std::make_pair("torrent-1-peer-a"sv, 15),
        std::make_pair("torrent-1-peer-b"sv, 25),
        std::make_pair("torrent-2-peer-a"sv, 50)
    };
    EXPECT_EQ(expected, allocation);
}

TEST_F(RequestAllocatorTest, stopsWhenOutOfReqs)
{
    auto mediator = MockMediator();
    mediator.peer_to_pools_.try_emplace("torrent-1-peer-a"sv, std::vector<std::string_view>{ "torrent-1-pool"sv, "session-pool"sv });
    mediator.peer_to_pools_.try_emplace("torrent-1-peer-b"sv, std::vector<std::string_view>{ "torrent-1-pool"sv, "session-pool"sv });
    mediator.peer_to_pools_.try_emplace("torrent-2-peer-a"sv, std::vector<std::string_view>{ "session-pool"sv }); // no speed limit for torrent 2
    mediator.pending_reqs_.try_emplace("torrent-1-peer-a", 10);
    mediator.pending_reqs_.try_emplace("torrent-1-peer-b", size_t{});
    mediator.pending_reqs_.try_emplace("torrent-2-peer-a", size_t{});
    mediator.pool_limit_.try_emplace("torrent-1-pool"sv, 50);
    mediator.pool_limit_.try_emplace("session-pool"sv, 100);

    auto allocation = RequestAllocator::allocateBlockReqs(mediator, 23);
    std::sort(std::begin(allocation), std::end(allocation));
    auto const expected = std::vector<std::pair<PeerKey, size_t>>{
        std::make_pair("torrent-1-peer-a"sv, 1),
        std::make_pair("torrent-1-peer-b"sv, 11),
        std::make_pair("torrent-2-peer-a"sv, 11)
    };
    EXPECT_EQ(expected, allocation);
}
