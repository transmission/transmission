/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "transmission.h"
#include "net.h"

/** @addtogroup peers Peers
    @{ */

struct tr_peerIo;

/** @brief opaque struct holding hanshake state information.
           freed when the handshake is completed. */
typedef struct tr_handshake tr_handshake;

/* returns true on success, false on error */
typedef bool (* handshakeDoneCB)(struct tr_handshake* handshake, struct tr_peerIo* io, bool readAnythingFromPeer,
    bool isConnected, uint8_t const* peerId, void* userData);

/** @brief instantiate a new handshake */
tr_handshake* tr_handshakeNew(struct tr_peerIo* io, tr_encryption_mode encryptionMode, handshakeDoneCB doneCB,
    void* doneUserData);

tr_address const* tr_handshakeGetAddr(struct tr_handshake const* handshake, tr_port* port);

void tr_handshakeAbort(tr_handshake* handshake);

struct tr_peerIo* tr_handshakeStealIO(tr_handshake* handshake);

/** @} */
