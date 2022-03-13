// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <type_traits>

#ifdef HAVE_COMMONCRYPTO_COMMONBIGNUM_H
#include <CommonCrypto/CommonBigNum.h>
#endif
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonRandom.h>

#include <fmt/base.h>

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

#ifndef HAVE_COMMONCRYPTO_COMMONBIGNUM_H

using CCBigNumRef = struct _CCBigNumRef*;
using CCBigNumConstRef = struct _CCBigNumRef const*;
using CCStatus = CCCryptorStatus;

extern "C"
{
    CCBigNumRef CCBigNumFromData(CCStatus* status, void const* s, size_t len);
    CCBigNumRef CCCreateBigNum(CCStatus* status);
    CCBigNumRef CCBigNumCreateRandom(CCStatus* status, int bits, int top, int bottom);
    void CCBigNumFree(CCBigNumRef bn);
    CCStatus CCBigNumModExp(CCBigNumRef result, CCBigNumConstRef a, CCBigNumConstRef power, CCBigNumConstRef modulus);
    uint32_t CCBigNumByteCount(CCBigNumConstRef bn);
    size_t CCBigNumToData(CCStatus* status, CCBigNumConstRef bn, void* to);
}

#endif /* !HAVE_COMMONCRYPTO_COMMONBIGNUM_H */

/***
****
***/

namespace
{

static char constexpr MyName[] = "tr_crypto_utils";

char const* ccrypto_error_to_str(CCCryptorStatus error_code)
{
    switch (error_code)
    {
    case kCCSuccess:
        return "Operation completed normally";

    case kCCParamError:
        return "Illegal parameter value";

    case kCCBufferTooSmall:
        return "Insufficent buffer provided for specified operation";

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
        auto const errmsg = fmt::format(
            _("CCrypto error: {errmsg} ({errcode})"),
            fmt::arg("errmsg", ccrypto_error_to_str(error_code)),
            fmt::arg("errcode", error_code));
        tr_logAddMessage(file, line, TR_LOG_ERROR, MyName, errmsg);
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

bool check_ccrypto_pointer(void const* pointer, CCCryptorStatus const* result, char const* file, int line)
{
    bool const ret = pointer != nullptr;

    if (!ret)
    {
        log_ccrypto_error(*result, file, line);
    }

    return ret;
}

#define check_pointer(pointer, result) check_ccrypto_pointer((pointer), (result), __FILE__, __LINE__)

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

namespace
{

struct CCBigNumDeleter
{
    void operator()(CCBigNumRef bn) const noexcept
    {
        if (bn != nullptr)
        {
            CCBigNumFree(bn);
        }
    }
};

using CCBigNumPtr = std::unique_ptr<std::remove_pointer_t<CCBigNumRef>, CCBigNumDeleter>;

struct tr_dh_ctx
{
    CCBigNumPtr p;
    CCBigNumPtr g;
    CCBigNumPtr private_key;
};

} // namespace

tr_dh_ctx_t tr_dh_new(
    uint8_t const* prime_num,
    size_t prime_num_length,
    uint8_t const* generator_num,
    size_t generator_num_length)
{
    TR_ASSERT(prime_num != nullptr);
    TR_ASSERT(generator_num != nullptr);

    auto handle = std::make_unique<tr_dh_ctx>();
    auto status = CCStatus{};

    handle->p = CCBigNumPtr(CCBigNumFromData(&status, prime_num, prime_num_length));
    if (!check_pointer(handle->p.get(), &status))
    {
        return nullptr;
    }

    handle->g = CCBigNumPtr(CCBigNumFromData(&status, generator_num, generator_num_length));
    if (!check_pointer(handle->g.get(), &status))
    {
        return nullptr;
    }

    return handle.release();
}

void tr_dh_free(tr_dh_ctx_t handle)
{
    delete static_cast<tr_dh_ctx*>(handle);
}

bool tr_dh_make_key(tr_dh_ctx_t raw_handle, size_t private_key_length, uint8_t* public_key, size_t* public_key_length)
{
    TR_ASSERT(raw_handle != nullptr);
    TR_ASSERT(public_key != nullptr);

    auto& handle = *static_cast<tr_dh_ctx*>(raw_handle);
    auto status = CCStatus{};

    handle.private_key = CCBigNumPtr(CCBigNumCreateRandom(&status, private_key_length * 8, private_key_length * 8, 0));
    if (!check_pointer(handle.private_key.get(), &status))
    {
        return false;
    }

    auto const my_public_key = CCBigNumPtr(CCCreateBigNum(&status));
    if (!check_pointer(my_public_key.get(), &status))
    {
        return false;
    }

    if (!check_result(CCBigNumModExp(my_public_key.get(), handle.g.get(), handle.private_key.get(), handle.p.get())))
    {
        return false;
    }

    auto const my_public_key_length = CCBigNumByteCount(my_public_key.get());
    CCBigNumToData(&status, my_public_key.get(), public_key);
    if (!check_result(status))
    {
        return false;
    }

    auto const dh_size = CCBigNumByteCount(handle.p.get());
    tr_dh_align_key(public_key, my_public_key_length, dh_size);

    if (public_key_length != nullptr)
    {
        *public_key_length = dh_size;
    }

    return true;
}

tr_dh_secret_t tr_dh_agree(tr_dh_ctx_t raw_handle, uint8_t const* other_public_key, size_t other_public_key_length)
{
    TR_ASSERT(raw_handle != nullptr);
    TR_ASSERT(other_public_key != nullptr);

    auto const& handle = *static_cast<tr_dh_ctx*>(raw_handle);
    auto status = CCStatus{};

    auto const other_key = CCBigNumPtr(CCBigNumFromData(&status, other_public_key, other_public_key_length));
    if (!check_pointer(other_key.get(), &status))
    {
        return nullptr;
    }

    auto const my_secret_key = CCBigNumPtr(CCCreateBigNum(&status));
    if (!check_pointer(my_secret_key.get(), &status))
    {
        return nullptr;
    }

    if (!check_result(CCBigNumModExp(my_secret_key.get(), other_key.get(), handle.private_key.get(), handle.p.get())))
    {
        return nullptr;
    }

    auto const dh_size = CCBigNumByteCount(handle.p.get());
    auto ret = std::unique_ptr<tr_dh_secret, decltype(&tr_free)>(tr_dh_secret_new(dh_size), &tr_free);

    auto const my_secret_key_length = CCBigNumByteCount(my_secret_key.get());
    CCBigNumToData(&status, my_secret_key.get(), ret->key);
    if (!check_result(status))
    {
        return nullptr;
    }

    tr_dh_secret_align(ret.get(), my_secret_key_length);

    return ret.release();
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
