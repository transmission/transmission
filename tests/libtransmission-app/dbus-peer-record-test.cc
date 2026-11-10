// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] tr_pathbuf record_path(std::string_view const config_dir)
{
    return tr_pathbuf{ config_dir, '/', interop::PeerRecordFilename };
}

void write_raw(std::string_view const config_dir, std::string_view const contents)
{
    tr_file_save(record_path(config_dir), contents);
}

} // namespace

TEST_F(DBusPeerRecordTest, roundTrips)
{
    auto const written = interop::DBusPeerRecord{ ":1.42",
                                                  "com.transmissionbt.Transmission",
                                                  "/com/transmissionbt/Transmission",
                                                  "9e2b56eeebe8d9263fb4cf6a6ad13153" };
    interop::write_dbus_peer_record(sandboxDir(), written);

    auto const read = interop::read_dbus_peer_record(sandboxDir());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(written.bus_name, read->bus_name);
    EXPECT_EQ(written.interface, read->interface);
    EXPECT_EQ(written.path, read->path);
    EXPECT_EQ(written.bus_id, read->bus_id);
}

// A record from before the bus ID was recorded has no `bus-id` line, so a reader
// must not need one, and a writer with no ID to give writes none.
TEST_F(DBusPeerRecordTest, leavesOutABusIdItDoesNotHave)
{
    interop::write_dbus_peer_record(sandboxDir(), { ":1.42", "org.example.Client", "/org/example/Client", {} });

    auto contents = std::vector<char>{};
    ASSERT_TRUE(tr_file_read(record_path(sandboxDir()), contents));
    auto const text = std::string_view{ std::data(contents), std::size(contents) };
    EXPECT_EQ(std::string_view::npos, text.find("bus-id"sv));

    auto const read = interop::read_dbus_peer_record(sandboxDir());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(":1.42", read->bus_name);
    EXPECT_TRUE(std::empty(read->bus_id));
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
auto constexpr ThisBus = "9e2b56eeebe8d9263fb4cf6a6ad13153"sv;
auto constexpr OtherBus = "0123456789abcdef0123456789abcdef"sv;
auto constexpr NoBus = ""sv;

// Stands in for the bus and for the clients on it. A name this class was told about
// answers with an owner, and every other name answers with nothing. A probe reaches
// only a name this class was told answers, and every probe is kept for the test to see.
class Bus
{
public:
    void give(std::string_view const name, std::string_view const owner)
    {
        owners_.try_emplace(std::string{ name }, owner);
    }

    void answers(std::string_view const name)
    {
        answering_.emplace(name);
    }

    [[nodiscard]] interop::DBusNameOwner owner_lookup()
    {
        return [this](std::string_view const name)
        {
            auto const iter = owners_.find(std::string{ name });
            return iter != std::end(owners_) ? iter->second : std::string{};
        };
    }

    [[nodiscard]] interop::DBusPeerProbe probe()
    {
        return [this](interop::DBusPeerRecord const& record)
        {
            probed_.push_back(record);
            return answering_.count(record.bus_name) != 0U;
        };
    }

    [[nodiscard]] std::vector<interop::DBusPeerRecord> const& probed() const noexcept
    {
        return probed_;
    }

private:
    std::map<std::string, std::string> owners_;
    std::set<std::string> answering_;
    std::vector<interop::DBusPeerRecord> probed_;
};

void record(std::string_view const config_dir, std::string_view const bus_name, std::string_view const bus_id = ThisBus)
{
    interop::write_dbus_peer_record(
        config_dir,
        { std::string{ bus_name }, TR_INTEROP_DBUS_INTERFACE_NAME, TR_INTEROP_DBUS_OBJECT_PATH, std::string{ bus_id } });
}

[[nodiscard]] std::optional<interop::DBusPeerRecord> select(
    std::string_view const config_dir,
    Bus& bus,
    std::string_view const bus_id = ThisBus)
{
    return interop::select_dbus_peer(config_dir, bus_id, Self, bus.owner_lookup(), bus.probe());
}

} // namespace

