// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> /* memcpy(), memmove(), memset() */

#include <arc4.h>

#include "transmission.h"
#include "crypto.h"
#include "crypto-utils.h"
#include "tr-assert.h"
#include "utils.h"

/**
***
**/

static auto constexpr PrimeLen = size_t{ 96 };
static auto constexpr DhPrivkeyLen = size_t{ 20 };

static uint8_t constexpr dh_P[PrimeLen] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, //
    0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, //
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6, //
    0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD, //
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D, //
    0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45, //
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9, //
    0xA6, 0x3A, 0x36, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63, //
};

static uint8_t constexpr dh_G[] = { 2 };

/**
***
**/

static void ensureKeyExists(tr_crypto* crypto)
{
    if (crypto->dh == nullptr)
    {
        size_t public_key_length = 0;
        crypto->dh = tr_dh_new(dh_P, sizeof(dh_P), dh_G, sizeof(dh_G));
        tr_dh_make_key(crypto->dh, DhPrivkeyLen, crypto->myPublicKey, &public_key_length);

        TR_ASSERT(public_key_length == KEY_LEN);
    }
}

tr_crypto::tr_crypto(tr_sha1_digest_t const* torrent_hash_in, bool is_incoming_in)
    : is_incoming{ is_incoming_in }
{
    if (torrent_hash_in != nullptr)
    {
        this->torrent_hash = *torrent_hash_in;
    }
}

tr_crypto::~tr_crypto()
{
    tr_dh_secret_free(this->mySecret);
    tr_dh_free(this->dh);
    tr_free(this->enc_key);
    tr_free(this->dec_key);
}

/**
***
**/

bool tr_cryptoComputeSecret(tr_crypto* crypto, uint8_t const* peerPublicKey)
{
    ensureKeyExists(crypto);
    crypto->mySecret = tr_dh_agree(crypto->dh, peerPublicKey, KEY_LEN);
    return crypto->mySecret != nullptr;
}

uint8_t const* tr_cryptoGetMyPublicKey(tr_crypto const* crypto, int* setme_len)
{
    ensureKeyExists(const_cast<tr_crypto*>(crypto));
    *setme_len = KEY_LEN;
    return crypto->myPublicKey;
}

/**
***
**/

static void init_rc4(tr_crypto const* crypto, struct arc4_context** setme, char const* key)
{
    TR_ASSERT(crypto->torrent_hash);

    if (*setme == nullptr)
    {
        *setme = tr_new0(struct arc4_context, 1);
    }

    auto const buf = crypto->torrent_hash ?
        tr_cryptoSecretKeySha1(crypto, key, 4, std::data(*crypto->torrent_hash), std::size(*crypto->torrent_hash)) :
        std::nullopt;

    if (buf)
    {
        arc4_init(*setme, std::data(*buf), std::size(*buf));
        arc4_discard(*setme, 1024);
    }
}

static void crypt_rc4(struct arc4_context* key, size_t buf_len, void const* buf_in, void* buf_out)
{
    if (key == nullptr)
    {
        if (buf_in != buf_out)
        {
            memmove(buf_out, buf_in, buf_len);
        }

        return;
    }

    arc4_process(key, buf_in, buf_out, buf_len);
}

void tr_cryptoDecryptInit(tr_crypto* crypto)
{
    init_rc4(crypto, &crypto->dec_key, crypto->is_incoming ? "keyA" : "keyB"); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_cryptoDecrypt(tr_crypto* crypto, size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_rc4(crypto->dec_key, buf_len, buf_in, buf_out); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_cryptoEncryptInit(tr_crypto* crypto)
{
    init_rc4(crypto, &crypto->enc_key, crypto->is_incoming ? "keyB" : "keyA"); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_cryptoEncrypt(tr_crypto* crypto, size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_rc4(crypto->enc_key, buf_len, buf_in, buf_out); // lgtm[cpp/weak-cryptographic-algorithm]
}

std::optional<tr_sha1_digest_t> tr_cryptoSecretKeySha1(
    tr_crypto const* crypto,
    void const* prepend_data,
    size_t prepend_data_size,
    void const* append_data,
    size_t append_data_size)
{
    TR_ASSERT(crypto != nullptr);
    TR_ASSERT(crypto->mySecret != nullptr);

    return tr_dh_secret_derive(crypto->mySecret, prepend_data, prepend_data_size, append_data, append_data_size);
}

/**
***
**/

void tr_cryptoSetTorrentHash(tr_crypto* crypto, tr_sha1_digest_t const& hash)
{
    crypto->torrent_hash = hash;
}

std::optional<tr_sha1_digest_t> tr_cryptoGetTorrentHash(tr_crypto const* crypto)
{
    TR_ASSERT(crypto != nullptr);

    return crypto->torrent_hash;
}
