// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <map>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>

#include <libtransmission-app/dbus-peer-record.h>
#include <libtransmission-app/interop-names.h>

#include "test-fixtures.h"

using namespace std::string_view_literals;

namespace tr::test
{

using DBusPeerRecordTest = libtransmission::test::SandboxedTest;

namespace
{

void write_raw(std::string_view const config_dir, std::string_view const contents)
{
    tr_file_save(tr_pathbuf{ config_dir, '/', interop::PeerRecordFilename }, contents);
}

} // namespace

TEST_F(DBusPeerRecordTest, roundTrips)
{
    auto const written = interop::DBusPeerRecord{ ":1.42",
                                                  "com.transmissionbt.Transmission",
                                                  "/com/transmissionbt/Transmission" };
    interop::write_dbus_peer_record(sandboxDir(), written);

    auto const read = interop::read_dbus_peer_record(sandboxDir());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(written.bus_name, read->bus_name);
    EXPECT_EQ(written.interface, read->interface);
    EXPECT_EQ(written.path, read->path);
}

TEST_F(DBusPeerRecordTest, readsNothingWhenNothingWasRecorded)
{
    EXPECT_FALSE(interop::read_dbus_peer_record(sandboxDir()).has_value());
}

TEST_F(DBusPeerRecordTest, readsNothingFromARecordItCannotUnderstand)
{
    static auto constexpr Unusable = std::array{
        "bus-name=:1.5\n"sv, // no interface, no path
        "interface=com.transmissionbt.Transmission\npath=/com/transmissionbt/Transmission\n"sv, // no bus name
        // well-known, not unique
        "bus-name=com.transmissionbt.Transmission\ninterface=com.transmissionbt.Transmission\npath=/x\n"sv,
        "bus-name=:1.5\ninterface=nodots\npath=/x\n"sv,
        "bus-name=:1.5\ninterface=com.transmissionbt.Transmission\npath=noslash\n"sv,
        "gibberish\n"sv,
    };

    for (auto const contents : Unusable)
    {
        write_raw(sandboxDir(), contents);
        EXPECT_FALSE(interop::read_dbus_peer_record(sandboxDir()).has_value()) << contents;
    }
}

// A record comes from another client, so nothing constrains it until this reads it.
// GDBus refuses a name outside the grammar by answering nothing rather than reporting an error,
// so a bad name that gets past here looks exactly like a peer that is not there.
TEST_F(DBusPeerRecordTest, readsNothingFromANameDBusWouldReject)
{
    static auto constexpr Rejected = std::array{
        // Hyphens are legal in a bus name but not in an interface name.
        "bus-name=:1.5\ninterface=com.my-client.Transmission\npath=/x\n"sv,
        "bus-name=:1.5\ninterface=.\npath=/x\n"sv,
        "bus-name=:1.5\ninterface=com..x\npath=/x\n"sv,
        "bus-name=:1.5\ninterface=com.1abc.X\npath=/x\n"sv, // an element may not lead with a digit
        // Nor in an object path.
        "bus-name=:1.5\ninterface=org.example.Client\npath=/a-b\n"sv,
        "bus-name=:1.5\ninterface=org.example.Client\npath=/a/\n"sv,
        "bus-name=:1.5\ninterface=org.example.Client\npath=//x\n"sv,
        "bus-name=:1.5\ninterface=org.example.Client\npath=/a b\n"sv,
        "bus-name=:\ninterface=org.example.Client\npath=/x\n"sv,
        "bus-name=:1.\ninterface=org.example.Client\npath=/x\n"sv,
        "bus-name=:1 5\ninterface=org.example.Client\npath=/x\n"sv,
        "bus-name=:1\ninterface=org.example.Client\npath=/x\n"sv, // a bus name needs two elements
    };

    for (auto const contents : Rejected)
    {
        write_raw(sandboxDir(), contents);
        EXPECT_FALSE(interop::read_dbus_peer_record(sandboxDir()).has_value()) << contents;
    }
}

// The grammar is D-Bus's, not this client's naming habits.
// A separately-built client picks its own names, and refusing them would leave it unreachable.
TEST_F(DBusPeerRecordTest, readsTheNamesAnotherClientMayLegitimatelyUse)
{
    static auto constexpr Accepted = std::array{
        "bus-name=:1.0\ninterface=a.b\npath=/\n"sv,
        "bus-name=:a-b.c\ninterface=org.example.Client\npath=/org/example/Client\n"sv, // hyphen, legal here
        "bus-name=:1.2.3\ninterface=org.example._1\npath=/x_1/y2\n"sv,
    };

    for (auto const contents : Accepted)
    {
        write_raw(sandboxDir(), contents);
        EXPECT_TRUE(interop::read_dbus_peer_record(sandboxDir()).has_value()) << contents;
    }
}

TEST_F(DBusPeerRecordTest, ignoresKeysItDoesNotKnow)
{
    write_raw(
        sandboxDir(),
        "bus-name=:1.7\ninterface=org.example.Client\npath=/org/example/Client\nrpc-endpoint=http://x/\n"sv);

    auto const read = interop::read_dbus_peer_record(sandboxDir());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(":1.7", read->bus_name);
}

TEST_F(DBusPeerRecordTest, toleratesWhitespaceAroundKeysAndValues)
{
    write_raw(sandboxDir(), "bus-name = :1.9 \r\n interface = org.example.Client\r\n path = /org/example/Client\r\n"sv);

    auto const read = interop::read_dbus_peer_record(sandboxDir());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(":1.9", read->bus_name);
    EXPECT_EQ("org.example.Client", read->interface);
    EXPECT_EQ("/org/example/Client", read->path);
}

TEST_F(DBusPeerRecordTest, readsNothingFromAnOversizeFile)
{
    auto contents = std::string{ "bus-name=:1.5\ninterface=com.example.X\npath=/x\n" };
    contents.append(8192U, '#');
    write_raw(sandboxDir(), contents);

    EXPECT_FALSE(interop::read_dbus_peer_record(sandboxDir()).has_value());
}

// Which client a launch on this config dir is handed to.
// Both D-Bus transports ask this, so it is settled here rather than once per toolkit.
namespace
{

auto constexpr Self = ":1.7"sv;

// Stands in for the bus. A name this class was told about answers with an owner,
// and every other name answers with nothing.
class Bus
{
public:
    void give(std::string_view const name, std::string_view const owner)
    {
        owners_.try_emplace(std::string{ name }, owner);
    }

