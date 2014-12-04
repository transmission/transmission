/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef TR_ENCRYPTION_H
#define TR_ENCRYPTION_H

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <inttypes.h>

#include "utils.h" /* TR_GNUC_NULL_TERMINATED */

/**
*** @addtogroup peers
*** @{
**/

#include <openssl/dh.h> /* RC4_KEY */
#include <openssl/rc4.h> /* DH */

enum
{
    KEY_LEN = 96
};

/** @brief Holds state information for encrypted peer communications */
typedef struct
{
    RC4_KEY         dec_key;
    RC4_KEY         enc_key;
    DH *            dh;
    uint8_t         myPublicKey[KEY_LEN];
    uint8_t         mySecret[KEY_LEN];
    uint8_t         torrentHash[SHA_DIGEST_LENGTH];
    bool            isIncoming;
    bool            torrentHashIsSet;
    bool            mySecretIsSet;
}
tr_crypto;

/** @brief construct a new tr_crypto object */
void tr_cryptoConstruct (tr_crypto * crypto, const uint8_t * torrentHash, bool isIncoming);

/** @brief destruct an existing tr_crypto object */
void tr_cryptoDestruct (tr_crypto * crypto);


void tr_cryptoSetTorrentHash (tr_crypto * crypto, const uint8_t * torrentHash);

const uint8_t* tr_cryptoGetTorrentHash (const tr_crypto * crypto);

bool           tr_cryptoHasTorrentHash (const tr_crypto * crypto);

const uint8_t* tr_cryptoComputeSecret (tr_crypto *     crypto,
                                       const uint8_t * peerPublicKey);

const uint8_t* tr_cryptoGetMyPublicKey (const tr_crypto * crypto,
                                        int *             setme_len);

void           tr_cryptoDecryptInit (tr_crypto * crypto);

void           tr_cryptoDecrypt (tr_crypto *  crypto,
                                 size_t       buflen,
                                 const void * buf_in,
                                 void *       buf_out);

void           tr_cryptoEncryptInit (tr_crypto * crypto);

void           tr_cryptoEncrypt (tr_crypto *  crypto,
                                 size_t       buflen,
                                 const void * buf_in,
                                 void *       buf_out);

/* @} */

/**
*** @addtogroup utils Utilities
*** @{
**/

/** @brief generate a SSHA password from its plaintext source */
char*  tr_ssha1 (const void * plaintext);

/** @brief Validate a test password against the a ssha1 password */
bool tr_ssha1_matches (const char * ssha1, const char * pass);

/* @} */

#endif
