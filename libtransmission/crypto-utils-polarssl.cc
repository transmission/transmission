// This file Copyright © 2014-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <mutex>

#if defined(POLARSSL_IS_MBEDTLS)
#define API_HEADER(x) <mbedtls/x>
#define API(x) mbedtls_##x
#define API_VERSION_NUMBER MBEDTLS_VERSION_NUMBER
#else
#define API_HEADER(x) <polarssl/x>
#define API(x) x
#define API_VERSION_NUMBER POLARSSL_VERSION_NUMBER
#endif

#include API_HEADER(base64.h)
#include API_HEADER(ctr_drbg.h)
#include API_HEADER(dhm.h)
#include API_HEADER(error.h)
#include API_HEADER(sha1.h)
#include API_HEADER(sha256.h)
#include API_HEADER(version.h)

#include <fmt/core.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"

#define TR_CRYPTO_X509_FALLBACK
#include "crypto-utils-fallback.cc" // NOLINT(bugprone-suspicious-include)

/***
****
***/

using api_ctr_drbg_context = API(ctr_drbg_context);
using api_sha1_context = API(sha1_context);
using api_sha256_context = API(sha256_context);
using api_dhm_context = API(dhm_context);

static void log_polarssl_error(int error_code, char const* file, int line)
{
    if (tr_logLevelIsActive(TR_LOG_ERROR))
    {
        char error_message[256];

#if defined(POLARSSL_IS_MBEDTLS)
        mbedtls_strerror(error_code, error_message, sizeof(error_message));
#elif API_VERSION_NUMBER >= 0x01030000
        polarssl_strerror(error_code, error_message, sizeof(error_message));
#else
        error_strerror(error_code, error_message, sizeof(error_message));
#endif

        tr_logAddMessage(
            file,
            line,
            TR_LOG_ERROR,
            fmt::format(
                _("{crypto_library} error: {error} ({error_code})"),
                fmt::arg("crypto_library", "PolarSSL/MbedTLS"),
                fmt::arg("error", error_message),
                fmt::arg("error_code", error_code)));
    }
}

#define log_error(error_code) log_polarssl_error((error_code), __FILE__, __LINE__)

static bool check_polarssl_result(int result, int expected_result, char const* file, int line)
{
    bool const ret = result == expected_result;

    if (!ret)
    {
        log_polarssl_error(result, file, line);
    }

    return ret;
}

#define check_result(result) check_polarssl_result((result), 0, __FILE__, __LINE__)
#define check_result_eq(result, x_result) check_polarssl_result((result), (x_result), __FILE__, __LINE__)

/***
****
***/

static int my_rand(void* /*context*/, unsigned char* buffer, size_t buffer_size)
{
    for (size_t i = 0; i < buffer_size; ++i)
    {
        buffer[i] = tr_rand_int_weak(256);
    }

    return 0;
}

static api_ctr_drbg_context* get_rng(void)
{
    static api_ctr_drbg_context rng;
    static bool rng_initialized = false;

    if (!rng_initialized)
    {
#if API_VERSION_NUMBER >= 0x02000000
        API(ctr_drbg_init)(&rng);

        if (!check_result(API(ctr_drbg_seed)(&rng, &my_rand, nullptr, nullptr, 0)))
#else

        if (!check_result(API(ctr_drbg_init)(&rng, &my_rand, nullptr, nullptr, 0)))
#endif
        {
            return nullptr;
        }

        rng_initialized = true;
    }

    return &rng;
}

static std::recursive_mutex rng_mutex_;

/***
****
***/

tr_sha1_ctx_t tr_sha1_init(void)
{
    api_sha1_context* handle = tr_new0(api_sha1_context, 1);

#if API_VERSION_NUMBER >= 0x01030800
    API(sha1_init)(handle);
#endif

    API(sha1_starts)(handle);
    return handle;
}

bool tr_sha1_update(tr_sha1_ctx_t raw_handle, void const* data, size_t data_length)
{
    auto* handle = static_cast<api_sha1_context*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != nullptr);

    API(sha1_update)(handle, static_cast<unsigned char const*>(data), data_length);
    return true;
}

std::optional<tr_sha1_digest_t> tr_sha1_final(tr_sha1_ctx_t raw_handle)
{
    auto* handle = static_cast<api_sha1_context*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    auto digest = tr_sha1_digest_t{};
    auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
    API(sha1_finish)(handle, digest_as_uchar);
#if API_VERSION_NUMBER >= 0x01030800
    API(sha1_free)(handle);
#endif

    tr_free(handle);
    return digest;
}

/***
****
***/

tr_sha256_ctx_t tr_sha256_init(void)
{
    api_sha256_context* handle = tr_new0(api_sha256_context, 1);

#if API_VERSION_NUMBER >= 0x01030800
    API(sha256_init)(handle);
#endif

    API(sha256_starts)(handle, 0);
    return handle;
}

bool tr_sha256_update(tr_sha256_ctx_t raw_handle, void const* data, size_t data_length)
{
    auto* handle = static_cast<api_sha256_context*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != nullptr);

    API(sha256_update)(handle, static_cast<unsigned char const*>(data), data_length);
    return true;
}

std::optional<tr_sha256_digest_t> tr_sha256_final(tr_sha256_ctx_t raw_handle)
{
    auto* handle = static_cast<api_sha256_context*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    auto digest = tr_sha256_digest_t{};
    auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
    API(sha256_finish)(handle, digest_as_uchar);
#if API_VERSION_NUMBER >= 0x01030800
    API(sha256_free)(handle);
#endif

    tr_free(handle);
    return digest;
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

    auto const lock = std::lock_guard(rng_mutex_);
    return check_result(API(ctr_drbg_random)(get_rng(), static_cast<unsigned char*>(buffer), length));
}
