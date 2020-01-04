/*
 * This file Copyright (C) 2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

/* This file is designed specifically to be included by other source files to
   implement missing (or duplicate) functionality without exposing internal
   details in header files. */

#include "transmission.h"
#include "crypto-utils.h"
#include "tr-assert.h"
#include "utils.h"

/***
****
***/

#ifdef TR_CRYPTO_DH_SECRET_FALLBACK

/* Most Diffie-Hellman backends handle secret key in the very same way: by
   manually allocating memory for it and storing the value in plain form. */

struct tr_dh_secret
{
    size_t key_length;
    uint8_t key[];
};

static struct tr_dh_secret* tr_dh_secret_new(size_t key_length)
{
    struct tr_dh_secret* handle = tr_malloc(sizeof(struct tr_dh_secret) + key_length);
    handle->key_length = key_length;
    return handle;
}

static void tr_dh_secret_align(struct tr_dh_secret* handle, size_t current_key_length)
{
    tr_dh_align_key(handle->key, current_key_length, handle->key_length);
}

bool tr_dh_secret_derive(tr_dh_secret_t raw_handle, void const* prepend_data, size_t prepend_data_size, void const* append_data,
    size_t append_data_size, uint8_t* hash)
{
    TR_ASSERT(raw_handle != NULL);
    TR_ASSERT(hash != NULL);

    struct tr_dh_secret* handle = raw_handle;

    return tr_sha1(hash, prepend_data == NULL ? "" : prepend_data, prepend_data == NULL ? 0 : (int)prepend_data_size,
        handle->key, (int)handle->key_length, append_data, append_data == NULL ? 0 : (int)append_data_size, NULL);
}

void tr_dh_secret_free(tr_dh_secret_t handle)
{
    tr_free(handle);
}

#endif /* TR_CRYPTO_DH_SECRET_FALLBACK */

#ifdef TR_CRYPTO_X509_FALLBACK

tr_x509_store_t tr_ssl_get_x509_store(tr_ssl_ctx_t handle)
{
    (void)handle;

    return NULL;
}

bool tr_x509_store_add(tr_x509_store_t handle, tr_x509_cert_t cert)
{
    (void)handle;
    (void)cert;

    return false;
}

tr_x509_cert_t tr_x509_cert_new(void const* der, size_t der_length)
{
    (void)der;
    (void)der_length;

    return NULL;
}

void tr_x509_cert_free(tr_x509_cert_t handle)
{
    (void)handle;
}

#endif /* TR_CRYPTO_X509_FALLBACK */
