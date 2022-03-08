// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifdef __APPLE__
/* OpenSSL "deprecated" as of OS X 10.7, but we still use it */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <fmt/core.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/dh.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"

#define TR_CRYPTO_DH_SECRET_FALLBACK
#include "crypto-utils-fallback.cc" // NOLINT(bugprone-suspicious-include)

/***
****
***/

static char constexpr MyName[] = "tr_crypto_utils";

static void log_openssl_error(char const* file, int line)
{
    if (!tr_log::error::enabled())
    {
        return;
    }

    unsigned long const error_code = ERR_get_error();
    char buf[512];

#ifndef TR_LIGHTWEIGHT

    static bool strings_loaded = false;

    if (!strings_loaded)
    {
#if OPENSSL_VERSION_NUMBER < 0x10100000 || (defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x20700000)
        ERR_load_crypto_strings();
#else
        OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
#endif

        strings_loaded = true;
    }

#endif

    ERR_error_string_n(error_code, buf, sizeof(buf));
    tr_log::error::add(
        file,
        line,
        fmt::format(
            _("{cryptolib} error: {errmsg} ({errcode})"),
            fmt::arg("cryptolib", "OpenSSL"),
            fmt::arg("errmsg", buf),
            fmt::arg("errcode", error_code)),
        MyName);
}

#define logerr() log_openssl_error(__FILE__, __LINE__)

static bool check_openssl_result(int result, int expected_result, bool expected_equal, char const* file, int line)
{
    bool const ret = (result == expected_result) == expected_equal;

    if (!ret)
    {
        log_openssl_error(file, line);
    }

    return ret;
}

#define check_result(result) check_openssl_result((result), 1, true, __FILE__, __LINE__)
#define check_result_neq(result, x_result) check_openssl_result((result), (x_result), false, __FILE__, __LINE__)

static bool check_openssl_pointer(void const* pointer, char const* file, int line)
{
    bool const ret = pointer != nullptr;

    if (!ret)
    {
        log_openssl_error(file, line);
    }

    return ret;
}

#define check_pointer(pointer) check_openssl_pointer((pointer), __FILE__, __LINE__)

/***
****
***/

tr_sha1_ctx_t tr_sha1_init()
{
    EVP_MD_CTX* handle = EVP_MD_CTX_create();

    if (check_result(EVP_DigestInit_ex(handle, EVP_sha1(), nullptr)))
    {
        return handle;
    }

    EVP_MD_CTX_destroy(handle);
    return nullptr;
}

bool tr_sha1_update(tr_sha1_ctx_t raw_handle, void const* data, size_t data_length)
{
    auto* const handle = static_cast<EVP_MD_CTX*>(raw_handle);

    TR_ASSERT(handle != nullptr);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != nullptr);

    return check_result(EVP_DigestUpdate(handle, data, data_length));
}

std::optional<tr_sha1_digest_t> tr_sha1_final(tr_sha1_ctx_t raw_handle)
{
    auto* handle = static_cast<EVP_MD_CTX*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    unsigned int hash_length = 0;
    auto digest = tr_sha1_digest_t{};
    auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
    bool const ok = check_result(EVP_DigestFinal_ex(handle, digest_as_uchar, &hash_length));
    TR_ASSERT(!ok || hash_length == std::size(digest));

    EVP_MD_CTX_destroy(handle);
    return ok ? std::make_optional(digest) : std::nullopt;
}

/***
****
***/

#if OPENSSL_VERSION_NUMBER < 0x0090802fL

static EVP_CIPHER_CTX* openssl_evp_cipher_context_new(void)
{
    EVP_CIPHER_CTX* handle = tr_new(EVP_CIPHER_CTX, 1);

    if (handle != nullptr)
    {
        EVP_CIPHER_CTX_init(handle);
    }

    return handle;
}

static void openssl_evp_cipher_context_free(EVP_CIPHER_CTX* handle)
{
    if (handle == nullptr)
    {
        return;
    }

    EVP_CIPHER_CTX_cleanup(handle);
    tr_free(handle);
}

#define EVP_CIPHER_CTX_new() openssl_evp_cipher_context_new()
#define EVP_CIPHER_CTX_free(x) openssl_evp_cipher_context_free((x))

#endif

/***
****
***/

#if OPENSSL_VERSION_NUMBER < 0x10100000 || (defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x20700000)

static inline int DH_set0_pqg(DH* dh, BIGNUM* p, BIGNUM* q, BIGNUM* g)
{
    /* If the fields p and g in d are nullptr, the corresponding input
     * parameters MUST be non-nullptr. q may remain nullptr.
     */
    if ((dh->p == nullptr && p == nullptr) || (dh->g == nullptr && g == nullptr))
    {
        return 0;
    }

    if (p != nullptr)
    {
        BN_free(dh->p);
        dh->p = p;
    }

    if (q != nullptr)
    {
        BN_free(dh->q);
        dh->q = q;
    }

    if (g != nullptr)
    {
        BN_free(dh->g);
        dh->g = g;
    }

    if (q != nullptr)
    {
        dh->length = BN_num_bits(q);
    }

    return 1;
}

