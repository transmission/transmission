#!/usr/bin/env python3
# This file Copyright © Mnemosyne LLC.
# It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
# or any future license endorsed by Mnemosyne LLC.
# License text can be found in the licenses/ folder.

"""A stand-in for a running Transmission instance.

Answers the three interop calls, recording each one to a log file, so the
real client binaries can be exercised as delegating launchers without a
running instance or a display. A launcher of either toolkit must reach this
object exactly as it would reach a real foreign client.

The ways to be reachable, matching the ones a client has:

  --well-known  own the shared bus name, and write no peer record. This is
                how a launcher finds a client whose record it cannot read.
  --foreign     take a bus name, interface and object path of our own that
                the launcher cannot know, and publish them in the config
                dir's peer record, with the ID of the bus that issued the
                name. This is how a launcher reaches a separately-built
                client that shares no D-Bus name with Transmission, so it
                must honour all three names.
  --old-record  like --foreign, but the record names no bus, the way a
                release from before the bus ID wrote it. A launcher cannot
                tell such a record from a stale one without asking, and must
                ask rather than pass it over: this is its only way to reach
                that release.
  --other-bus   like --foreign, but the record names a bus other than this
                one, as a record from before a reboot does once the bus has
                restarted. A launcher must pass it over without a call: the
                name it holds may be anyone's by now.
  --legacy      own the shared bus name like --well-known, but answer
                neither ConfigDir nor PresentWindowWithToken, the way a
                release older than both methods does. A launcher must treat
                the missing answers as "too old" and hand off anyway.
  --silent      like --old-record, but answer no call at all. This is the
                record a release from before the bus ID leaves behind once the
                bus has restarted and reissued its name to an unrelated
                process. A launcher must give up on it quickly, not wait out
                the default call timeout on every call.

The shared wire names come from the environment, which CMake fills from
interop-names.h, so this stays a client of the same contract the real ones
speak rather than a second spelling of it.

Usage: mock-instance.py <mode> <config-dir-to-answer> <log-file>
The first log line is "ready", written once the object is reachable.
"""

import os
import signal
import sys
from typing import Optional

from gi.repository import Gio, GLib


def wire_name(var: str) -> str:
    """A name the clients agree on, or nothing to be gained by guessing one."""
    value = os.environ.get(var)
    if not value:
        sys.exit(f"{var} is not set; CMake fills it from interop-names.h")
    return value


SERVICE_NAME = wire_name("TR_INTEROP_SERVICE_NAME")
INTERFACE_NAME = wire_name("TR_INTEROP_INTERFACE_NAME")
OBJECT_PATH = wire_name("TR_INTEROP_OBJECT_PATH")
ADD_METAINFO = wire_name("TR_INTEROP_METHOD_ADD_METAINFO")
PRESENT_WINDOW = wire_name("TR_INTEROP_METHOD_PRESENT_WINDOW")
PRESENT_WINDOW_WITH_TOKEN = wire_name("TR_INTEROP_METHOD_PRESENT_WINDOW_WITH_TOKEN")
CONFIG_DIR = wire_name("TR_INTEROP_METHOD_CONFIG_DIR")
PEER_RECORD_FILENAME = wire_name("TR_INTEROP_PEER_RECORD_FILENAME")

# Deliberately unrelated to anything in interop-names.h. A launcher that reaches
# this is reading the record rather than assuming the names it was built with.
FOREIGN_SERVICE_NAME = "org.example.OtherClient"
FOREIGN_INTERFACE_NAME = "org.example.OtherClient"
FOREIGN_OBJECT_PATH = "/org/example/OtherClient"

# Not this bus's ID, whichever that is: what the record of a bus since restarted names.
OTHER_BUS_ID = "0123456789abcdef0123456789abcdef"


