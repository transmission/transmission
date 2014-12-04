/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <stdarg.h>
#include <string.h> /* memcpy (), memset (), strcmp () */

#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/err.h>
#include <openssl/rc4.h>

#include "transmission.h"
#include "crypto.h"
#include "crypto-utils.h"
#include "log.h"
#include "utils.h"

#define MY_NAME "tr_crypto"

/**
***
**/

#define KEY_LEN 96

#define PRIME_LEN 96

#define DH_PRIVKEY_LEN_MIN 16
#define DH_PRIVKEY_LEN 20

static const uint8_t dh_P[PRIME_LEN] =
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2,
  0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
  0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6,
  0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
  0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D,
  0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
  0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9,
  0xA6, 0x3A, 0x36, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63,
};

static const uint8_t dh_G[] = { 2 };

/**
***
**/

#define logErrorFromSSL(...) \
  do { \
    if (tr_logLevelIsActive (TR_LOG_ERROR)) { \
      char buf[512]; \
      ERR_error_string_n (ERR_get_error (), buf, sizeof (buf)); \
      tr_logAddMessage (__FILE__, __LINE__, TR_LOG_ERROR, MY_NAME, "%s", buf); \
    } \
  } while (0)

static void
ensureKeyExists (tr_crypto * crypto)
{
  if (crypto->dh == NULL)
    {
      int len, offset;
      DH * dh = DH_new ();

      dh->p = BN_bin2bn (dh_P, sizeof (dh_P), NULL);
      if (dh->p == NULL)
        logErrorFromSSL ();

      dh->g = BN_bin2bn (dh_G, sizeof (dh_G), NULL);
      if (dh->g == NULL)
        logErrorFromSSL ();

      /* private DH value: strong random BN of DH_PRIVKEY_LEN*8 bits */
      dh->priv_key = BN_new ();
      do
        {
          if (BN_rand (dh->priv_key, DH_PRIVKEY_LEN * 8, -1, 0) != 1)
            logErrorFromSSL ();
        }
      while (BN_num_bits (dh->priv_key) < DH_PRIVKEY_LEN_MIN * 8);

      if (!DH_generate_key (dh))
        logErrorFromSSL ();

      /* DH can generate key sizes that are smaller than the size of
         P with exponentially decreasing probability, in which case
         the msb's of myPublicKey need to be zeroed appropriately. */
      len = BN_num_bytes (dh->pub_key);
      offset = KEY_LEN - len;
      assert (len <= KEY_LEN);
      memset (crypto->myPublicKey, 0, offset);
      BN_bn2bin (dh->pub_key, crypto->myPublicKey + offset);

      crypto->dh = dh;
    }
}

void
tr_cryptoConstruct (tr_crypto * crypto, const uint8_t * torrentHash, bool isIncoming)
{
  memset (crypto, 0, sizeof (tr_crypto));

  crypto->dh = NULL;
  crypto->isIncoming = isIncoming;
  tr_cryptoSetTorrentHash (crypto, torrentHash);
}

void
tr_cryptoDestruct (tr_crypto * crypto)
{
  if (crypto->dh != NULL)
    DH_free (crypto->dh);
}

/**
***
**/

const uint8_t*
tr_cryptoComputeSecret (tr_crypto *     crypto,
                        const uint8_t * peerPublicKey)
{
  DH * dh;
  int len;
  uint8_t secret[KEY_LEN];
  BIGNUM * bn = BN_bin2bn (peerPublicKey, KEY_LEN, NULL);

  ensureKeyExists (crypto);
  dh = crypto->dh;

  assert (DH_size (dh) == KEY_LEN);

  len = DH_compute_key (secret, bn, dh);
  if (len == -1)
    {
      logErrorFromSSL ();
    }
  else
    {
      int offset;
      assert (len <= KEY_LEN);
      offset = KEY_LEN - len;
      memset (crypto->mySecret, 0, offset);
      memcpy (crypto->mySecret + offset, secret, len);
      crypto->mySecretIsSet = true;
    }

  BN_free (bn);
  return crypto->mySecret;
}

const uint8_t*
tr_cryptoGetMyPublicKey (const tr_crypto * crypto,
                         int             * setme_len)
{
  ensureKeyExists ((tr_crypto *) crypto);
  *setme_len = KEY_LEN;
  return crypto->myPublicKey;
}

/**
***
**/

static void
initRC4 (tr_crypto  * crypto,
         RC4_KEY    * setme,
         const char * key)
{
  uint8_t buf[SHA_DIGEST_LENGTH];

  assert (crypto->torrentHashIsSet);
  assert (crypto->mySecretIsSet);

  if (tr_sha1 (buf,
               key, 4,
               crypto->mySecret, KEY_LEN,
               crypto->torrentHash, SHA_DIGEST_LENGTH,
               NULL))
    RC4_set_key (setme, SHA_DIGEST_LENGTH, buf);
}