TEST_F(DBusPeerRecordTest, prefersWhoeverRecordedItselfOnTheConfigDir)
{
    record(sandboxDir(), ":1.9");

    auto bus = Bus{};
    bus.give(":1.9", ":1.9");
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4"); // someone else holds the shared name

    auto const peer = select(sandboxDir(), bus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(":1.9", peer->bus_name);

    // The bus ID placed the record here, and a bus never reuses a unique name.
    EXPECT_TRUE(std::empty(bus.probed()));
}

// A record outlives the client that wrote it, so a name nobody owns is a client that has exited.
TEST_F(DBusPeerRecordTest, passesOverARecordedNameNobodyOwns)
{
    record(sandboxDir(), ":1.9");

    auto bus = Bus{};
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4");

    auto const peer = select(sandboxDir(), bus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(TR_INTEROP_DBUS_SERVICE_NAME, peer->bus_name);
    EXPECT_TRUE(std::empty(bus.probed()));
}

// After a reboot, the recorded name can belong to an unrelated process that never answers.
// The record is passed over without a call, and whoever holds the shared name is tried instead.
TEST_F(DBusPeerRecordTest, passesOverARecordFromAnotherBusWithoutCallingIt)
{
    record(sandboxDir(), ":1.9", OtherBus);

    auto bus = Bus{};
    bus.give(":1.9", ":1.9");
    bus.answers(":1.9"); // would be taken for a client if it were asked

    EXPECT_FALSE(select(sandboxDir(), bus).has_value());
    EXPECT_TRUE(std::empty(bus.probed()));

    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4");
    auto const peer = select(sandboxDir(), bus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(TR_INTEROP_DBUS_SERVICE_NAME, peer->bus_name);
    EXPECT_TRUE(std::empty(bus.probed()));
}

// A record that names no bus is how a release before the bus ID left one. It is still
// the only way to reach that release when it does not hold the shared name, so it is
// asked, at the names it recorded, and taken when it answers.
TEST_F(DBusPeerRecordTest, probesARecordThatNamesNoBus)
{
    write_raw(sandboxDir(), "bus-name=:1.9\ninterface=org.example.Client\npath=/org/example/Client\n"sv);

    auto bus = Bus{};
    bus.give(":1.9", ":1.9");
    bus.answers(":1.9");

    auto const peer = select(sandboxDir(), bus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(":1.9", peer->bus_name);

    ASSERT_EQ(1U, std::size(bus.probed()));
    EXPECT_EQ(":1.9", bus.probed().front().bus_name);
    EXPECT_EQ("org.example.Client", bus.probed().front().interface);
    EXPECT_EQ("/org/example/Client", bus.probed().front().path);
}

// ...and passed over when it does not. The live name is then something other than
// the client that wrote the record, and the shared name is tried instead.
TEST_F(DBusPeerRecordTest, passesOverARecordThatNamesNoBusWhenNobodyAnswersThere)
{
    record(sandboxDir(), ":1.9", NoBus);

    auto bus = Bus{};
    bus.give(":1.9", ":1.9");
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4");

    auto const peer = select(sandboxDir(), bus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(TR_INTEROP_DBUS_SERVICE_NAME, peer->bus_name);
    EXPECT_EQ(1U, std::size(bus.probed()));
}

// A record that names no bus outlives its client too, and then nobody owns its name.
// The bus driver settles that, so the name is not probed.
TEST_F(DBusPeerRecordTest, doesNotProbeARecordThatNamesNoBusWhenNobodyOwnsItsName)
{
    record(sandboxDir(), ":1.9", NoBus);

    auto bus = Bus{};
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4");

    auto const peer = select(sandboxDir(), bus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(TR_INTEROP_DBUS_SERVICE_NAME, peer->bus_name);
    EXPECT_TRUE(std::empty(bus.probed()));
}

// A bus whose ID this launch could not learn cannot place any record,
// so each one is asked rather than trusted or thrown away.
TEST_F(DBusPeerRecordTest, probesEveryRecordWhenThisBusHasNoId)
{
    record(sandboxDir(), ":1.9", OtherBus);

    auto bus = Bus{};
    bus.give(":1.9", ":1.9");

    EXPECT_FALSE(select(sandboxDir(), bus, NoBus).has_value());
    EXPECT_EQ(1U, std::size(bus.probed()));

    bus.answers(":1.9");
    auto const peer = select(sandboxDir(), bus, NoBus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(":1.9", peer->bus_name);
}

TEST_F(DBusPeerRecordTest, neverSelectsThisProcess)
{
    record(sandboxDir(), Self, NoBus); // not even when the record cannot be placed

    auto bus = Bus{};
    bus.give(Self, Self);
    bus.answers(Self);
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, Self); // this process holds the shared name too

    EXPECT_FALSE(select(sandboxDir(), bus).has_value());
    EXPECT_TRUE(std::empty(bus.probed()));
}

// How a client too old to leave a record, or one whose record could not be written, is still reached.
TEST_F(DBusPeerRecordTest, fallsBackToTheWellKnownNameWithNoRecord)
{
    auto bus = Bus{};
    bus.give(TR_INTEROP_DBUS_SERVICE_NAME, ":1.4");

    auto const peer = select(sandboxDir(), bus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ(TR_INTEROP_DBUS_SERVICE_NAME, peer->bus_name);
    EXPECT_EQ(TR_INTEROP_DBUS_INTERFACE_NAME, peer->interface);
    EXPECT_EQ(TR_INTEROP_DBUS_OBJECT_PATH, peer->path);
}

TEST_F(DBusPeerRecordTest, findsNobodyOnAnEmptyBus)
{
    auto bus = Bus{};
    EXPECT_FALSE(select(sandboxDir(), bus).has_value());
}

// The record names an interface and path rather than assuming them.
// That is how a client built from other sources can be handed a torrent.
TEST_F(DBusPeerRecordTest, callsAPeerAtTheNamesItRecorded)
{
    interop::write_dbus_peer_record(
        sandboxDir(),
        { ":1.9", "org.example.Client", "/org/example/Client", std::string{ ThisBus } });

    auto bus = Bus{};
    bus.give(":1.9", ":1.9");

    auto const peer = select(sandboxDir(), bus);
    ASSERT_TRUE(peer.has_value());
    EXPECT_EQ("org.example.Client", peer->interface);
    EXPECT_EQ("/org/example/Client", peer->path);
}

} // namespace tr::test
