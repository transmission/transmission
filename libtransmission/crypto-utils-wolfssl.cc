// This file Copyright 2014-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <mutex>

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/version.h>

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

// ---

static void log_wolfssl_error(int error_code, char const* file, int line)
{
    if (tr_logLevelIsActive(TR_LOG_ERROR))
    {
        tr_logAddMessage(
            file,
            line,
            TR_LOG_ERROR,
            fmt::format(
                _("{crypto_library} error: {error} ({error_code})"),
                fmt::arg("crypto_library", "WolfSSL"),
                fmt::arg("error", wc_GetErrorString(error_code)),
                fmt::arg("error_code", error_code)));
    }
}

static bool check_wolfssl_result(int result, char const* file, int line)
{
    bool const ret = result == 0;

    if (!ret)
    {
        log_wolfssl_error(result, file, line);
    }

    return ret;
}

#define check_result(result) check_wolfssl_result((result), __FILE__, __LINE__)

// ---

static TR_WC_RNG* get_rng()
{
    static TR_WC_RNG rng;
    static bool rng_initialized = false;

    if (!rng_initialized)
    {
        if (!check_result(wc_InitRng(&rng)))
        {
            return nullptr;
        }

        rng_initialized = true;
    }

    return &rng;
}

static std::mutex rng_mutex_;

// ---

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
        wc_InitSha(&handle_);
    }

    void add(void const* data, size_t data_length) override
    {
        if (data_length > 0U)
        {
            wc_ShaUpdate(&handle_, static_cast<byte const*>(data), data_length);
        }
    }

    [[nodiscard]] tr_sha1_digest_t finish() override
    {
        auto digest = tr_sha1_digest_t{};
        wc_ShaFinal(&handle_, reinterpret_cast<byte*>(std::data(digest)));
        clear();
        return digest;
    }

private:
    wc_Sha handle_ = {};
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
        wc_InitSha256(&handle_);
    }

    void add(void const* data, size_t data_length) override
    {
        if (data_length > 0U)
        {
            wc_Sha256Update(&handle_, static_cast<byte const*>(data), data_length);
        }
    }

    [[nodiscard]] tr_sha256_digest_t finish() override
    {
        auto digest = tr_sha256_digest_t{};
        wc_Sha256Final(&handle_, reinterpret_cast<byte*>(std::data(digest)));
        clear();
        return digest;
    }

private:
    wc_Sha256 handle_ = {};
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

// ---

bool tr_rand_buffer_crypto(void* buffer, size_t length)
{
    if (length == 0)
    {
        return true;
    }

    TR_ASSERT(buffer != nullptr);

    auto const lock = std::lock_guard(rng_mutex_);
    return check_result(wc_RNG_GenerateBlock(get_rng(), static_cast<byte*>(buffer), length));
}
