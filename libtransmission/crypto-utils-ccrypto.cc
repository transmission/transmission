// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <type_traits>

#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonRandom.h>

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

namespace
{

char const* ccrypto_error_to_str(CCCryptorStatus error_code)
{
    switch (error_code)
    {
    case kCCSuccess:
        return "Operation completed normally";

    case kCCParamError:
        return "Illegal parameter value";

    case kCCBufferTooSmall:
        return "Insufficient buffer provided for specified operation";

    case kCCMemoryFailure:
        return "Memory allocation failure";

    case kCCAlignmentError:
        return "Input size was not aligned properly";

    case kCCDecodeError:
        return "Input data did not decode or decrypt properly";

    case kCCUnimplemented:
        return "Function not implemented for the current algorithm";

    case kCCOverflow:
        return "Buffer overflow";

    case kCCRNGFailure:
        return "Random number generator failure";
    }

    return "Unknown error";
}

void log_ccrypto_error(CCCryptorStatus error_code, char const* file, int line)
{
    if (tr_logLevelIsActive(TR_LOG_ERROR))
    {
        tr_logAddMessage(
            file,
            line,
            TR_LOG_ERROR,
            fmt::format(
                _("{crypto_library} error: {error} ({error_code})"),
                fmt::arg("crypto_library", "CCrypto"),
                fmt::arg("error", ccrypto_error_to_str(error_code)),
                fmt::arg("error_code", error_code)));
    }
}

bool check_ccrypto_result(CCCryptorStatus result, char const* file, int line)
{
    bool const ret = result == kCCSuccess;

    if (!ret)
    {
        log_ccrypto_error(result, file, line);
    }

    return ret;
}

#define check_result(result) check_ccrypto_result((result), __FILE__, __LINE__)

} // namespace

/***
****
***/

tr_sha1_ctx_t tr_sha1_init(void)
{
    auto* handle = new CC_SHA1_CTX();
    CC_SHA1_Init(handle);
    return handle;
}

bool tr_sha1_update(tr_sha1_ctx_t handle, void const* data, size_t data_length)
{
    TR_ASSERT(handle != nullptr);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != nullptr);

    CC_SHA1_Update(static_cast<CC_SHA1_CTX*>(handle), data, data_length);
    return true;
}

std::optional<tr_sha1_digest_t> tr_sha1_final(tr_sha1_ctx_t raw_handle)
{
    TR_ASSERT(raw_handle != nullptr);
    auto* handle = static_cast<CC_SHA1_CTX*>(raw_handle);

    auto digest = tr_sha1_digest_t{};
    auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
    CC_SHA1_Final(digest_as_uchar, handle);

    delete handle;
    return digest;
}

/***
****
***/

tr_sha256_ctx_t tr_sha256_init(void)
{
    auto* handle = new CC_SHA256_CTX();
    CC_SHA256_Init(handle);
    return handle;
}

bool tr_sha256_update(tr_sha256_ctx_t handle, void const* data, size_t data_length)
{
    TR_ASSERT(handle != nullptr);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != nullptr);

    CC_SHA256_Update(static_cast<CC_SHA256_CTX*>(handle), data, data_length);
    return true;
}

std::optional<tr_sha256_digest_t> tr_sha256_final(tr_sha256_ctx_t raw_handle)
{
    TR_ASSERT(raw_handle != nullptr);
    auto* handle = static_cast<CC_SHA256_CTX*>(raw_handle);

    auto digest = tr_sha256_digest_t{};
    auto* const digest_as_uchar = reinterpret_cast<unsigned char*>(std::data(digest));
    CC_SHA256_Final(digest_as_uchar, handle);

    delete handle;
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

    return check_result(CCRandomGenerateBytes(buffer, length));
}
