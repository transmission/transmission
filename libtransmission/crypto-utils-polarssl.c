/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>

#include <polarssl/arc4.h>
#include <polarssl/base64.h>
#include <polarssl/ctr_drbg.h>
#include <polarssl/dhm.h>
#include <polarssl/error.h>
#include <polarssl/sha1.h>
#include <polarssl/version.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "log.h"
#include "platform.h"
#include "utils.h"

#define TR_CRYPTO_DH_SECRET_FALLBACK
#include "crypto-utils-fallback.c"

/***
****
***/

#define MY_NAME "tr_crypto_utils"

static void
log_polarssl_error (int          error_code,
                    const char * file,
                    int          line)
{
  if (tr_logLevelIsActive (TR_LOG_ERROR))
    {
      char error_message[256];

#if POLARSSL_VERSION_NUMBER >= 0x01030000
      polarssl_strerror (error_code, error_message, sizeof (error_message));
#else
      error_strerror (error_code, error_message, sizeof (error_message));
#endif

      tr_logAddMessage (file, line, TR_LOG_ERROR, MY_NAME, "PolarSSL error: %s", error_message);
    }
}

#define log_error(error_code) log_polarssl_error(error_code, __FILE__, __LINE__)

static bool
check_polarssl_result (int          result,
                       int          expected_result,
                       const char * file,
                       int          line)
{
  const bool ret = result == expected_result;
  if (!ret)
    log_polarssl_error (result, file, line);
  return ret;
}

#define check_result(result) check_polarssl_result ((result), 0, __FILE__, __LINE__)
#define check_result_eq(result, x_result) check_polarssl_result ((result), (x_result), __FILE__, __LINE__)

/***
****
***/

static int
my_rand (void * context UNUSED, unsigned char * buffer, size_t buffer_size)
{
  size_t i;

  for (i = 0; i < buffer_size; ++i)
    buffer[i] = tr_rand_int_weak (256);

  return 0;
}

static ctr_drbg_context *
get_rng (void)
{
  static ctr_drbg_context rng;
  static bool rng_initialized = false;

  if (!rng_initialized)
    {
      if (!check_result (ctr_drbg_init (&rng, &my_rand, NULL, NULL, 0)))
        return NULL;
      rng_initialized = true;
    }

  return &rng;
}

static tr_lock *
get_rng_lock (void)
{
  static tr_lock * lock = NULL;

  if (lock == NULL)
    lock = tr_lockNew ();

  return lock;
}

/***
****
***/

tr_sha1_ctx_t
tr_sha1_init (void)
{
  sha1_context * handle = tr_new0 (sha1_context, 1);

#if POLARSSL_VERSION_NUMBER >= 0x01030800
  sha1_init (handle);
#endif

  sha1_starts (handle);
  return handle;
}

bool
tr_sha1_update (tr_sha1_ctx_t   handle,
                const void    * data,
                size_t          data_length)
{
  assert (handle != NULL);

  if (data_length == 0)
    return true;

  assert (data != NULL);

  sha1_update (handle, data, data_length);
  return true;
}

bool
tr_sha1_final (tr_sha1_ctx_t   handle,
               uint8_t       * hash)
{
  if (hash != NULL)
    {
      assert (handle != NULL);

      sha1_finish (handle, hash);
    }

#if POLARSSL_VERSION_NUMBER >= 0x01030800
  sha1_free (handle);
#endif

  tr_free (handle);
  return true;
}

/***
****
***/

tr_rc4_ctx_t
tr_rc4_new (void)
{
  arc4_context * handle = tr_new0 (arc4_context, 1);

#if POLARSSL_VERSION_NUMBER >= 0x01030800
  arc4_init (handle);
#endif

  return handle;
}

void
tr_rc4_free (tr_rc4_ctx_t handle)
{
#if POLARSSL_VERSION_NUMBER >= 0x01030800
  arc4_free (handle);
#endif

  tr_free (handle);
}

void
tr_rc4_set_key (tr_rc4_ctx_t    handle,
                const uint8_t * key,
                size_t          key_length)
{
  assert (handle != NULL);
  assert (key != NULL);

  arc4_setup (handle, key, key_length);
}

void
tr_rc4_process (tr_rc4_ctx_t   handle,
                const void   * input,
                void         * output,
                size_t         length)
{
  assert (handle != NULL);

  if (length == 0)
    return;

  assert (input != NULL);
  assert (output != NULL);

  arc4_crypt (handle, length, input, output);
}

/***
****
***/

tr_dh_ctx_t
tr_dh_new (const uint8_t * prime_num,
           size_t          prime_num_length,
           const uint8_t * generator_num,
           size_t          generator_num_length)
{
  dhm_context * handle = tr_new0 (dhm_context, 1);

  assert (prime_num != NULL);
  assert (generator_num != NULL);

#if POLARSSL_VERSION_NUMBER >= 0x01030800
  dhm_init (handle);
#endif

  if (!check_result (mpi_read_binary (&handle->P, prime_num, prime_num_length)) ||
      !check_result (mpi_read_binary (&handle->G, generator_num, generator_num_length)))
    {
      dhm_free (handle);
      return NULL;
    }

  handle->len = prime_num_length;

  return handle;
}

void
tr_dh_free (tr_dh_ctx_t handle)
{
  if (handle == NULL)
    return;

  dhm_free (handle);
}

bool
tr_dh_make_key (tr_dh_ctx_t   raw_handle,
                size_t        private_key_length,
                uint8_t     * public_key,
                size_t      * public_key_length)
{
  dhm_context * handle = raw_handle;

  assert (handle != NULL);
  assert (public_key != NULL);

  if (public_key_length != NULL)
    *public_key_length = handle->len;

  return check_result (dhm_make_public (handle, private_key_length, public_key,
                                        handle->len, my_rand, NULL));
}

tr_dh_secret_t
tr_dh_agree (tr_dh_ctx_t     raw_handle,
             const uint8_t * other_public_key,
             size_t          other_public_key_length)
{
  dhm_context * handle = raw_handle;
  struct tr_dh_secret * ret;
  size_t secret_key_length;

  assert (handle != NULL);
  assert (other_public_key != NULL);

  if (!check_result (dhm_read_public (handle, other_public_key,
                                      other_public_key_length)))
    return NULL;

  ret = tr_dh_secret_new (handle->len);

  secret_key_length = handle->len;

#if POLARSSL_VERSION_NUMBER >= 0x01030000
  if (!check_result (dhm_calc_secret (handle, ret->key,
                                      &secret_key_length, my_rand, NULL)))
#else
  if (!check_result (dhm_calc_secret (handle, ret->key, &secret_key_length)))
#endif
    {
      tr_dh_secret_free (ret);
      return NULL;
    }

  tr_dh_secret_align (ret, secret_key_length);

  return ret;
}

/***
****
***/

bool
tr_rand_buffer (void   * buffer,
                size_t   length)
{
  bool ret;
  tr_lock * rng_lock = get_rng_lock ();

  assert (buffer != NULL);

  tr_lockLock (rng_lock);
  ret = check_result (ctr_drbg_random (get_rng (), buffer, length));
  tr_lockUnlock (rng_lock);

  return ret;
}