    [[nodiscard]] interop::DBusNameOwner owner_lookup()
    {
        return [this](std::string_view const name)
        {
            auto const iter = owners_.find(std::string{ name });
            return iter != std::end(owners_) ? iter->second : std::string{};
        };
    }

private:
    std::map<std::string, std::string> owners_;
};

void record(std::string_view const config_dir, std::string_view const bus_name)
{
    interop::write_dbus_peer_record(
        config_dir,
        { std::string{ bus_name }, TR_INTEROP_DBUS_INTERFACE_NAME, TR_INTEROP_DBUS_OBJECT_PATH });
}

} // namespace

TEST_F(DBusPeerRecordTest, prefersWhoeverRecordedItselfOnTheConfigDir)
{
    record(sandboxDir(), ":1.9");

    auto bus = Bus{};
    bus.give(":1.9", ":1.9");
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4"); // someone else holds the shared name

    auto const peer = interop::select_dbus_peer(sandboxDir(), Self, bus.owner_lookup());
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(":1.9", peer->bus_name);
}

// A record outlives the client that wrote it, so a name nobody owns is a client that has exited.
TEST_F(DBusPeerRecordTest, passesOverARecordedNameNobodyOwns)
{
    record(sandboxDir(), ":1.9");

    auto bus = Bus{};
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4");

    auto const peer = interop::select_dbus_peer(sandboxDir(), Self, bus.owner_lookup());
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(TR_INTEROP_DBUS_SERVICE_NAME, peer->bus_name);
}

TEST_F(DBusPeerRecordTest, neverSelectsThisProcess)
{
    record(sandboxDir(), Self);

    auto bus = Bus{};
    bus.give(Self, Self);
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, Self); // this process holds the shared name too

    EXPECT_FALSE(interop::select_dbus_peer(sandboxDir(), Self, bus.owner_lookup()).has_value());
}

// How a client too old to leave a record, or one whose record could not be written, is still reached.
TEST_F(DBusPeerRecordTest, fallsBackToTheWellKnownNameWithNoRecord)
{
    auto bus = Bus{};
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4");

    auto const peer = interop::select_dbus_peer(sandboxDir(), Self, bus.owner_lookup());
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(TR_INTEROP_DBUS_SERVICE_NAME, peer->bus_name);
    EXPECT_EQ(TR_INTEROP_DBUS_INTERFACE_NAME, peer->interface);
    EXPECT_EQ(TR_INTEROP_DBUS_OBJECT_PATH, peer->path);
}

TEST_F(DBusPeerRecordTest, findsNobodyOnAnEmptyBus)
{
    auto bus = Bus{};
    EXPECT_FALSE(interop::select_dbus_peer(sandboxDir(), Self, bus.owner_lookup()).has_value());
}

// The record names an interface and path rather than assuming them.
// That is how a client built from other sources can be handed a torrent.
TEST_F(DBusPeerRecordTest, callsAPeerAtTheNamesItRecorded)
{
    write_raw(sandboxDir(), "bus-name=:1.9\ninterface=org.example.Client\npath=/org/example/Client\n"sv);

    auto bus = Bus{};
    bus.give(":1.9", ":1.9");

    auto const peer = interop::select_dbus_peer(sandboxDir(), Self, bus.owner_lookup());
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ("org.example.Client", peer->interface);
    EXPECT_EQ("/org/example/Client", peer->path);
}

} // namespace tr::test
