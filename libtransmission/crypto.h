/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

// NB: crypto-test-ref.h needs this, so use it instead of #pragma once
#ifndef TR_ENCRYPTION_H
#define TR_ENCRYPTION_H

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cinttypes> // uintX_t
#include <cstddef>
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
    std::optional<tr_sha1_digest_t> torrent_hash;
    struct arc4_context* dec_key;
    struct arc4_context* enc_key;
    tr_dh_ctx_t dh;
    uint8_t myPublicKey[KEY_LEN];
    tr_dh_secret_t mySecret;
    bool isIncoming;
};

/** @brief construct a new tr_crypto object */
void tr_cryptoConstruct(tr_crypto* crypto, tr_sha1_digest_t const* torrent_hash, bool isIncoming);

/** @brief destruct an existing tr_crypto object */
void tr_cryptoDestruct(tr_crypto* crypto);

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
