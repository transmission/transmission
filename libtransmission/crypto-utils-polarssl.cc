// This file Copyright © 2014-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <mutex>

#if defined(POLARSSL_IS_MBEDTLS)
// NOLINTBEGIN bugprone-macro-parentheses
#define API_HEADER(x) <mbedtls/x>
#define API(x) mbedtls_##x
// NOLINTEND
#define API_VERSION_NUMBER MBEDTLS_VERSION_NUMBER
#else
#define API_HEADER(x) <polarssl/x>
#define API(x) x
#define API_VERSION_NUMBER POLARSSL_VERSION_NUMBER
#endif

#include API_HEADER(base64.h)
#include API_HEADER(ctr_drbg.h)
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
    // since we're initializing tr_rand_buffer()'s rng, we can't use tr_rand_buffer() here
    tr_rand_buffer_std(buffer, buffer_size);
    return 0;
}

static api_ctr_drbg_context* get_rng()
{
    static api_ctr_drbg_context rng;
    static bool rng_initialized = false;

    if (!rng_initialized)
    {
#if API_VERSION_NUMBER >= 0x02000000
        API(ctr_drbg_init)(&rng);

        if (!check_result(API(ctr_drbg_seed)(&rng, my_rand, nullptr, nullptr, 0)))
#else

        if (!check_result(API(ctr_drbg_init)(&rng, my_rand, nullptr, nullptr, 0)))
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

namespace
{

class Sha1Impl final : public tr_sha1
{
public:
    Sha1Impl()
    {
        clear();
    }

    ~Sha1Impl() override = default;

    void clear() override
    {
#if API_VERSION_NUMBER >= 0x01030800
        API(sha1_init)(&handle_);
#endif

#if API_VERSION_NUMBER >= 0x02070000
        mbedtls_sha1_starts_ret(&handle_);
#else
        API(sha1_starts)(&handle_);
#endif
    }

    void add(void const* data, size_t data_length) override
    {
        if (data_length > 0U)
        {
#if API_VERSION_NUMBER >= 0x02070000
            mbedtls_sha1_update_ret(&handle_, static_cast<unsigned char const*>(data), data_length);
#else
            API(sha1_update)(&handle_, static_cast<unsigned char const*>(data), data_length);
#endif
        }
    }

    [[nodiscard]] tr_sha1_digest_t finish() override
    {
        auto digest = tr_sha1_digest_t{};
        auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
#if API_VERSION_NUMBER >= 0x02070000
        mbedtls_sha1_finish_ret(&handle_, digest_as_uchar);
#else
        API(sha1_finish)(&handle_, digest_as_uchar);
#endif

#if API_VERSION_NUMBER >= 0x01030800
        API(sha1_free)(&handle_);
#endif

        return digest;
    }

private:
    mbedtls_sha1_context handle_ = {};
};

class Sha256Impl final : public tr_sha256
{
public:
    Sha256Impl()
    {
        clear();
    }

    ~Sha256Impl() override = default;

    void clear() override
    {
#if API_VERSION_NUMBER >= 0x01030800
        API(sha256_init)(&handle_);
#endif

#if API_VERSION_NUMBER >= 0x02070000
        mbedtls_sha256_starts_ret(&handle_, 0);
#else
        API(sha256_starts)(&handle_);
#endif
    }

    void add(void const* data, size_t data_length) override
    {
        if (data_length > 0U)
        {
#if API_VERSION_NUMBER >= 0x02070000
            mbedtls_sha256_update_ret(&handle_, static_cast<unsigned char const*>(data), data_length);
#else
            API(sha256_update)(&handle_, static_cast<unsigned char const*>(data), data_length);
#endif
        }
    }

    [[nodiscard]] tr_sha256_digest_t finish() override
    {
        auto digest = tr_sha256_digest_t{};
        auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
#if API_VERSION_NUMBER >= 0x02070000
        mbedtls_sha256_finish_ret(&handle_, digest_as_uchar);
#else
        API(sha256_finish)(&handle_, digest_as_uchar);
#endif

#if API_VERSION_NUMBER >= 0x01030800
        API(sha256_free)(&handle_);
#endif

        return digest;
    }

private:
    mbedtls_sha256_context handle_ = {};
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

bool tr_rand_buffer_crypto(void* buffer, size_t length)
{
    if (length == 0)
    {
        return true;
    }

    TR_ASSERT(buffer != nullptr);

    auto const lock = std::lock_guard(rng_mutex_);
    return check_result(API(ctr_drbg_random)(get_rng(), static_cast<unsigned char*>(buffer), length));
}
