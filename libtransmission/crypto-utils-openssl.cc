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

namespace
{

class ShaHelper
{
public:
    using EvpFunc = decltype((EVP_sha1));

    ShaHelper(EvpFunc evp_func)
        : evp_func_{ evp_func }
    {
        clear();
    }

    void clear()
    {
        EVP_DigestInit_ex(handle_.get(), evp_func_(), nullptr);
    }

    void update(void const* data, size_t data_length)
    {
        if (data_length != 0U)
        {
            EVP_DigestUpdate(handle_.get(), data, data_length);
        }
    }

    template<typename DigestType>
    [[nodiscard]] DigestType digest()
    {
        TR_ASSERT(handle_ != nullptr);

        unsigned int hash_length = 0;
        auto digest = DigestType{};
        auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
        bool const ok = check_result(EVP_DigestFinal_ex(handle_.get(), digest_as_uchar, &hash_length));
        TR_ASSERT(!ok || hash_length == std::size(digest));

        clear();
        return digest;
    }

private:
    struct MessageDigestDeleter
    {
        void operator()(EVP_MD_CTX* ctx) const noexcept
        {
            EVP_MD_CTX_destroy(ctx);
        }
    };

    EvpFunc evp_func_;
    std::unique_ptr<EVP_MD_CTX, MessageDigestDeleter> const handle_{ EVP_MD_CTX_create() };
};

class Sha1Impl final : public tr_sha1
{
public:
    ~Sha1Impl() override = default;

    void clear() override
    {
        helper_.clear();
    }

    void add(void const* data, size_t data_length) override
    {
        helper_.update(data, data_length);
    }

    [[nodiscard]] tr_sha1_digest_t final() override
    {
        return helper_.digest<tr_sha1_digest_t>();
    }

private:
    ShaHelper helper_{ EVP_sha1 };
};

class Sha256Impl final : public tr_sha256
{
public:
    ~Sha256Impl() override = default;

    void clear() override
    {
        helper_.clear();
    }

    void add(void const* data, size_t data_length) override
    {
        helper_.update(data, data_length);
    }

    [[nodiscard]] tr_sha256_digest_t final() override
    {
        return helper_.digest<tr_sha256_digest_t>();
    }

private:
    ShaHelper helper_{ EVP_sha256 };
};

} // namespace

std::unique_ptr<tr_sha1> tr_sha1::create()
{
    return std::make_unique<Sha1Impl>();
}

std::unique_ptr<tr_sha256> tr_sha256::create()
{
    return std::make_unique<Sha256Impl>();
}

/***
****
***/

#if OPENSSL_VERSION_NUMBER < 0x0090802fL

static EVP_CIPHER_CTX* openssl_evp_cipher_context_new()
{
    auto* const handle = new EVP_CIPHER_CTX{};

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
    delete handle;
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