def introspection_xml(interface: str, modern: bool) -> str:
    """GDBus rejects calls to methods missing from this, so leaving out ConfigDir and
    PresentWindowWithToken makes the mock answer exactly as a release predating both
    methods does."""
    modern_methods = (
        f"<method name='{CONFIG_DIR}'><arg type='s' direction='out'/></method>"
        f"<method name='{PRESENT_WINDOW_WITH_TOKEN}'>"
        f"<arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
    ) if modern else ""
    return f"""
<node>
  <interface name='{interface}'>
    <method name='{ADD_METAINFO}'>
      <arg type='s' direction='in'/><arg type='b' direction='out'/>
    </method>
    <method name='{PRESENT_WINDOW}'><arg type='b' direction='out'/></method>
    {modern_methods}
  </interface>
</node>
"""


def write_peer_record(config_dir: str, bus_name: str, interface: str, path: str, bus_id: Optional[str]) -> None:
    """The file a client leaves to say where it answers. See dbus-peer-record.h.
    The bus ID is optional there, and None leaves it out."""
    record = f"bus-name={bus_name}\ninterface={interface}\npath={path}\n"
    if bus_id is not None:
        record += f"bus-id={bus_id}\n"
    with open(os.path.join(config_dir, PEER_RECORD_FILENAME), "w", encoding="utf-8") as fp:
        fp.write(record)


def bus_id(conn: Gio.DBusConnection) -> str:
    """org.freedesktop.DBus.GetId: what a client records so a later launch can tell
    whether a recorded name is from the bus it is on."""
    reply = conn.call_sync(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "GetId",
        None,
        GLib.VariantType("(s)"),
        Gio.DBusCallFlags.NONE,
        -1,
        None,
    )
    return reply.unpack()[0]


def main() -> None:
    mode, config_dir, log_path = sys.argv[1], sys.argv[2], sys.argv[3]
    foreign = mode in ("--foreign", "--old-record", "--other-bus", "--silent")
    legacy = mode == "--legacy"
    silent = mode == "--silent"

    service = FOREIGN_SERVICE_NAME if foreign else SERVICE_NAME
    interface = FOREIGN_INTERFACE_NAME if foreign else INTERFACE_NAME
    path = FOREIGN_OBJECT_PATH if foreign else OBJECT_PATH

    def log(line: str) -> None:
        with open(log_path, "a", encoding="utf-8") as fp:
            fp.write(line + "\n")

    # Held and never answered, so a caller waits out its own timeout, as it would
    # on a process that does not dispatch the call.
    held = []

    def on_call(_conn, _sender, _path, _iface, method, params, invocation) -> None:
        if silent:
            held.append(invocation)
        elif method == ADD_METAINFO:
            log(f"{ADD_METAINFO} " + params.unpack()[0])
            invocation.return_value(GLib.Variant("(b)", (True,)))
        elif method == PRESENT_WINDOW:
            log(PRESENT_WINDOW)
            invocation.return_value(GLib.Variant("(b)", (True,)))
        elif method == PRESENT_WINDOW_WITH_TOKEN:
            log(f"{PRESENT_WINDOW_WITH_TOKEN} " + params.unpack()[0])
            invocation.return_value(GLib.Variant("(b)", (True,)))
        elif method == CONFIG_DIR:
            log(CONFIG_DIR)
            invocation.return_value(GLib.Variant("(s)", (config_dir,)))

    node = Gio.DBusNodeInfo.new_for_xml(introspection_xml(interface, modern=not legacy))
    conn = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    conn.register_object(path, node.interfaces[0], on_call, None, None)

    def publish_record() -> None:
        # The record names the unique connection, not the well-known name. That is
        # what a real client records, and what proves the launcher used the file.
        recorded_bus: Optional[str] = None
        if mode == "--foreign":
            recorded_bus = bus_id(conn)
        elif mode == "--other-bus":
            recorded_bus = OTHER_BUS_ID
        write_peer_record(config_dir, conn.get_unique_name(), interface, path, recorded_bus)

    def on_name_acquired(_conn, _name) -> None:
        if foreign:
            publish_record()
        log("ready")

    Gio.bus_own_name_on_connection(conn, service, Gio.BusNameOwnerFlags.NONE, on_name_acquired, None)

    loop = GLib.MainLoop()
    signal.signal(signal.SIGTERM, lambda *_: loop.quit())
    loop.run()


if __name__ == "__main__":
    main()
