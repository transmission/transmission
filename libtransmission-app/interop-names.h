// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

// The names by which one Transmission instance reaches another.
// Other projects build clients that use these too, so treat them as a wire contract.
// Change a value here and those clients can no longer find us.
//
// Four more shared names live in `libtransmission/transmission.h`, because
// `transmission-cli` and `transmission-daemon` need them and do not link
// libtransmission-app:
//
//   TR_CONFIG_DIR_NAME            the default config dir the clients share
//   TR_RPC_SESSION_ID_HEADER      the CSRF header a third-party RPC client sends
//   TR_RPC_RPC_VERSION_HEADER     the version header it reads back
//   TrHttpServerDefaultBasePath   with TrHttpServerRpcRelativePath, the RPC URL
//
// The config dir lock filename lives in `libtransmission/config-dir-lock.cc` for the
// same reason. Those three files hold every shared name we have.

// Macros, because Q_CLASSINFO and QStringLiteral need a literal token, not a constexpr value.
// We spell the interface out instead of defining it from the service name.
// A fork that renames the service can then decide whether to rename the interface too.
#define TR_INTEROP_DBUS_SERVICE_NAME "com.transmissionbt.Transmission"
#define TR_INTEROP_DBUS_INTERFACE_NAME "com.transmissionbt.Transmission"
#define TR_INTEROP_DBUS_OBJECT_PATH "/com/transmissionbt/Transmission"

// The class id a running Qt client registers on Windows.
// transmission-qt.idl and dist/msi/components/QtClient.wxs spell it out too,
// so keep all three in sync.
#define TR_INTEROP_COM_CLASS_ID "{0e2c952c-0597-491f-ba26-249d7e6fab49}"

#define TR_INTEROP_METHOD_ADD_METAINFO "AddMetainfo"
#define TR_INTEROP_METHOD_PRESENT_WINDOW "PresentWindow"
#define TR_INTEROP_METHOD_PRESENT_WINDOW_WITH_TOKEN "PresentWindowWithToken"
#define TR_INTEROP_METHOD_CONFIG_DIR "ConfigDir"

namespace tr::interop
{

inline constexpr std::string_view DBusIntrospectionXml =
    "<node>"
    "<interface name='" TR_INTEROP_DBUS_INTERFACE_NAME
    "'>"
    "<method name='" TR_INTEROP_METHOD_ADD_METAINFO
    "'><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
    "<method name='" TR_INTEROP_METHOD_PRESENT_WINDOW
    "'><arg type='b' direction='out'/></method>"
    "<method name='" TR_INTEROP_METHOD_PRESENT_WINDOW_WITH_TOKEN
    "'><arg type='s' direction='in'/><arg type='b' direction='out'/></method>"
    "<method name='" TR_INTEROP_METHOD_CONFIG_DIR
    "'><arg type='s' direction='out'/></method>"
    "</interface>"
    "</node>";

// A Windows client registers an OLE item moniker, delimiter "!".
// Its item is this prefix followed by the canonical config dir, defined below,
// so each config dir gets its own entry in the running object table.
inline constexpr std::string_view ComConfigMonikerPrefix = "Transmission.ConfigDir:";

// Names and signatures are both part of the contract. In D-Bus notation:
//   AddMetainfo(s) -> b   PresentWindow() -> b
//   PresentWindowWithToken(s) -> b   ConfigDir() -> s
//
// - AddMetainfo's string carries a URL or magnet link as itself,
//   or a torrent file's contents base64'd.
// - PresentWindow is called by third-party scripts, so its name cannot change.
// - PresentWindowWithToken's string carries the caller's activation token, the pass a
//   desktop hands a launch so that focus may follow it to another window. A caller with
//   a token offers this method first and falls back to PresentWindow when it goes
//   unanswered, so a client without the method still presents, just without focus.
// - ConfigDir must answer with the canonical config dir, defined below.
inline constexpr std::string_view MethodAddMetainfo = TR_INTEROP_METHOD_ADD_METAINFO;
inline constexpr std::string_view MethodPresentWindow = TR_INTEROP_METHOD_PRESENT_WINDOW;
inline constexpr std::string_view MethodPresentWindowWithToken = TR_INTEROP_METHOD_PRESENT_WINDOW_WITH_TOKEN;
inline constexpr std::string_view MethodConfigDir = TR_INTEROP_METHOD_CONFIG_DIR;

// The canonical config dir
//
// Every client has to normalize a config dir the same way. Two that normalize differently
// disagree about whether they are serving the same dir, and the answer you get depends on
// which one launched second.
//
// The canonical form of a config dir is the path the OS reports for the directory itself:
//   - POSIX:   realpath(3). Absolute, every symlink resolved, no trailing slash.
//   - Windows: GetFinalPathNameByHandleW() with FILE_NAME_NORMALIZED | VOLUME_NAME_DOS on
//              a handle to the dir. Remove the `\\?\` prefix and rewrite `\\?\UNC\` to
//              `\\`. This is the on-disk spelling, so the filesystem decides its case,
//              not the caller.
//
// We cannot resolve a dir that does not exist yet. It canonicalizes to itself made
// absolute against the working directory, and no further.
//
// `tr::interop::canonical_config_dir()` implements this rule. Nothing else should.
//
// Windows needs the rule more than D-Bus does:
//   - D-Bus: we canonicalize our own dir and the ConfigDir() reply before comparing them,
//     so a peer that spells its path differently still matches if the two resolve alike.
//   - COM: we concatenate the canonical dir into the moniker item above and match it as an
//     exact string. Nothing re-canonicalizes it, so a client that spells it differently is
//     never found.

// A running session stores its D-Bus info in `${config_dir}/dbus-peer`.
// See `dbus-peer-record.h` for more info about that file and its use.
inline constexpr std::string_view PeerRecordFilename = "dbus-peer";

// A launch holds this from the moment it gives up on delegating until it can itself be
// delegated to, so two launches racing on one config dir cannot both start.
// Shared like the rest. A client that names it differently will not serialize against us.
inline constexpr std::string_view StartupLockFilename = "lock.startup";

} // namespace tr::interop
