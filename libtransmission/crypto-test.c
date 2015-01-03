/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <string.h>

#include "transmission.h"
#include "crypto.h"
#include "crypto-utils.h"

#include "libtransmission-test.h"

#include "crypto-test-ref.h"

static int
test_torrent_hash (void)
{
  tr_crypto a;
  uint8_t hash[SHA_DIGEST_LENGTH];
  int i;

  for (i = 0; i < SHA_DIGEST_LENGTH; ++i)
    hash[i] = i;

  tr_cryptoConstruct (&a, NULL, true);

  check (!tr_cryptoHasTorrentHash (&a));
  check (tr_cryptoGetTorrentHash (&a) == NULL);

  tr_cryptoSetTorrentHash (&a, hash);
  check (tr_cryptoHasTorrentHash (&a));
  check (tr_cryptoGetTorrentHash (&a) != NULL);
  check (memcmp (tr_cryptoGetTorrentHash (&a), hash, SHA_DIGEST_LENGTH) == 0);

  tr_cryptoDestruct (&a);

  for (i = 0; i < SHA_DIGEST_LENGTH; ++i)
    hash[i] = i + 1;

  tr_cryptoConstruct (&a, hash, false);

  check (tr_cryptoHasTorrentHash (&a));
  check (tr_cryptoGetTorrentHash (&a) != NULL);
  check (memcmp (tr_cryptoGetTorrentHash (&a), hash, SHA_DIGEST_LENGTH) == 0);

  tr_cryptoSetTorrentHash (&a, NULL);
  check (!tr_cryptoHasTorrentHash (&a));
  check (tr_cryptoGetTorrentHash (&a) == NULL);

  tr_cryptoDestruct (&a);

  return 0;
}

static int
test_encrypt_decrypt (void)
{
  tr_crypto a;
  tr_crypto_ b;
  uint8_t hash[SHA_DIGEST_LENGTH];
  const char test1[] = { "test1" };
  char buf11[sizeof (test1)], buf12[sizeof (test1)];
  const char test2[] = { "@#)C$@)#(*%bvkdjfhwbc039bc4603756VB3)" };
  char buf21[sizeof (test2)], buf22[sizeof (test2)];
  int i;

  for (i = 0; i < SHA_DIGEST_LENGTH; ++i)
    hash[i] = i;

  tr_cryptoConstruct (&a, hash, false);
  tr_cryptoConstruct_ (&b, hash, true);
  check (tr_cryptoComputeSecret (&a, tr_cryptoGetMyPublicKey_ (&b, &i)));
  check (tr_cryptoComputeSecret_ (&b, tr_cryptoGetMyPublicKey (&a, &i)));

  tr_cryptoEncryptInit (&a);
  tr_cryptoEncrypt (&a, sizeof (test1), test1, buf11);
  tr_cryptoDecryptInit_ (&b);
  tr_cryptoDecrypt_ (&b, sizeof (test1), buf11, buf12);
  check_streq (test1, buf12);

  tr_cryptoEncryptInit_ (&b);
  tr_cryptoEncrypt_ (&b, sizeof (test2), test2, buf21);
  tr_cryptoDecryptInit (&a);
  tr_cryptoDecrypt (&a, sizeof (test2), buf21, buf22);
  check_streq (test2, buf22);

  tr_cryptoDestruct_ (&b);
  tr_cryptoDestruct (&a);

  return 0;
}

static int
test_sha1 (void)
{
  uint8_t hash[SHA_DIGEST_LENGTH];
  uint8_t hash_[SHA_DIGEST_LENGTH];

  check (tr_sha1 (hash, "test", 4, NULL));
  check (tr_sha1_ (hash_, "test", 4, NULL));
  check (memcmp (hash, "\xa9\x4a\x8f\xe5\xcc\xb1\x9b\xa6\x1c\x4c\x08\x73\xd3\x91\xe9\x87\x98\x2f\xbb\xd3", SHA_DIGEST_LENGTH) == 0);
  check (memcmp (hash, hash_, SHA_DIGEST_LENGTH) == 0);

  check (tr_sha1 (hash, "1", 1, "22", 2, "333", 3, NULL));
  check (tr_sha1_ (hash_, "1", 1, "22", 2, "333", 3, NULL));
  check (memcmp (hash, "\x1f\x74\x64\x8e\x50\xa6\xa6\x70\x8e\xc5\x4a\xb3\x27\xa1\x63\xd5\x53\x6b\x7c\xed", SHA_DIGEST_LENGTH) == 0);
  check (memcmp (hash, hash_, SHA_DIGEST_LENGTH) == 0);

  return 0;
}

