#include <MacTypes.h>
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonRandom.h>
#include "crypto-utils.h"
#include "log.h"
#include "tr-assert.h"
#define TR_CRYPTO_X509_FALLBACK
#define TR_CRYPTO_DH_SECRET_FALLBACK
#include "crypto-utils-fallback.c"

#define check_result(result) check_ccrypto_result((result), 0, __FILE__, __LINE__)
#define MY_NAME "tr_crypto_utils"

typedef struct OpaqueSecDHContext* SecDHContext;
OSStatus SecDHCreate(uint32_t g, uint8_t const* p, size_t p_len, uint32_t l, uint8_t const* recip, size_t recip_len,
    SecDHContext* dh);
OSStatus SecDHGenerateKeypair(SecDHContext dh, uint8_t* pub_key, size_t* pub_key_len);
OSStatus SecDHComputeKey(SecDHContext dh, uint8_t const* pub_key, size_t pub_key_len, uint8_t* computed_key,
    size_t* computed_key_len);
size_t SecDHGetMaxKeyLength(SecDHContext dh);
void SecDHDestroy(SecDHContext dh);

static void log_ccrypto_error(char const* file, int line, char const* buf)
{
    if (tr_logLevelIsActive(TR_LOG_ERROR))
    {
        tr_logAddMessage(file, line, TR_LOG_ERROR, MY_NAME, "CommonCrypto error: %s", buf);
    }
}

static bool check_ccrypto_result(int result, int expected_result, char const* file, int line)
{
    bool const ret = (result == expected_result);
    char const* err = NULL;
    switch (result)
    {
    case kCCSuccess: // OK
        break;

    case kCCParamError:
        err = "Illegal parameter value.";
        break;

    case kCCBufferTooSmall:
        err = "Insufficent buffer provided for specified operation.";
        break;

    case kCCMemoryFailure:
        err = "Memory allocation failure.";
        break;

    case kCCAlignmentError:
        err = "Input size was not aligned properly.";
        break;

    case kCCDecodeError:
        err = "Input data did not decode or decrypt properly.";
        break;

    case kCCUnimplemented:
        err = "Function not implemented for the current algorithm.";
        break;

    case kCCInvalidKey:
        err = "Key is not valid.";
        break;

    default:
        break;
    }

    if (!ret)
    {
        log_ccrypto_error(file, line, err);
    }

    return ret;
}

/**
 * @brief Allocate and initialize new SHA1 hasher context.
 */
tr_sha1_ctx_t tr_sha1_init(void)
{
    CC_SHA1_CTX* ctx = tr_malloc0(sizeof(CC_SHA1_CTX));
    CC_SHA1_Init(ctx);
    return ctx;
}

/**
 * @brief Update SHA1 hash.
 */
bool tr_sha1_update(tr_sha1_ctx_t handle, void const* data, size_t data_length)
{
    TR_ASSERT(handle != NULL);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != NULL);

    CC_SHA1_Update((CC_SHA1_CTX*)handle, data, data_length);
    return true;
}

/**
 * @brief Finalize and export SHA1 hash, free hasher context.
 */
bool tr_sha1_final(tr_sha1_ctx_t handle, uint8_t* hash)
{
    TR_ASSERT(handle != NULL);

    CC_SHA1_Final(hash, (CC_SHA1_CTX*)handle);
    tr_free(handle);
    return true;
}

/**
 * @brief Allocate and initialize new RC4 cipher context.
 */
tr_rc4_ctx_t tr_rc4_new(void)
{
    return tr_malloc0(kCCContextSizeRC4);
}

/**
 * @brief Free RC4 cipher context.
 */
void tr_rc4_free(tr_rc4_ctx_t handle)
{
    check_result(CCCryptorRelease((CCCryptorRef)handle));
    tr_free(handle);
}

/**
 * @brief Set RC4 cipher key.
 */
void tr_rc4_set_key(tr_rc4_ctx_t handle, uint8_t const* key, size_t key_length)
{
    TR_ASSERT(handle != NULL);
    TR_ASSERT(key != NULL);

    check_result(CCCryptorCreate(kCCEncrypt, kCCAlgorithmRC4, 0, key, key_length, NULL, handle));
}

/**
 * @brief Process memory block with RC4 cipher.
 */
void tr_rc4_process(tr_rc4_ctx_t handle, void const* input, void* output, size_t length)
{
    TR_ASSERT(handle != NULL);

    if (length == 0)
    {
        return;
    }

    TR_ASSERT(input != NULL);
    TR_ASSERT(output != NULL);

    size_t output_length;
    check_result(CCCryptorUpdate(handle, input, length, output, length, &output_length));
}

/**
 * @brief Allocate and initialize new Diffie-Hellman (DH) key exchange context.
 */
tr_dh_ctx_t tr_dh_new(uint8_t const* prime_num, size_t prime_num_length, uint8_t const* generator_num,
    size_t generator_num_length)
{
    TR_ASSERT(prime_num != NULL);
    TR_ASSERT(generator_num != NULL);
    TR_ASSERT(generator_num_length == 1);

    SecDHContext dh;
    OSStatus status = SecDHCreate((uint32_t)generator_num[0], prime_num, prime_num_length, 0, NULL, 0, &dh);
    if (status != 0)
    {
        return NULL;
    }

    return dh;
}

/**
 * @brief Free DH key exchange context.
 */
void tr_dh_free(tr_dh_ctx_t handle)
{
    if (handle == NULL)
    {
        return;
    }

    SecDHDestroy(handle);
}

/**
 * @brief Generate private and public DH keys, export public key.
 */
bool tr_dh_make_key(tr_dh_ctx_t handle, __unused size_t private_key_length, uint8_t* public_key, size_t* public_key_length)
{
    TR_ASSERT(handle != NULL);
    TR_ASSERT(public_key != NULL);

    SecDHContext dh = handle;
    *public_key_length = SecDHGetMaxKeyLength(dh);
    OSStatus status = SecDHGenerateKeypair(dh, public_key, public_key_length);
    if (status != 0)
    {
        // ERROR
        return false;
    }

    return true;
}

/**
 * @brief Perform DH key exchange, generate secret key.
 */
tr_dh_secret_t tr_dh_agree(tr_dh_ctx_t handle, uint8_t const* other_public_key, size_t other_public_key_length)
{
    TR_ASSERT(handle != NULL);
    TR_ASSERT(other_public_key != NULL);

    SecDHContext dh = handle;
    size_t computed_key_len = SecDHGetMaxKeyLength(dh);
    struct tr_dh_secret* secret = tr_dh_secret_new(computed_key_len);

    OSStatus status = SecDHComputeKey(dh, other_public_key, other_public_key_length, secret->key, &secret->key_length);
    if (status != 0)
    {
        // ERROR
        tr_dh_secret_free(secret);
        return NULL;
    }

    tr_dh_secret_align(secret, secret->key_length);
    return secret;
}

/**
 * @brief Fill a buffer with random bytes.
 */
bool tr_rand_buffer(void* buffer, size_t length)
{
    TR_ASSERT(buffer != NULL);

    return check_result(CCRandomGenerateBytes(buffer, length));
}
