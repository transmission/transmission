// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "tr/torrent/types.h"

inline auto constexpr TrInet6AddrStrlen = 46U;

inline auto constexpr TrAddrStrlen = 64U;

inline auto constexpr TrDefaultBlocklistFilename = std::string_view{ "blocklist.bin" };
inline auto constexpr TrDefaultHttpServerBasePath = std::string_view{ "/transmission/" };
inline auto constexpr TrDefaultPeerLimitGlobal = 200U;
inline auto constexpr TrDefaultPeerLimitTorrent = 50U;
inline auto constexpr TrDefaultPeerPort = 51413U;
inline auto constexpr TrDefaultPeerSocketTos = std::string_view{ "le" };
inline auto constexpr TrDefaultRpcPort = 9091U;
inline auto constexpr TrDefaultRpcWhitelist = std::string_view{ "127.0.0.1,::1" };

inline auto constexpr TrHttpServerRpcRelativePath = std::string_view{ "rpc" };
inline auto constexpr TrHttpServerWebRelativePath = std::string_view{ "web/" };
inline auto constexpr TrRpcSessionIdHeader = std::string_view{ "X-Transmission-Session-Id" };
inline auto constexpr TrRpcVersionHeader = std::string_view{ "X-Transmission-Rpc-Version" };
