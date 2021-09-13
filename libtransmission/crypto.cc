/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* memcpy(), memmove(), memset() */

#include <arc4.h>

#include "transmission.h"
#include "crypto.h"
#include "crypto-utils.h"
#include "tr-assert.h"
#include "utils.h"

/**
***
**/

#define PRIME_LEN 96
#define DH_PRIVKEY_LEN 20

static uint8_t const dh_P[PRIME_LEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, //
    0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, //
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6, //
    0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD, //
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D, //
    0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45, //
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9, //
    0xA6, 0x3A, 0x36, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63, //
};

static uint8_t const dh_G[] = { 2 };

/**
***
**/

static void ensureKeyExists(tr_crypto* crypto)
{
    if (crypto->dh == NULL)
    {
        size_t public_key_length;

        crypto->dh = tr_dh_new(dh_P, sizeof(dh_P), dh_G, sizeof(dh_G));
        tr_dh_make_key(crypto->dh, DH_PRIVKEY_LEN, crypto->myPublicKey, &public_key_length);

        TR_ASSERT(public_key_length == KEY_LEN);
    }
}

void tr_cryptoConstruct(tr_crypto* crypto, uint8_t const* torrentHash, bool isIncoming)
{
    memset(crypto, 0, sizeof(tr_crypto));

    crypto->isIncoming = isIncoming;
    tr_cryptoSetTorrentHash(crypto, torrentHash);
}

void tr_cryptoDestruct(tr_crypto* crypto)
{
    tr_dh_secret_free(crypto->mySecret);
    tr_dh_free(crypto->dh);
    tr_free(crypto->enc_key);
    tr_free(crypto->dec_key);
}

/**
***
**/

bool tr_cryptoComputeSecret(tr_crypto* crypto, uint8_t const* peerPublicKey)
{
    ensureKeyExists(crypto);
    crypto->mySecret = tr_dh_agree(crypto->dh, peerPublicKey, KEY_LEN);
    return crypto->mySecret != NULL;
}

uint8_t const* tr_cryptoGetMyPublicKey(tr_crypto const* crypto, int* setme_len)
{
    ensureKeyExists((tr_crypto*)crypto);
    *setme_len = KEY_LEN;
    return crypto->myPublicKey;
}

/**
***
**/

static void init_rc4(tr_crypto const* crypto, struct arc4_context** setme, char const* key)
{
    TR_ASSERT(crypto->torrentHashIsSet);

    if (*setme == NULL)
    {
        *setme = tr_new0(struct arc4_context, 1);
    }

    uint8_t buf[SHA_DIGEST_LENGTH];

    if (tr_cryptoSecretKeySha1(crypto, key, 4, crypto->torrentHash, SHA_DIGEST_LENGTH, buf))
    {
        arc4_init(*setme, buf, SHA_DIGEST_LENGTH);
        arc4_discard(*setme, 1024);
    }
}

static void crypt_rc4(struct arc4_context* key, size_t buf_len, void const* buf_in, void* buf_out)
{
    if (key == NULL)
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
    init_rc4(crypto, &crypto->dec_key, crypto->isIncoming ? "keyA" : "keyB"); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_cryptoDecrypt(tr_crypto* crypto, size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_rc4(crypto->dec_key, buf_len, buf_in, buf_out); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_cryptoEncryptInit(tr_crypto* crypto)
{
    init_rc4(crypto, &crypto->enc_key, crypto->isIncoming ? "keyB" : "keyA"); // lgtm[cpp/weak-cryptographic-algorithm]
}

void tr_cryptoEncrypt(tr_crypto* crypto, size_t buf_len, void const* buf_in, void* buf_out)
{
    crypt_rc4(crypto->enc_key, buf_len, buf_in, buf_out); // lgtm[cpp/weak-cryptographic-algorithm]
}

bool tr_cryptoSecretKeySha1(
    tr_crypto const* crypto,
    void const* prepend_data,
    size_t prepend_data_size,
    void const* append_data,
    size_t append_data_size,
    uint8_t* hash)
{
    TR_ASSERT(crypto != NULL);
    TR_ASSERT(crypto->mySecret != NULL);

    return tr_dh_secret_derive(crypto->mySecret, prepend_data, prepend_data_size, append_data, append_data_size, hash);
}

/**
***
**/

void tr_cryptoSetTorrentHash(tr_crypto* crypto, uint8_t const* hash)
{
    crypto->torrentHashIsSet = hash != NULL;

    if (hash != NULL)
    {
        memcpy(crypto->torrentHash, hash, SHA_DIGEST_LENGTH);
    }
    else
    {
        memset(crypto->torrentHash, 0, SHA_DIGEST_LENGTH);
    }
}

uint8_t const* tr_cryptoGetTorrentHash(tr_crypto const* crypto)
{
    TR_ASSERT(crypto != NULL);

    return crypto->torrentHashIsSet ? crypto->torrentHash : NULL;
}

bool tr_cryptoHasTorrentHash(tr_crypto const* crypto)
{
    TR_ASSERT(crypto != NULL);

    return crypto->torrentHashIsSet;
}
