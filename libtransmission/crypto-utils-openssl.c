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
#include <openssl/rand.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "log.h"

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

bool
tr_rand_buffer (void   * buffer,
                size_t   length)
{
  assert (buffer != NULL);

  return check_result (RAND_bytes (buffer, (int) length));
}
