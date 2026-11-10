// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <libtransmission/error.h> // tr_error

namespace tr::interop
{

// This struct holds the contents of `${config_dir}/dbus-peer`.
// That file exists to give another client the information it needs to
// hand a magnet link or .torrent file off to a client that's already
// running and using this config dir.
//
// Clients that support this mechanism must use the file's
// bus/iface/path instead of hardcoded values,
// allowing other clients to speak to Transmission and vice versa.
//
// Whichever client wrote `dbus-peer` is the one that will receive
// a .torrent file or magnet link passed over D-Bus.
struct DBusPeerRecord
{
    std::string bus_name;
    std::string interface;
    std::string path;

    // The bus that issued `bus_name`, as org.freedesktop.DBus.GetId reports it.
    // Optional in the file: a record from before this key names no bus.
    //
    // A unique name identifies a connection only together with its bus. A bus
    // never reuses one, but the next bus starts over, so after a restart the
    // recorded name can belong to an unrelated process.
    std::string bus_id;
};

// The file format itself. Clients call select_dbus_peer() and publish_dbus_peer() below
// instead. These are declared so that tests can check the record grammar directly.
// The two functions below cannot. They answer "nobody to call" for a malformed record
// and for a departed one alike.

// Returns nullopt if the record is missing, unreadable, or ill-formed.
[[nodiscard]] std::optional<DBusPeerRecord> read_dbus_peer_record(std::string_view config_dir);

// Returns false, and sets `error`, when the record can't be written.
// A client that doesn't record itself may not be found by a later launch
// on this config dir, which will then start a second client instead.
// An empty `bus_id` is left out of the file rather than written empty.
bool write_dbus_peer_record(std::string_view config_dir, DBusPeerRecord const& record, tr_error* error = nullptr);

// Which connection owns `name` right now, or empty when nobody does.
// Supplied by the transport, since only it speaks to the bus.
using DBusNameOwner = std::function<std::string(std::string_view name)>;

// Whether the client `record` names answers ConfigDir() with a dir within
// DBusPeerProbeTimeout, called on the record's own interface and path.
// Supplied by the transport. select_dbus_peer() asks it only about a record
// that cannot be placed on a bus, since a bus ID settles that without a call.
using DBusPeerProbe = std::function<bool(DBusPeerRecord const& record)>;

// How long a launch gives a record that names no bus to prove it names a client. Such a
// record may point at an unrelated process that never answers, and the default call
// timeout is far longer.
// A client this slow to answer looks absent, and the launch then starts and ends on
// its config dir lock, so the wait is short rather than instant.
inline constexpr auto DBusPeerProbeTimeout = std::chrono::seconds{ 3 };

// The same wait as the integer millisecond count a D-Bus binding takes for a call timeout.
inline constexpr int DBusPeerProbeTimeoutMsec = static_cast<int>(std::chrono::milliseconds{ DBusPeerProbeTimeout }.count());

// The contact information for the client running in `config_dir`,
// or nullopt when no other client is there.
//
// `bus_id` is this bus's ID, or empty when the bus driver did not say.
// `self` is this connection's unique name, so a client never selects itself.
//
// `${config_dir}/dbus-peer` is used when it names a live connection on this bus.
// A record from another bus is passed over without a call. A record that cannot be
// placed, because it or this launch has no bus ID, is used only if `probe` finds a
// client answering there, since a live name alone could be anyone's.
// As a fallback, the owner (if any) of the well-known name is used.
// If neither names a live connection, there is no other client to reach.
[[nodiscard]] std::optional<DBusPeerRecord> select_dbus_peer(
    std::string_view config_dir,
    std::string_view bus_id,
    std::string_view self,
    DBusNameOwner const& name_owner,
    DBusPeerProbe const& probe);

// Publishes this client in three steps, in this order:
//  1. the object, so that once either name below resolves, there is something to answer with;
//  2. the well-known name, which is how a caller with no record to read reaches this client;
//  3. the record, which points a later launch on this config dir at this client and no other.
//
// `bus_id` is this bus's ID, or empty when the bus driver did not say. A record
// without it costs every later launch a probe, so transports should supply it.
//
// A false from `register_object` skips steps 2 and 3. A name that resolves to a client with
// no object is worse than no name at all.
// A false from `claim_well_known_name` is not a failure. Another session already owns that
// name and answers there, and callers still reach this client through its record.
void publish_dbus_peer(
    std::string_view config_dir,
    std::string_view bus_id,
    std::string_view unique_name,
    std::function<bool()> const& register_object,
    std::function<bool()> const& claim_well_known_name);

} // namespace tr::interop
