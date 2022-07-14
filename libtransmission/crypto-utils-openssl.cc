// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifdef __APPLE__
/* OpenSSL "deprecated" as of OS X 10.7, but we still use it */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <fmt/core.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"

/***
****
***/

static void log_openssl_error(char const* file, int line)
{
    unsigned long const error_code = ERR_get_error();

    if (tr_logLevelIsActive(TR_LOG_ERROR))
    {
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
        tr_logAddMessage(
            file,
            line,
            TR_LOG_ERROR,
            fmt::format(
                _("{crypto_library} error: {error} ({error_code})"),
                fmt::arg("crypto_library", "OpenSSL"),
                fmt::arg("error", buf),
                fmt::arg("error_code", error_code)));
    }
}

#define log_error() log_openssl_error(__FILE__, __LINE__)

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

tr_sha256_ctx_t tr_sha256_init()
{
    EVP_MD_CTX* handle = EVP_MD_CTX_create();

    if (check_result(EVP_DigestInit_ex(handle, EVP_sha256(), nullptr)))
    {
        return handle;
    }

    EVP_MD_CTX_destroy(handle);
    return nullptr;
}

bool tr_sha256_update(tr_sha256_ctx_t raw_handle, void const* data, size_t data_length)
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

std::optional<tr_sha256_digest_t> tr_sha256_final(tr_sha1_ctx_t raw_handle)
{
    auto* handle = static_cast<EVP_MD_CTX*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    unsigned int hash_length = 0;
    auto digest = tr_sha256_digest_t{};
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
        log_error();
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
