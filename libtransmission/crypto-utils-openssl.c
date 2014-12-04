/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "log.h"
#include "utils.h"

/***
****
***/

#define MY_NAME "tr_crypto_utils"

static void
log_openssl_error (const char * file,
                   int          line)
{
  const unsigned long error_code = ERR_get_error ();

  if (tr_logLevelIsActive (TR_LOG_ERROR))
    {
      char buf[512];

#ifndef TR_LIGHTWEIGHT
      static bool strings_loaded = false;
      if (!strings_loaded)
        {
          ERR_load_crypto_strings ();
          strings_loaded = true;
        }
#endif

      ERR_error_string_n (error_code, buf, sizeof (buf));
      tr_logAddMessage (file, line, TR_LOG_ERROR, MY_NAME, "OpenSSL error: %s", buf);
    }
}

#define log_error() log_openssl_error(__FILE__, __LINE__)

static bool
check_openssl_result (int          result,
                      int          expected_result,
                      bool         expected_equal,
                      const char * file,
                      int          line)
{
  const bool ret = (result == expected_result) == expected_equal;
  if (!ret)
    log_openssl_error (file, line);
  return ret;
}

#define check_result(result) check_openssl_result ((result), 1, true, __FILE__, __LINE__)
#define check_result_eq(result, x_result) check_openssl_result ((result), (x_result), true, __FILE__, __LINE__)
#define check_result_neq(result, x_result) check_openssl_result ((result), (x_result), false, __FILE__, __LINE__)

static bool
check_openssl_pointer (void       * pointer,
                       const char * file,
                       int          line)
{
  const bool ret = pointer != NULL;
  if (!ret)
    log_openssl_error (file, line);
  return ret;
}

#define check_pointer(pointer) check_openssl_pointer ((pointer), __FILE__, __LINE__)

/***
****
***/

tr_sha1_ctx_t
tr_sha1_init (void)
{
  EVP_MD_CTX * handle = EVP_MD_CTX_create ();

  if (check_result (EVP_DigestInit_ex (handle, EVP_sha1 (), NULL)))
    return handle;

  EVP_MD_CTX_destroy (handle);
  return NULL;
}

bool
tr_sha1_update (tr_sha1_ctx_t   handle,
                const void    * data,
                size_t          data_length)
{
  assert (handle != NULL);
  assert (data != NULL);

  return check_result (EVP_DigestUpdate (handle, data, data_length));
}

bool
tr_sha1_final (tr_sha1_ctx_t   handle,
               uint8_t       * hash)
{
  bool ret = true;

  if (hash != NULL)
    {
      unsigned int hash_length;

      assert (handle != NULL);

      ret = check_result (EVP_DigestFinal_ex (handle, hash, &hash_length));

      assert (!ret || hash_length == SHA_DIGEST_LENGTH);
    }

  EVP_MD_CTX_destroy (handle);
  return ret;
}

/***
****
***/

tr_rc4_ctx_t
tr_rc4_new (void)
{
  EVP_CIPHER_CTX * handle = EVP_CIPHER_CTX_new ();

  if (check_result (EVP_CipherInit_ex (handle, EVP_rc4 (), NULL, NULL, NULL, -1)))
    return handle;

  EVP_CIPHER_CTX_free (handle);
  return NULL;
}

void
tr_rc4_free (tr_rc4_ctx_t handle)
{
  if (handle == NULL)
    return;

  EVP_CIPHER_CTX_free (handle);
}

void
tr_rc4_set_key (tr_rc4_ctx_t    handle,
                const uint8_t * key,
                size_t          key_length)
{
  assert (handle != NULL);
  assert (key != NULL);

  if (!check_result (EVP_CIPHER_CTX_set_key_length (handle, key_length)))
    return;
  check_result (EVP_CipherInit_ex (handle, NULL, NULL, key, NULL, -1));
}

void
tr_rc4_process (tr_rc4_ctx_t   handle,
                const void   * input,
                void         * output,
                size_t         length)
{
  int output_length;

  assert (handle != NULL);
  assert (input != NULL);
  assert (output != NULL);

  check_result (EVP_CipherUpdate (handle, output, &output_length, input, length));
}

/***
****
***/

bool
tr_rand_buffer (void   * buffer,
                size_t   length)
{
  assert (buffer != NULL);

  return check_result (RAND_bytes (buffer, (int) length));
}
