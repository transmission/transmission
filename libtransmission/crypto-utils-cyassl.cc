// This file Copyright 2014-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <mutex>

#if defined(CYASSL_IS_WOLFSSL)
#define API_HEADER(x) <wolfssl/x>
#define API_HEADER_CRYPT(x) API_HEADER(wolfcrypt/x)
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

static RNG* get_rng()
{
    static RNG rng;
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

tr_sha1_ctx_t tr_sha1_init()
{
    auto* const handle = new Sha{};

    if (check_result(API(InitSha)(handle)))
    {
        return handle;
    }

    delete handle;
    return nullptr;
}

bool tr_sha1_update(tr_sha1_ctx_t raw_handle, void const* data, size_t data_length)
{
    auto* handle = static_cast<Sha*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != nullptr);

    return check_result(API(ShaUpdate)(handle, static_cast<byte const*>(data), data_length));
}

std::optional<tr_sha1_digest_t> tr_sha1_final(tr_sha1_ctx_t raw_handle)
{
    auto* handle = static_cast<Sha*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    auto digest = tr_sha1_digest_t{};
    auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
    auto const ok = check_result(API(ShaFinal)(handle, digest_as_uchar));

    delete handle;
    return ok ? std::make_optional(digest) : std::nullopt;
}

/***
****
***/

tr_sha256_ctx_t tr_sha256_init()
{
    auto* const handle = new Sha256{};

    if (check_result(API(InitSha256)(handle)))
    {
        return handle;
    }

    delete handle;
    return nullptr;
}

bool tr_sha256_update(tr_sha256_ctx_t raw_handle, void const* data, size_t data_length)
{
    auto* handle = static_cast<Sha256*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != nullptr);

    return check_result(API(Sha256Update)(handle, static_cast<byte const*>(data), data_length));
}

std::optional<tr_sha256_digest_t> tr_sha256_final(tr_sha256_ctx_t raw_handle)
{
    auto* handle = static_cast<Sha256*>(raw_handle);
    TR_ASSERT(handle != nullptr);

    auto digest = tr_sha256_digest_t{};
    auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
    auto const ok = check_result(API(Sha256Final)(handle, digest_as_uchar));

    delete handle;
    return ok ? std::make_optional(digest) : std::nullopt;
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
