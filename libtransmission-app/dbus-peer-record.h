// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

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
bool write_dbus_peer_record(std::string_view config_dir, DBusPeerRecord const& record, tr_error* error = nullptr);

// Which connection owns `name` right now, or empty when nobody does.
// Supplied by the transport, since only it speaks to the bus.
using DBusNameOwner = std::function<std::string(std::string_view name)>;

// The contact information for the client running in `config_dir`,
// or nullopt when no other client is there.
//
// `self` is this connection's unique name, so a client never selects itself.
//
// If `${config_dir}/dbus-peer` is found, its contents are used.
// As a fallback, the owner (if any) of the well-known name is used.
// If neither names a live connection, there is no other client to reach.
[[nodiscard]] std::optional<DBusPeerRecord> select_dbus_peer(
    std::string_view config_dir,
    std::string_view self,
    DBusNameOwner const& name_owner);

// Publishes this client in three steps, in this order:
//  1. the object, so that once either name below resolves, there is something to answer with;
//  2. the well-known name, which is how a caller with no record to read reaches this client;
//  3. the record, which points a later launch on this config dir at this client and no other.
//
// A false from `register_object` skips steps 2 and 3. A name that resolves to a client with
// no object is worse than no name at all.
// A false from `claim_well_known_name` is not a failure. Another session already owns that
// name and answers there, and callers still reach this client through its record.
void publish_dbus_peer(
    std::string_view config_dir,
    std::string_view unique_name,
    std::function<bool()> const& register_object,
    std::function<bool()> const& claim_well_known_name);

} // namespace tr::interop
