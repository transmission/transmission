/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

/* *INDENT-OFF* */
#if defined(CYASSL_IS_WOLFSSL)
#define API_HEADER(x) <wolfssl/x>
#define API_HEADER_CRYPT(x) API_HEADER(wolfcrypt/x)
#define API(x) wc_ ## x
#define API_VERSION_HEX LIBWOLFSSL_VERSION_HEX
#else
#define API_HEADER(x) <cyassl/x>
#define API_HEADER_CRYPT(x) API_HEADER(ctaocrypt/x)
#define API(x) x
#define API_VERSION_HEX LIBCYASSL_VERSION_HEX
#endif

#include API_HEADER_CRYPT(arc4.h)
#include API_HEADER_CRYPT(dh.h)
#include API_HEADER_CRYPT(error-crypt.h)
#include API_HEADER_CRYPT(random.h)
#include API_HEADER_CRYPT(sha.h)
#include API_HEADER(version.h)
/* *INDENT-ON* */

#include "transmission.h"
#include "crypto-utils.h"
#include "log.h"
#include "platform.h"
#include "tr-assert.h"
#include "utils.h"

#define TR_CRYPTO_DH_SECRET_FALLBACK
#define TR_CRYPTO_X509_FALLBACK
#include "crypto-utils-fallback.c"

struct tr_dh_ctx
{
    DhKey dh;
    word32 key_length;
    uint8_t* private_key;
    word32 private_key_length;
};

/***
****
***/

#define MY_NAME "tr_crypto_utils"

static void log_cyassl_error(int error_code, char const* file, int line)
{
    if (tr_logLevelIsActive(TR_LOG_ERROR))
    {
#if API_VERSION_HEX >= 0x03004000
        char const* error_message = API(GetErrorString)(error_code);
#elif API_VERSION_HEX >= 0x03000002
        char const* error_message = CTaoCryptGetErrorString(error_code);
#else
        char error_message[CYASSL_MAX_ERROR_SZ];
        CTaoCryptErrorString(error_code, error_message);
#endif

        tr_logAddMessage(file, line, TR_LOG_ERROR, MY_NAME, "CyaSSL error: %s", error_message);
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

static RNG* get_rng(void)
{
    static RNG rng;
    static bool rng_initialized = false;

    if (!rng_initialized)
    {
        if (!check_result(API(InitRng)(&rng)))
        {
            return NULL;
        }

        rng_initialized = true;
    }

    return &rng;
}

static tr_lock* get_rng_lock(void)
{
    static tr_lock* lock = NULL;

    if (lock == NULL)
    {
        lock = tr_lockNew();
    }

    return lock;
}

/***
****
***/

tr_sha1_ctx_t tr_sha1_init(void)
{
    Sha* handle = tr_new(Sha, 1);

    if (check_result(API(InitSha)(handle)))
    {
        return handle;
    }

    tr_free(handle);
    return NULL;
}

bool tr_sha1_update(tr_sha1_ctx_t handle, void const* data, size_t data_length)
{
    TR_ASSERT(handle != NULL);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != NULL);

    return check_result(API(ShaUpdate)(handle, data, data_length));
}

bool tr_sha1_final(tr_sha1_ctx_t handle, uint8_t* hash)
{
    bool ret = true;

    if (hash != NULL)
    {
        TR_ASSERT(handle != NULL);

        ret = check_result(API(ShaFinal)(handle, hash));
    }

    tr_free(handle);
    return ret;
}

/***
****
***/

tr_rc4_ctx_t tr_rc4_new(void)
{
    return tr_new0(Arc4, 1);
}

void tr_rc4_free(tr_rc4_ctx_t handle)
{
    tr_free(handle);
}

void tr_rc4_set_key(tr_rc4_ctx_t handle, uint8_t const* key, size_t key_length)
{
    TR_ASSERT(handle != NULL);
    TR_ASSERT(key != NULL);

    API(Arc4SetKey)(handle, key, key_length);
}

void tr_rc4_process(tr_rc4_ctx_t handle, void const* input, void* output, size_t length)
{
    TR_ASSERT(handle != NULL);

    if (length == 0)
    {
        return;
    }

    TR_ASSERT(input != NULL);
    TR_ASSERT(output != NULL);

    API(Arc4Process)(handle, output, input, length);
}

/***
****
***/

tr_dh_ctx_t tr_dh_new(uint8_t const* prime_num, size_t prime_num_length, uint8_t const* generator_num,
    size_t generator_num_length)
{
    TR_ASSERT(prime_num != NULL);
    TR_ASSERT(generator_num != NULL);

    struct tr_dh_ctx* handle = tr_new0(struct tr_dh_ctx, 1);

    API(InitDhKey)(&handle->dh);

    if (!check_result(API(DhSetKey)(&handle->dh, prime_num, prime_num_length, generator_num, generator_num_length)))
    {
        tr_free(handle);
        return NULL;
    }

    handle->key_length = prime_num_length;

    return handle;
}

void tr_dh_free(tr_dh_ctx_t raw_handle)
{
    struct tr_dh_ctx* handle = raw_handle;

    if (handle == NULL)
    {
        return;
    }

    API(FreeDhKey)(&handle->dh);
    tr_free(handle->private_key);
    tr_free(handle);
}

bool tr_dh_make_key(tr_dh_ctx_t raw_handle, size_t private_key_length UNUSED, uint8_t* public_key, size_t* public_key_length)
{
    TR_ASSERT(raw_handle != NULL);
    TR_ASSERT(public_key != NULL);

    struct tr_dh_ctx* handle = raw_handle;
    word32 my_private_key_length;
    word32 my_public_key_length;
    tr_lock* rng_lock = get_rng_lock();

    if (handle->private_key == NULL)
    {
        handle->private_key = tr_malloc(handle->key_length);
    }

    tr_lockLock(rng_lock);

    if (!check_result(API(DhGenerateKeyPair)(&handle->dh, get_rng(), handle->private_key, &my_private_key_length, public_key,
        &my_public_key_length)))
    {
        tr_lockUnlock(rng_lock);
        return false;
    }

    tr_lockUnlock(rng_lock);

    tr_dh_align_key(public_key, my_public_key_length, handle->key_length);

    handle->private_key_length = my_private_key_length;

    if (public_key_length != NULL)
    {
        *public_key_length = handle->key_length;
    }

    return true;
}

tr_dh_secret_t tr_dh_agree(tr_dh_ctx_t raw_handle, uint8_t const* other_public_key, size_t other_public_key_length)
{
    TR_ASSERT(raw_handle != NULL);
    TR_ASSERT(other_public_key != NULL);

    struct tr_dh_ctx* handle = raw_handle;
    struct tr_dh_secret* ret;
    word32 my_secret_key_length;

    ret = tr_dh_secret_new(handle->key_length);

    if (check_result(API(DhAgree)(&handle->dh, ret->key, &my_secret_key_length, handle->private_key, handle->private_key_length,
        other_public_key, other_public_key_length)))
    {
        tr_dh_secret_align(ret, my_secret_key_length);
    }
    else
    {
        tr_dh_secret_free(ret);
        ret = NULL;
    }

    return ret;
}

/***
****
***/

bool tr_rand_buffer(void* buffer, size_t length)
{
    TR_ASSERT(buffer != NULL);

    bool ret;
    tr_lock* rng_lock = get_rng_lock();

    tr_lockLock(rng_lock);
    ret = check_result(API(RNG_GenerateBlock)(get_rng(), buffer, length));
    tr_lockUnlock(rng_lock);

    return ret;
}
