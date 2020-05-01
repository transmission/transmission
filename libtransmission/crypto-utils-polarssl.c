/*
 * This file Copyright (C) 2014-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

/* *INDENT-OFF* */
#if defined(POLARSSL_IS_MBEDTLS)
#define API_HEADER(x) <mbedtls/x>
#define API(x) mbedtls_ ## x
#define API_VERSION_NUMBER MBEDTLS_VERSION_NUMBER
#else
#define API_HEADER(x) <polarssl/x>
#define API(x) x
#define API_VERSION_NUMBER POLARSSL_VERSION_NUMBER
#endif
/* *INDENT-ON* */

#include API_HEADER(arc4.h)
#include API_HEADER(base64.h)
#include API_HEADER(ctr_drbg.h)
#include API_HEADER(dhm.h)
#include API_HEADER(error.h)
#include API_HEADER(sha1.h)
#include API_HEADER(version.h)

#include "transmission.h"
#include "crypto-utils.h"
#include "log.h"
#include "platform.h"
#include "tr-assert.h"
#include "utils.h"

#define TR_CRYPTO_DH_SECRET_FALLBACK
#define TR_CRYPTO_X509_FALLBACK
#include "crypto-utils-fallback.c"

/***
****
***/

#define MY_NAME "tr_crypto_utils"

typedef API (ctr_drbg_context) api_ctr_drbg_context;
typedef API (sha1_context) api_sha1_context;
typedef API (arc4_context) api_arc4_context;
typedef API (dhm_context) api_dhm_context;

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

        tr_logAddMessage(file, line, TR_LOG_ERROR, MY_NAME, "PolarSSL error: %s", error_message);
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

static int my_rand(void* context UNUSED, unsigned char* buffer, size_t buffer_size)
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

        if (!check_result(API(ctr_drbg_seed)(&rng, &my_rand, NULL, NULL, 0)))
#else

        if (!check_result(API(ctr_drbg_init)(&rng, &my_rand, NULL, NULL, 0)))
#endif
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
    api_sha1_context* handle = tr_new0(api_sha1_context, 1);

#if API_VERSION_NUMBER >= 0x01030800
    API(sha1_init)(handle);
#endif

    API(sha1_starts)(handle);
    return handle;
}

bool tr_sha1_update(tr_sha1_ctx_t handle, void const* data, size_t data_length)
{
    TR_ASSERT(handle != NULL);

    if (data_length == 0)
    {
        return true;
    }

    TR_ASSERT(data != NULL);

    API(sha1_update)(handle, data, data_length);
    return true;
}

bool tr_sha1_final(tr_sha1_ctx_t handle, uint8_t* hash)
{
    if (hash != NULL)
    {
        TR_ASSERT(handle != NULL);

        API(sha1_finish)(handle, hash);
    }

#if API_VERSION_NUMBER >= 0x01030800
    API(sha1_free)(handle);
#endif

    tr_free(handle);
    return true;
}

/***
****
***/

tr_rc4_ctx_t tr_rc4_new(void)
{
    api_arc4_context* handle = tr_new0(api_arc4_context, 1);

#if API_VERSION_NUMBER >= 0x01030800
    API(arc4_init)(handle);
#endif

    return handle;
}

void tr_rc4_free(tr_rc4_ctx_t handle)
{
#if API_VERSION_NUMBER >= 0x01030800
    API(arc4_free)(handle);
#endif

    tr_free(handle);
}

void tr_rc4_set_key(tr_rc4_ctx_t handle, uint8_t const* key, size_t key_length)
{
    TR_ASSERT(handle != NULL);
    TR_ASSERT(key != NULL);

    API(arc4_setup)(handle, key, key_length);
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

    API(arc4_crypt)(handle, length, input, output);
}

/***
****
***/

tr_dh_ctx_t tr_dh_new(uint8_t const* prime_num, size_t prime_num_length, uint8_t const* generator_num,
    size_t generator_num_length)
{
    TR_ASSERT(prime_num != NULL);
    TR_ASSERT(generator_num != NULL);

    api_dhm_context* handle = tr_new0(api_dhm_context, 1);

#if API_VERSION_NUMBER >= 0x01030800
    API(dhm_init)(handle);
#endif

    if (!check_result(API(mpi_read_binary)(&handle->P, prime_num, prime_num_length)) ||
        !check_result(API(mpi_read_binary)(&handle->G, generator_num, generator_num_length)))
    {
        API(dhm_free)(handle);
        return NULL;
    }

    handle->len = prime_num_length;

    return handle;
}

void tr_dh_free(tr_dh_ctx_t handle)
{
    if (handle == NULL)
    {
        return;
    }

    API(dhm_free)(handle);
}

bool tr_dh_make_key(tr_dh_ctx_t raw_handle, size_t private_key_length, uint8_t* public_key, size_t* public_key_length)
{
    TR_ASSERT(raw_handle != NULL);
    TR_ASSERT(public_key != NULL);

    api_dhm_context* handle = raw_handle;

    if (public_key_length != NULL)
    {
        *public_key_length = handle->len;
    }

    return check_result(API(dhm_make_public)(handle, private_key_length, public_key, handle->len, my_rand, NULL));
}

tr_dh_secret_t tr_dh_agree(tr_dh_ctx_t raw_handle, uint8_t const* other_public_key, size_t other_public_key_length)
{
    TR_ASSERT(raw_handle != NULL);
    TR_ASSERT(other_public_key != NULL);

    api_dhm_context* handle = raw_handle;
    struct tr_dh_secret* ret;
    size_t secret_key_length;

    if (!check_result(API(dhm_read_public)(handle, other_public_key, other_public_key_length)))
    {
        return NULL;
    }

    ret = tr_dh_secret_new(handle->len);

    secret_key_length = handle->len;

#if API_VERSION_NUMBER >= 0x02000000

    if (!check_result(API(dhm_calc_secret)(handle, ret->key, secret_key_length, &secret_key_length, my_rand, NULL)))
#elif API_VERSION_NUMBER >= 0x01030000

    if (!check_result(API(dhm_calc_secret)(handle, ret->key, &secret_key_length, my_rand, NULL)))
#else

    if (!check_result(API(dhm_calc_secret)(handle, ret->key, &secret_key_length)))
#endif
    {
        tr_dh_secret_free(ret);
        return NULL;
    }

    tr_dh_secret_align(ret, secret_key_length);

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
    ret = check_result(API(ctr_drbg_random)(get_rng(), buffer, length));
    tr_lockUnlock(rng_lock);

    return ret;
}