void
tr_cryptoDecryptInit (tr_crypto * crypto)
{
  unsigned char discard[1024];
  const char * txt = crypto->isIncoming ? "keyA" : "keyB";

  initRC4 (crypto, &crypto->dec_key, txt);
  RC4 (&crypto->dec_key, sizeof (discard), discard, discard);
}

void
tr_cryptoDecrypt (tr_crypto  * crypto,
                  size_t       buf_len,
                  const void * buf_in,
                  void       * buf_out)
{
  RC4 (&crypto->dec_key, buf_len,
       (const unsigned char*)buf_in,
       (unsigned char*)buf_out);
}

void
tr_cryptoEncryptInit (tr_crypto * crypto)
{
  unsigned char discard[1024];
  const char * txt = crypto->isIncoming ? "keyB" : "keyA";

  initRC4 (crypto, &crypto->enc_key, txt);
  RC4 (&crypto->enc_key, sizeof (discard), discard, discard);
}

void
tr_cryptoEncrypt (tr_crypto  * crypto,
                  size_t       buf_len,
                  const void * buf_in,
                  void       * buf_out)
{
  RC4 (&crypto->enc_key, buf_len,
       (const unsigned char*)buf_in,
       (unsigned char*)buf_out);
}

/**
***
**/

void
tr_cryptoSetTorrentHash (tr_crypto     * crypto,
                         const uint8_t * hash)
{
  crypto->torrentHashIsSet = hash != NULL;

  if (hash)
    memcpy (crypto->torrentHash, hash, SHA_DIGEST_LENGTH);
  else
    memset (crypto->torrentHash, 0, SHA_DIGEST_LENGTH);
}

const uint8_t*
tr_cryptoGetTorrentHash (const tr_crypto * crypto)
{
  assert (crypto);

  return crypto->torrentHashIsSet ? crypto->torrentHash : NULL;
}

bool
tr_cryptoHasTorrentHash (const tr_crypto * crypto)
{
  assert (crypto);

  return crypto->torrentHashIsSet;
}

/***
****
***/

char*
tr_ssha1 (const void * plaintext)
{
  enum { saltval_len = 8,
         salter_len  = 64 };
  static const char * salter = "0123456789"
                               "abcdefghijklmnopqrstuvwxyz"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "./";

  size_t i;
  unsigned char salt[saltval_len];
  uint8_t sha[SHA_DIGEST_LENGTH];
  char buf[2*SHA_DIGEST_LENGTH + saltval_len + 2];

  tr_rand_buffer (salt, saltval_len);
  for (i=0; i<saltval_len; ++i)
    salt[i] = salter[ salt[i] % salter_len ];

  tr_sha1 (sha, plaintext, strlen (plaintext), salt, saltval_len, NULL);
  tr_sha1_to_hex (&buf[1], sha);
  memcpy (&buf[1+2*SHA_DIGEST_LENGTH], &salt, saltval_len);
  buf[1+2*SHA_DIGEST_LENGTH + saltval_len] = '\0';
  buf[0] = '{'; /* signal that this is a hash. this makes saving/restoring easier */

  return tr_strdup (&buf);
}

bool
tr_ssha1_matches (const char * source, const char * pass)
{
  char * salt;
  size_t saltlen;
  char * hashed;
  uint8_t buf[SHA_DIGEST_LENGTH];
  bool result;
  const size_t sourcelen = strlen (source);

  /* extract the salt */
  if (sourcelen < 2*SHA_DIGEST_LENGTH-1)
    return false;
  saltlen = sourcelen - 2*SHA_DIGEST_LENGTH-1;
  salt = tr_malloc (saltlen);
  memcpy (salt, source + 2*SHA_DIGEST_LENGTH+1, saltlen);

  /* hash pass + salt */
  hashed = tr_malloc (2*SHA_DIGEST_LENGTH + saltlen + 2);
  tr_sha1 (buf, pass, strlen (pass), salt, saltlen, NULL);
  tr_sha1_to_hex (&hashed[1], buf);
  memcpy (hashed + 1+2*SHA_DIGEST_LENGTH, salt, saltlen);
  hashed[1+2*SHA_DIGEST_LENGTH + saltlen] = '\0';
  hashed[0] = '{';

  result = strcmp (source, hashed) == 0 ? true : false;

  tr_free (hashed);
  tr_free (salt);

  return result;
}
