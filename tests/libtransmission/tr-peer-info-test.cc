// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <ctime>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

#include <libtransmission/transmission.h>

#include <libtransmission/net.h>
#include <libtransmission/peer-mgr.h>

#include "gtest/gtest.h"

using namespace std::literals;

using PeerInfoTest = ::testing::Test;

TEST_F(PeerInfoTest, mergeConnectable)
{
    // Same as the truth table in tr_peer_info::merge()
    static auto constexpr Tests = std::array{
        std::pair{ std::tuple{ std::optional{ true }, true, std::optional{ true }, true }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional{ true }, true, std::optional{ true }, false }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional{ true }, true, std::optional{ false }, false }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional{ true }, true, std::optional<bool>{}, true }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional{ true }, true, std::optional<bool>{}, false }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional{ true }, false, std::optional{ true }, true }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional{ true }, false, std::optional{ true }, false }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional{ true }, false, std::optional{ false }, false }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional{ true }, false, std::optional<bool>{}, true }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional{ true }, false, std::optional<bool>{}, false }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional{ false }, false, std::optional{ true }, true }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional{ false }, false, std::optional{ true }, false }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional{ false }, false, std::optional{ false }, false }, std::optional{ false } },
        std::pair{ std::tuple{ std::optional{ false }, false, std::optional<bool>{}, true }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional{ false }, false, std::optional<bool>{}, false }, std::optional{ false } },
        std::pair{ std::tuple{ std::optional<bool>{}, true, std::optional{ true }, true }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional<bool>{}, true, std::optional{ true }, false }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional<bool>{}, true, std::optional{ false }, false }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional<bool>{}, true, std::optional<bool>{}, true }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional<bool>{}, true, std::optional<bool>{}, false }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional<bool>{}, false, std::optional{ true }, true }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional<bool>{}, false, std::optional{ true }, false }, std::optional{ true } },
        std::pair{ std::tuple{ std::optional<bool>{}, false, std::optional{ false }, false }, std::optional{ false } },
        std::pair{ std::tuple{ std::optional<bool>{}, false, std::optional<bool>{}, true }, std::optional<bool>{} },
        std::pair{ std::tuple{ std::optional<bool>{}, false, std::optional<bool>{}, false }, std::optional<bool>{} },
    };
    static_assert(std::size(Tests) == 25U);

    for (auto const& [condition, result] : Tests)
    {
        auto const& [this_connectable, this_connected, that_connectable, that_connected] = condition;

        auto info_this = tr_peer_info{ tr_address{}, 0, TR_PEER_FROM_PEX, {} };
        auto info_that = tr_peer_info{ tr_address{}, 0, TR_PEER_FROM_PEX, {} };

        if (this_connectable)
        {
            info_this.set_connectable(*this_connectable);
        }
        info_this.set_connected(time_t{}, this_connected);

        if (that_connectable)
        {
            info_that.set_connectable(*that_connectable);
        }
        info_that.set_connected(time_t{}, that_connected);

        info_this.merge(info_that);

        EXPECT_EQ(info_this.is_connectable(), result);
    }
}

TEST_F(PeerInfoTest, updateCanonicalPriority)
{
    static auto constexpr Tests = std::array{
        std::tuple{ "123.213.32.10:51413"sv, "98.76.54.32:6881"sv, uint32_t{ 0xEC2D7224 } },
        std::tuple{ "123.213.32.10:51413"sv, "123.213.32.234:6881"sv, uint32_t{ 0x99568189 } }
    };

    for (auto [client_sockaddr_str, peer_sockaddr_str, expected] : Tests)
    {
        auto client_sockaddr = tr_socket_address::from_string(client_sockaddr_str);
        auto peer_sockaddr = tr_socket_address::from_string(peer_sockaddr_str);
        EXPECT_TRUE(client_sockaddr && peer_sockaddr) << "Test case is bugged";
        if (!client_sockaddr || !peer_sockaddr)
        {
            continue;
        }

        auto const info = tr_peer_info{ *peer_sockaddr,
                                        0,
                                        TR_PEER_FROM_PEX,
                                        client_sockaddr->address(),
                                        [&client_sockaddr]
                                        {
                                            return client_sockaddr->port();
                                        } };
        EXPECT_EQ(info.get_canonical_priority(), expected);
    }
}
