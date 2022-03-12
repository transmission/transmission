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
#include API_HEADER(version.h)

#include <fmt/core.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"

#define TR_CRYPTO_DH_SECRET_FALLBACK
#define TR_CRYPTO_X509_FALLBACK
#include "crypto-utils-fallback.cc" // NOLINT(bugprone-suspicious-include)

/***
****
***/

static char constexpr MyName[] = "tr_crypto_utils";

using api_ctr_drbg_context = API(ctr_drbg_context);
using api_sha1_context = API(sha1_context);
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

        auto const errmsg = fmt::format(_("PolarSSL error: {errmsg}"), fmt::arg("errmsg", error_message));
        tr_logAddMessage(file, line, TR_LOG_ERROR, MyName, errmsg);
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

tr_dh_ctx_t tr_dh_new(
    uint8_t const* prime_num,
    size_t prime_num_length,
    uint8_t const* generator_num,
    size_t generator_num_length)
{
    TR_ASSERT(prime_num != nullptr);
    TR_ASSERT(generator_num != nullptr);

    api_dhm_context* handle = tr_new0(api_dhm_context, 1);

#if API_VERSION_NUMBER >= 0x01030800
    API(dhm_init)(handle);
#endif

    if (!check_result(API(mpi_read_binary)(&handle->P, prime_num, prime_num_length)) ||
        !check_result(API(mpi_read_binary)(&handle->G, generator_num, generator_num_length)))
    {
        API(dhm_free)(handle);
        return nullptr;
    }

    handle->len = prime_num_length;

    return handle;
}

void tr_dh_free(tr_dh_ctx_t raw_handle)
{
    auto* handle = static_cast<api_dhm_context*>(raw_handle);

    if (handle == nullptr)
    {
        return;
    }

    API(dhm_free)(handle);
}

bool tr_dh_make_key(tr_dh_ctx_t raw_handle, size_t private_key_length, uint8_t* public_key, size_t* public_key_length)
{
    TR_ASSERT(raw_handle != nullptr);
    TR_ASSERT(public_key != nullptr);

    auto* handle = static_cast<api_dhm_context*>(raw_handle);

    if (public_key_length != nullptr)
    {
        *public_key_length = handle->len;
    }

    return check_result(API(dhm_make_public)(handle, private_key_length, public_key, handle->len, my_rand, nullptr));
}

tr_dh_secret_t tr_dh_agree(tr_dh_ctx_t raw_handle, uint8_t const* other_public_key, size_t other_public_key_length)
{
    TR_ASSERT(raw_handle != nullptr);
    TR_ASSERT(other_public_key != nullptr);

    auto* handle = static_cast<api_dhm_context*>(raw_handle);

    if (!check_result(API(dhm_read_public)(handle, other_public_key, other_public_key_length)))
    {
        return nullptr;
    }

    tr_dh_secret* const ret = tr_dh_secret_new(handle->len);

    size_t secret_key_length = handle->len;

#if API_VERSION_NUMBER >= 0x02000000

    if (!check_result(API(dhm_calc_secret)(handle, ret->key, secret_key_length, &secret_key_length, my_rand, nullptr)))
#elif API_VERSION_NUMBER >= 0x01030000

    if (!check_result(API(dhm_calc_secret)(handle, ret->key, &secret_key_length, my_rand, nullptr)))
#else

    if (!check_result(API(dhm_calc_secret)(handle, ret->key, &secret_key_length)))
#endif
    {
        tr_dh_secret_free(ret);
        return nullptr;
    }

    tr_dh_secret_align(ret, secret_key_length);

    return ret;
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
