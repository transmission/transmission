/*
 * This file Copyright (C) 2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

/* This file is designed specifically to be included by other source files to
   implement missing (or duplicate) functionality without exposing internal
   details in header files. */

#include <assert.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "utils.h"

/***
****
***/

#ifdef TR_CRYPTO_DH_SECRET_FALLBACK

/* Most Diffie-Hellman backends handle secret key in the very same way: by
   manually allocating memory for it and storing the value in plain form. */

struct tr_dh_secret
{
  size_t  key_length;
  uint8_t key[];
};

static struct tr_dh_secret *
tr_dh_secret_new (size_t key_length)
{
  struct tr_dh_secret * handle = tr_malloc (sizeof (struct tr_dh_secret) + key_length);
  handle->key_length = key_length;
  return handle;
}

static void
tr_dh_secret_align (struct tr_dh_secret * handle,
                    size_t                current_key_length)
{
  tr_dh_align_key (handle->key, current_key_length, handle->key_length);
}

bool
tr_dh_secret_derive (tr_dh_secret_t   raw_handle,
                     const void     * prepend_data,
                     size_t           prepend_data_size,
                     const void     * append_data,
                     size_t           append_data_size,
                     uint8_t        * hash)
{
  struct tr_dh_secret * handle = raw_handle;

  assert (handle != NULL);
  assert (hash != NULL);

  return tr_sha1 (hash,
                  prepend_data == NULL ? "" : prepend_data,
                  prepend_data == NULL ? 0 : (int) prepend_data_size,
                  handle->key, (int) handle->key_length,
                  append_data, append_data == NULL ? 0 : (int) append_data_size,
                  NULL);
}

void
tr_dh_secret_free (tr_dh_secret_t handle)
{
  tr_free (handle);
}

#endif /* TR_CRYPTO_DH_SECRET_FALLBACK */