static constexpr int DH_set_length(DH* dh, long length)
{
    dh->length = length;
    return 1;
}

static constexpr void DH_get0_key(DH const* dh, BIGNUM const** pub_key, BIGNUM const** priv_key)
{
    if (pub_key != nullptr)
    {
        *pub_key = dh->pub_key;
    }

    if (priv_key != nullptr)
    {
        *priv_key = dh->priv_key;
    }
}

#endif

tr_dh_ctx_t tr_dh_new(
    uint8_t const* prime_num,
    size_t prime_num_length,
    uint8_t const* generator_num,
    size_t generator_num_length)
{
    TR_ASSERT(prime_num != nullptr);
    TR_ASSERT(generator_num != nullptr);

    DH* handle = DH_new();

    BIGNUM* const p = BN_bin2bn(prime_num, prime_num_length, nullptr);
    BIGNUM* const g = BN_bin2bn(generator_num, generator_num_length, nullptr);

    if (!check_pointer(p) || !check_pointer(g) || DH_set0_pqg(handle, p, nullptr, g) == 0)
    {
        BN_free(p);
        BN_free(g);
        DH_free(handle);
        handle = nullptr;
    }

    return handle;
}

void tr_dh_free(tr_dh_ctx_t raw_handle)
{
    auto* handle = static_cast<DH*>(raw_handle);

    if (handle == nullptr)
    {
        return;
    }

    DH_free(handle);
}

bool tr_dh_make_key(tr_dh_ctx_t raw_handle, size_t private_key_length, uint8_t* public_key, size_t* public_key_length)
{
    TR_ASSERT(raw_handle != nullptr);
    TR_ASSERT(public_key != nullptr);

    auto* handle = static_cast<DH*>(raw_handle);

    DH_set_length(handle, private_key_length * 8);

    if (!check_result(DH_generate_key(handle)))
    {
        return false;
    }

    BIGNUM const* my_public_key = nullptr;
    DH_get0_key(handle, &my_public_key, nullptr);

    int const my_public_key_length = BN_bn2bin(my_public_key, public_key);
    int const dh_size = DH_size(handle);

    tr_dh_align_key(public_key, my_public_key_length, dh_size);

    if (public_key_length != nullptr)
    {
        *public_key_length = dh_size;
    }

    return true;
}

tr_dh_secret_t tr_dh_agree(tr_dh_ctx_t raw_handle, uint8_t const* other_public_key, size_t other_public_key_length)
{
    auto* handle = static_cast<DH*>(raw_handle);

    TR_ASSERT(handle != nullptr);
    TR_ASSERT(other_public_key != nullptr);

    BIGNUM* const other_key = BN_bin2bn(other_public_key, other_public_key_length, nullptr);
    if (!check_pointer(other_key))
    {
        return nullptr;
    }

    int const dh_size = DH_size(handle);
    tr_dh_secret* ret = tr_dh_secret_new(dh_size);
    int const secret_key_length = DH_compute_key(ret->key, other_key, handle);

    if (check_result_neq(secret_key_length, -1))
    {
        tr_dh_secret_align(ret, secret_key_length);
    }
    else
    {
        tr_dh_secret_free(ret);
        ret = nullptr;
    }

    BN_free(other_key);
    return ret;
}

/***
****
***/

tr_x509_store_t tr_ssl_get_x509_store(tr_ssl_ctx_t handle)
{
    if (handle == nullptr)
    {
        return nullptr;
    }

    return SSL_CTX_get_cert_store(static_cast<SSL_CTX const*>(handle));
}

bool tr_x509_store_add(tr_x509_store_t handle, tr_x509_cert_t cert)
{
    TR_ASSERT(handle != nullptr);
    TR_ASSERT(cert != nullptr);

    return check_result(X509_STORE_add_cert(static_cast<X509_STORE*>(handle), static_cast<X509*>(cert)));
}

tr_x509_cert_t tr_x509_cert_new(void const* der, size_t der_length)
{
    TR_ASSERT(der != nullptr);

    X509* const ret = d2i_X509(nullptr, (unsigned char const**)&der, der_length);

    if (ret == nullptr)
    {
        logerr();
    }

    return ret;
}

void tr_x509_cert_free(tr_x509_cert_t handle)
{
    if (handle == nullptr)
    {
        return;
    }

    X509_free(static_cast<X509*>(handle));
}

/***
****
***/

bool tr_rand_buffer(void* buffer, size_t length)
{
    if (length == 0)
    {
        return true;
    }

    TR_ASSERT(buffer != nullptr);

    return check_result(RAND_bytes(static_cast<unsigned char*>(buffer), (int)length));
}
