/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <stdarg.h>
#include <stdlib.h> /* abs(), srand(), rand() */
#include <string.h> /* memcpy(), memmove(), memset(), strcmp(), strlen() */

#include <b64/cdecode.h>
#include <b64/cencode.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "tr-assert.h"
#include "utils.h"

/***
****
***/

void tr_dh_align_key(uint8_t* key_buffer, size_t key_size, size_t buffer_size)
{
    TR_ASSERT(key_size <= buffer_size);

    /* DH can generate key sizes that are smaller than the size of
       key buffer with exponentially decreasing probability, in which case
       the msb's of key buffer need to be zeroed appropriately. */
    if (key_size < buffer_size)
    {
        size_t const offset = buffer_size - key_size;
        memmove(key_buffer + offset, key_buffer, key_size);
        memset(key_buffer, 0, offset);
    }
}

/***
****
***/

bool tr_sha1(uint8_t* hash, void const* data1, int data1_length, ...)
{
    tr_sha1_ctx_t sha;

    if ((sha = tr_sha1_init()) == NULL)
    {
        return false;
    }

    if (tr_sha1_update(sha, data1, data1_length))
    {
        va_list vl;
        void const* data;

        va_start(vl, data1_length);

        while ((data = va_arg(vl, void const*)) != NULL)
        {
            int const data_length = va_arg(vl, int);
            TR_ASSERT(data_length >= 0);

            if (!tr_sha1_update(sha, data, data_length))
            {
                break;
            }
        }

        va_end(vl);

        /* did we reach the end of argument list? */
        if (data == NULL)
        {
            return tr_sha1_final(sha, hash);
        }
    }

    tr_sha1_final(sha, NULL);
    return false;
}

/***
****
***/

int tr_rand_int(int upper_bound)
{
    TR_ASSERT(upper_bound > 0);

    unsigned int noise;

    if (tr_rand_buffer(&noise, sizeof(noise)))
    {
        return noise % upper_bound;
    }

    /* fall back to a weaker implementation... */
    return tr_rand_int_weak(upper_bound);
}

int tr_rand_int_weak(int upper_bound)
{
    TR_ASSERT(upper_bound > 0);

    static bool init = false;

    if (!init)
    {
        srand(tr_time_msec());
        init = true;
    }

    return rand() % upper_bound;
}

/***
****
***/

char* tr_ssha1(char const* plain_text)
{
    TR_ASSERT(plain_text != NULL);

    enum
    {
        saltval_len = 8,
        salter_len = 64
    };

    static char const* salter =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "./";

    unsigned char salt[saltval_len];
    uint8_t sha[SHA_DIGEST_LENGTH];
    char buf[2 * SHA_DIGEST_LENGTH + saltval_len + 2];

    tr_rand_buffer(salt, saltval_len);

    for (size_t i = 0; i < saltval_len; ++i)
    {
        salt[i] = salter[salt[i] % salter_len];
    }

    tr_sha1(sha, plain_text, (int)strlen(plain_text), salt, saltval_len, NULL);
    tr_sha1_to_hex(&buf[1], sha);
    memcpy(&buf[1 + 2 * SHA_DIGEST_LENGTH], &salt, saltval_len);
    buf[1 + 2 * SHA_DIGEST_LENGTH + saltval_len] = '\0';
    buf[0] = '{'; /* signal that this is a hash. this makes saving/restoring easier */

    return tr_strdup(buf);
}

bool tr_ssha1_matches(char const* ssha1, char const* plain_text)
{
    TR_ASSERT(ssha1 != NULL);
    TR_ASSERT(plain_text != NULL);

    size_t const brace_len = 1;
    size_t const brace_and_hash_len = brace_len + 2 * SHA_DIGEST_LENGTH;

    size_t const source_len = strlen(ssha1);

    if (source_len < brace_and_hash_len || ssha1[0] != '{')
    {
        return false;
    }

    /* extract the salt */
    char const* const salt = ssha1 + brace_and_hash_len;
    size_t const salt_len = source_len - brace_and_hash_len;

    uint8_t buf[SHA_DIGEST_LENGTH * 2 + 1];

    /* hash pass + salt */
    tr_sha1(buf, plain_text, (int)strlen(plain_text), salt, (int)salt_len, NULL);
    tr_sha1_to_hex((char*)buf, buf);

    return strncmp(ssha1 + brace_len, (char const*)buf, SHA_DIGEST_LENGTH * 2) == 0;
}

/***
****
***/

void* tr_base64_encode(void const* input, size_t input_length, size_t* output_length)
{
    char* ret;

    if (input != NULL)
    {
        if (input_length != 0)
        {
            size_t ret_length = 4 * ((input_length + 2) / 3);
            base64_encodestate state;

#ifdef USE_SYSTEM_B64
            /* Additional space is needed for newlines if we're using unpatched libb64 */
            ret_length += ret_length / 72 + 1;
#endif

            ret = tr_new(char, ret_length + 8);

            base64_init_encodestate(&state);
            ret_length = base64_encode_block(input, input_length, ret, &state);
            ret_length += base64_encode_blockend(ret + ret_length, &state);

            if (output_length != NULL)
            {
                *output_length = ret_length;
            }

            ret[ret_length] = '\0';

            return ret;
        }
        else
        {
            ret = tr_strdup("");
        }
    }
    else
    {
        ret = NULL;
    }

    if (output_length != NULL)
    {
        *output_length = 0;
    }

    return ret;
}

void* tr_base64_encode_str(char const* input, size_t* output_length)
{
    return tr_base64_encode(input, input == NULL ? 0 : strlen(input), output_length);
}

void* tr_base64_decode(void const* input, size_t input_length, size_t* output_length)
{
    char* ret;

    if (input != NULL)
    {
        if (input_length != 0)
        {
            size_t ret_length = input_length / 4 * 3;
            base64_decodestate state;

            ret = tr_new(char, ret_length + 8);

            base64_init_decodestate(&state);
            ret_length = base64_decode_block(input, input_length, ret, &state);

            if (output_length != NULL)
            {
                *output_length = ret_length;
            }

            ret[ret_length] = '\0';

            return ret;
        }
        else
        {
            ret = tr_strdup("");
        }
    }
    else
    {
        ret = NULL;
    }

    if (output_length != NULL)
    {
        *output_length = 0;
    }

    return ret;
}

void* tr_base64_decode_str(char const* input, size_t* output_length)
{
    return tr_base64_decode(input, input == NULL ? 0 : strlen(input), output_length);
}
