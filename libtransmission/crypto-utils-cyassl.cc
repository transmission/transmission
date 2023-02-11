// This file Copyright 2014-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <mutex>

#if defined(CYASSL_IS_WOLFSSL)
// NOLINTBEGIN bugprone-macro-parentheses
#define API_HEADER(x) <wolfssl/x>
#define API_HEADER_CRYPT(x) API_HEADER(wolfcrypt/x)
// NOLINTEND
#define API(x) wc_##x
#define API_VERSION_HEX LIBWOLFSSL_VERSION_HEX
#else
#define API_HEADER(x) <cyassl/x>
#define API_HEADER_CRYPT(x) API_HEADER(ctaocrypt/x)
#define API(x) x
#define API_VERSION_HEX LIBCYASSL_VERSION_HEX
#endif

#include API_HEADER(options.h)
#include API_HEADER_CRYPT(error-crypt.h)
#include API_HEADER_CRYPT(random.h)
#include API_HEADER_CRYPT(sha.h)
#include API_HEADER_CRYPT(sha256.h)
#include API_HEADER(version.h)

#include <fmt/core.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"

#if LIBWOLFSSL_VERSION_HEX >= 0x04000000 // 4.0.0
using TR_WC_RNG = WC_RNG;
#else
using TR_WC_RNG = RNG;
#endif

#define TR_CRYPTO_X509_FALLBACK
#include "crypto-utils-fallback.cc" // NOLINT(bugprone-suspicious-include)

/***
****
***/

static void log_cyassl_error(int error_code, char const* file, int line)
{
    if (tr_logLevelIsActive(TR_LOG_ERROR))
    {
#if API_VERSION_HEX >= 0x03004000
        char const* error_message = API(GetErrorString)(error_code);
#elif API_VERSION_HEX >= 0x03000002
        char const* error_message = CTaoCryptGetErrorString(error_code);
#else
        char error_message[CYASSL_MAX_ERROR_SZ] = {};
        CTaoCryptErrorString(error_code, error_message);
#endif

        tr_logAddMessage(
            file,
            line,
            TR_LOG_ERROR,
            fmt::format(
                _("{crypto_library} error: {error} ({error_code})"),
                fmt::arg("crypto_library", "CyaSSL/WolfSSL"),
                fmt::arg("error", error_message),
                fmt::arg("error_code", error_code)));
    }
}

static bool check_cyassl_result(int result, char const* file, int line)
{
    bool const ret = result == 0;

    if (!ret)
    {
        log_cyassl_error(result, file, line);
    }

    return ret;
}

#define check_result(result) check_cyassl_result((result), __FILE__, __LINE__)

/***
****
***/

static TR_WC_RNG* get_rng()
{
    static TR_WC_RNG rng;
    static bool rng_initialized = false;

    if (!rng_initialized)
    {
        if (!check_result(API(InitRng)(&rng)))
        {
            return nullptr;
        }

        rng_initialized = true;
    }

    return &rng;
}

static std::mutex rng_mutex_;

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
        API(InitSha)(&handle_);
    }

    void add(void const* data, size_t data_length) override
    {
        if (data_length > 0U)
        {
            API(ShaUpdate)(&handle_, static_cast<byte const*>(data), data_length);
        }
    }

    [[nodiscard]] tr_sha1_digest_t finish() override
    {
        auto digest = tr_sha1_digest_t{};
        API(ShaFinal)(&handle_, reinterpret_cast<byte*>(std::data(digest)));
        clear();
        return digest;
    }

private:
    API(Sha) handle_ = {};
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
        API(InitSha256)(&handle_);
    }

    void add(void const* data, size_t data_length) override
    {
        if (data_length > 0U)
        {
            API(Sha256Update)(&handle_, static_cast<byte const*>(data), data_length);
        }
    }

    [[nodiscard]] tr_sha256_digest_t finish() override
    {
        auto digest = tr_sha256_digest_t{};
        API(Sha256Final)(&handle_, reinterpret_cast<byte*>(std::data(digest)));
        clear();
        return digest;
    }

private:
    API(Sha256) handle_ = {};
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

bool tr_rand_buffer(void* buffer, size_t length)
{
    if (length == 0)
    {
        return true;
    }

    TR_ASSERT(buffer != nullptr);

    auto const lock = std::lock_guard(rng_mutex_);
    return check_result(API(RNG_GenerateBlock)(get_rng(), static_cast<byte*>(buffer), length));
}
