// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <gtest/gtest.h>

#include "libtransmission/bep55-introducer-store.h"

using namespace bep55;

static tr_socket_address make_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
{
    auto addr = tr_address{};
    addr.type = TR_AF_INET;
    auto const* p = reinterpret_cast<uint8_t*>(&addr.addr.addr4.s_addr);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
    const_cast<uint8_t*>(p)[0] = a;
    const_cast<uint8_t*>(p)[1] = b;
    const_cast<uint8_t*>(p)[2] = c;
    const_cast<uint8_t*>(p)[3] = d;
    // NOLINTEND(cppcoreguidelines-pro-type-member-init)
    return tr_socket_address{ addr, tr_port::from_host(port) };
}

TEST(Bep55IntroducerStore, RecordAndLookup)
{
    HolepunchIntroducerStore store;

    auto const target = make_ipv4(192, 168, 1, 1, 6881);
    auto const introducer = make_ipv4(10, 0, 0, 1, 6882);

    EXPECT_FALSE(store.find_introducer(target));

    store.record(target, introducer);

    auto found = store.find_introducer(target);
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, introducer);
}

TEST(Bep55IntroducerStore, UnknownTargetReturnsNullopt)
{
    HolepunchIntroducerStore store;

    auto const target = make_ipv4(192, 168, 1, 1, 6881);
    EXPECT_FALSE(store.find_introducer(target));
}

TEST(Bep55IntroducerStore, MaxSizeEvictsOldest)
{
    HolepunchIntroducerStore store;

    for (size_t i = 0; i < HolepunchIntroducerStore::MaxSize; ++i)
    {
        auto const target = make_ipv4(192, 168, 1, 0, static_cast<uint16_t>(i));
        auto const introducer = make_ipv4(10, 0, 0, 0, static_cast<uint16_t>(i));
        store.record(target, introducer);
    }

    for (size_t i = 0; i < HolepunchIntroducerStore::MaxSize; ++i)
    {
        auto const target = make_ipv4(192, 168, 1, 0, static_cast<uint16_t>(i));
        EXPECT_TRUE(store.find_introducer(target));
    }

    auto const overflow = make_ipv4(192, 168, 1, 0, static_cast<uint16_t>(HolepunchIntroducerStore::MaxSize));
    auto const overflow_intro = make_ipv4(10, 0, 0, 99, 0);
    store.record(overflow, overflow_intro);

    auto const evicted = make_ipv4(192, 168, 1, 0, 0);
    EXPECT_FALSE(store.find_introducer(evicted));

    EXPECT_TRUE(store.find_introducer(overflow));
}

TEST(Bep55IntroducerStore, MultipleTargetsAndIntroducers)
{
    HolepunchIntroducerStore store;

    auto const target1 = make_ipv4(192, 168, 1, 1, 6881);
    auto const target2 = make_ipv4(192, 168, 1, 2, 6882);
    auto const introducer1 = make_ipv4(10, 0, 0, 1, 6881);
    auto const introducer2 = make_ipv4(10, 0, 0, 2, 6882);

    store.record(target1, introducer1);
    store.record(target2, introducer2);

    auto found1 = store.find_introducer(target1);
    auto found2 = store.find_introducer(target2);

    ASSERT_TRUE(found1);
    ASSERT_TRUE(found2);
    EXPECT_EQ(*found1, introducer1);
    EXPECT_EQ(*found2, introducer2);
}
