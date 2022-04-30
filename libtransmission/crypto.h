// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// NB: crypto-test-ref.h needs this, so use it instead of #pragma once
#ifndef TR_ENCRYPTION_H
#define TR_ENCRYPTION_H

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <optional>

#include "crypto-utils.h"
#include "tr-macros.h"

/**
*** @addtogroup peers
*** @{
**/

enum
{
    KEY_LEN = 96
};

/** @brief Holds state information for encrypted peer communications */
struct tr_crypto
{
    tr_crypto(tr_sha1_digest_t const* torrent_hash = nullptr, bool is_incoming = true);
    ~tr_crypto();

    std::optional<tr_sha1_digest_t> torrent_hash = {};
    struct arc4_context* dec_key = nullptr;
    struct arc4_context* enc_key = nullptr;
    tr_dh_ctx_t dh = {};
    uint8_t myPublicKey[KEY_LEN] = {};
    tr_dh_secret_t mySecret = {};
    bool is_incoming = false;
};

void tr_cryptoSetTorrentHash(tr_crypto* crypto, tr_sha1_digest_t const& torrent_hash);

std::optional<tr_sha1_digest_t> tr_cryptoGetTorrentHash(tr_crypto const* crypto);

bool tr_cryptoComputeSecret(tr_crypto* crypto, uint8_t const* peerPublicKey);

uint8_t const* tr_cryptoGetMyPublicKey(tr_crypto const* crypto, int* setme_len);

void tr_cryptoDecryptInit(tr_crypto* crypto);

void tr_cryptoDecrypt(tr_crypto* crypto, size_t buflen, void const* buf_in, void* buf_out);

void tr_cryptoEncryptInit(tr_crypto* crypto);

void tr_cryptoEncrypt(tr_crypto* crypto, size_t buflen, void const* buf_in, void* buf_out);

std::optional<tr_sha1_digest_t> tr_cryptoSecretKeySha1(
    tr_crypto const* crypto,
    void const* prepend_data,
    size_t prepend_data_size,
    void const* append_data,
    size_t append_data_size);

/* @} */

#endif // TR_ENCRYPTION_H
