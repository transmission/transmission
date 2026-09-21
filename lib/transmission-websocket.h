// This file was part of the Transmission project.
// See LICENSE for licensing information.
//
// WebSocket tracker client support for WebTorrent compatibility.
// Implements the BitTorrent tracker protocol over WebSocket (ws:// and wss://).

#pragma once

#include <string>
#include <vector>

#include "transmission.h"
#include "tr-macros.h"
#include "tr-websocket.h"

namespace libtransmission
{

class TrackerClient;

/**
 * Creates a WebSocket tracker client if the tracker URL uses ws:// or wss:// scheme.
 * Returns nullptr if the URL is not a WebSocket URL.
 */
TR_CONSTEXPR bool isWebsocketTrackerUrl(std::string_view url)
{
    return tr_strvStartsWith(url, "ws://") || tr_strvStartsWith(url, "wss://");
}

} // namespace libtransmission
