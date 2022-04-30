// This file Copyright © 2014-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

/* This file is designed specifically to be included by other source files to
   implement missing (or duplicate) functionality without exposing internal
   details in header files. */

#include <string_view>

#include "transmission.h"

#include "crypto-utils.h"
#include "tr-assert.h"
#include "tr-macros.h"
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
    auto* handle = static_cast<struct tr_dh_secret*>(tr_malloc(sizeof(struct tr_dh_secret) + key_length));
    handle->key_length = key_length;
    return handle;
}

static void tr_dh_secret_align(struct tr_dh_secret* handle, size_t current_key_length)
{
    tr_dh_align_key(handle->key, current_key_length, handle->key_length);
}

std::optional<tr_sha1_digest_t> tr_dh_secret_derive(
    tr_dh_secret_t raw_handle,
    void const* prepend_data,
    size_t prepend_data_size,
    void const* append_data,
    size_t append_data_size)
{
    TR_ASSERT(raw_handle != nullptr);

    auto const* handle = static_cast<struct tr_dh_secret*>(raw_handle);

    return tr_sha1(
        std::string_view{ static_cast<char const*>(prepend_data), prepend_data_size },
        std::string_view{ reinterpret_cast<char const*>(handle->key), handle->key_length },
        std::string_view{ static_cast<char const*>(append_data), append_data_size });
}

void tr_dh_secret_free(tr_dh_secret_t handle)
{
    tr_free(handle);
}

#endif /* TR_CRYPTO_DH_SECRET_FALLBACK */

#ifdef TR_CRYPTO_X509_FALLBACK

tr_x509_store_t tr_ssl_get_x509_store(tr_ssl_ctx_t /*handle*/)
{
    return nullptr;
}

bool tr_x509_store_add(tr_x509_store_t /*handle*/, tr_x509_cert_t /*cert*/)
{
    return false;
}

tr_x509_cert_t tr_x509_cert_new(void const* /*der*/, size_t /*der_length*/)
{
    return nullptr;
}

void tr_x509_cert_free(tr_x509_cert_t /*handle*/)
{
}

#endif /* TR_CRYPTO_X509_FALLBACK */
