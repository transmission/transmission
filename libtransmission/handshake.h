/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_HANDSHAKE_H
#define TR_HANDSHAKE_H

#include "transmission.h"
#include "net.h"

/** @addtogroup peers Peers
    @{ */

struct tr_peerIo;

/** @brief opaque struct holding hanshake state information.
           freed when the handshake is completed. */
typedef struct tr_handshake tr_handshake;

/* returns true on success, false on error */
typedef bool (*handshakeDoneCB)(struct tr_handshake * handshake,
                                   struct tr_peerIo    * io,
                                   bool                  readAnythingFromPeer,
                                   bool                  isConnected,
                                   const uint8_t       * peerId,
                                   void                * userData);

/** @brief instantiate a new handshake */
tr_handshake *         tr_handshakeNew (struct tr_peerIo * io,
                                        tr_encryption_mode encryptionMode,
                                        handshakeDoneCB    doneCB,
                                        void *             doneUserData);

const tr_address *     tr_handshakeGetAddr (const struct tr_handshake  * handshake,
                                            tr_port                    * port);

void                   tr_handshakeAbort (tr_handshake * handshake);

struct tr_peerIo*      tr_handshakeGetIO (tr_handshake * handshake);

struct tr_peerIo*      tr_handshakeStealIO (tr_handshake * handshake);


/** @} */
#endif
