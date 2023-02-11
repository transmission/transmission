// This file Copyright © 2021-2023 Mnemosyne LLC.
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

// ---

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

void log_ccrypto_error(CCCryptorStatus error_code, char const* file, long line)
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

bool check_ccrypto_result(CCCryptorStatus result, char const* file, long line)
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
        CC_SHA1_Init(&handle_);
    }

    void add(void const* data, size_t data_length) override
    {
        static auto constexpr Max = static_cast<size_t>(std::numeric_limits<CC_LONG>::max());
        auto const* sha_data = static_cast<uint8_t const*>(data);
        while (data_length > 0)
        {
            auto const n_bytes = static_cast<CC_LONG>(std::min(data_length, Max));
            CC_SHA1_Update(&handle_, sha_data, n_bytes);
            data_length -= n_bytes;
            sha_data += n_bytes;
        }
    }

    [[nodiscard]] tr_sha1_digest_t finish() override
    {
        auto digest = tr_sha1_digest_t{};
        CC_SHA1_Final(reinterpret_cast<unsigned char*>(std::data(digest)), &handle_);
        clear();
        return digest;
    }

private:
    CC_SHA1_CTX handle_ = {};
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
        CC_SHA256_Init(&handle_);
    }

    void add(void const* data, size_t data_length) override
    {
        static auto constexpr Max = static_cast<size_t>(std::numeric_limits<CC_LONG>::max());
        auto const* sha_data = static_cast<uint8_t const*>(data);
        while (data_length > 0)
        {
            auto const n_bytes = static_cast<CC_LONG>(std::min(data_length, Max));
            CC_SHA256_Update(&handle_, sha_data, n_bytes);
            data_length -= n_bytes;
            sha_data += n_bytes;
        }
    }

    [[nodiscard]] tr_sha256_digest_t finish() override
    {
        auto digest = tr_sha256_digest_t{};
        CC_SHA256_Final(reinterpret_cast<unsigned char*>(std::data(digest)), &handle_);
        clear();
        return digest;
    }

private:
    CC_SHA256_CTX handle_;
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

    return check_result(CCRandomGenerateBytes(buffer, length));
}