static int
test_ssha1 (void)
{
  const char * const test_data[] =
    {
      "test",
      "QNY)(*#$B)!_X$B !_B#($^!)*&$%CV!#)&$C!@$(P*)"
    };

  size_t i;

#define HASH_COUNT (16 * 1024)

  for (i = 0; i < sizeof (test_data) / sizeof (*test_data); ++i)
    {
      char * const phrase = tr_strdup (test_data[i]);
      char ** hashes = tr_new (char *, HASH_COUNT);
      size_t j;

      for (j = 0; j < HASH_COUNT; ++j)
        {
          hashes[j] = j % 2 == 0 ? tr_ssha1 (phrase) : tr_ssha1_ (phrase);

          check (hashes[j] != NULL);

          /* phrase matches each of generated hashes */
          check (tr_ssha1_matches (hashes[j], phrase));
          check (tr_ssha1_matches_ (hashes[j], phrase));
        }

      for (j = 0; j < HASH_COUNT; ++j)
        {
          size_t k;

          /* all hashes are different */
          for (k = 0; k < HASH_COUNT; ++k)
            check (k == j || strcmp (hashes[j], hashes[k]) != 0);
        }

      /* exchange two first chars */
      phrase[0] ^= phrase[1];
      phrase[1] ^= phrase[0];
      phrase[0] ^= phrase[1];

      for (j = 0; j < HASH_COUNT; ++j)
        {
          /* changed phrase doesn't match the hashes */
          check (!tr_ssha1_matches (hashes[j], phrase));
          check (!tr_ssha1_matches_ (hashes[j], phrase));
        }

      for (j = 0; j < HASH_COUNT; ++j)
        tr_free (hashes[j]);

      tr_free (hashes);
      tr_free (phrase);
    }

#undef HASH_COUNT

  return 0;
}

static int
test_random (void)
{
  int i;

  /* test that tr_rand_int () stays in-bounds */
  for (i = 0; i < 100000; ++i)
    {
      const int val = tr_rand_int (100);
      check (val >= 0);
      check (val < 100);
    }

  return 0;
}

static bool
base64_eq (const char * a,
           const char * b)
{
  for (; ; ++a, ++b)
    {
      while (*a == '\r' || *a == '\n')
        ++a;
      while (*b == '\r' || *b == '\n')
        ++b;
      if (*a == '\0' || *b == '\0' || *a != *b)
        break;
    }

  return *a == *b;
}

static int
test_base64 (void)
{
  size_t len;
  char * in, * out;
  int i;

  out = tr_base64_encode_str ("YOYO!", &len);
  check_int_eq (strlen (out), len);
  check (base64_eq ("WU9ZTyE=", out));
  in = tr_base64_decode_str (out, &len);
  check_int_eq (5, len);
  check_streq ("YOYO!", in);
  tr_free (in);
  tr_free (out);

  out = tr_base64_encode ("", 0, &len);
  check_int_eq (0, len);
  check_streq ("", out);
  tr_free (out);
  out = tr_base64_decode ("", 0, &len);
  check_int_eq (0, len);
  check_streq ("", out);
  tr_free (out);

  out = tr_base64_encode (NULL, 0, &len);
  check_int_eq (0, len);
  check (out == NULL);
  out = tr_base64_decode (NULL, 0, &len);
  check_int_eq (0, len);
  check (out == NULL);

#define MAX_BUF_SIZE 1024

  for (i = 1; i <= MAX_BUF_SIZE; ++i)
    {
      int j;
      char buf[MAX_BUF_SIZE + 1];

      for (j = 0; j < i; ++j)
        buf[j] = tr_rand_int_weak (256);

      out = tr_base64_encode (buf, j, &len);
      check_int_eq (strlen (out), len);
      in = tr_base64_decode (out, len, &len);
      check_int_eq (j, len);
      check (memcmp (in, buf, len) == 0);
      tr_free (in);
      tr_free (out);

      for (j = 0; j < i; ++j)
        buf[j] = 1 + tr_rand_int_weak (255);
      buf[j] = '\0';

      out = tr_base64_encode_str (buf, &len);
      check_int_eq (strlen (out), len);
      in = tr_base64_decode_str (out, &len);
      check_int_eq (j, len);
      check_streq (in, buf);
      tr_free (in);
      tr_free (out);
    }

#undef MAX_BUF_SIZE

  return 0;
}

int
main (void)
{
  const testFunc tests[] = { test_torrent_hash,
                             test_encrypt_decrypt,
                             test_sha1,
                             test_ssha1,
                             test_random,
                             test_base64 };

  return runTests (tests, NUM_TESTS (tests));
}
