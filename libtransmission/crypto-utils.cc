/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstdarg>
#include <cstring> /* memcpy(), memmove(), memset(), strcmp(), strlen() */
#include <random> /* random_device, mt19937, uniform_int_distribution*/

#include <arc4.h>

extern "C"
{
#include <b64/cdecode.h>
#include <b64/cencode.h>
}

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
    tr_sha1_ctx_t sha = tr_sha1_init();
    if (sha == nullptr)
    {
        return false;
    }

    if (tr_sha1_update(sha, data1, data1_length))
    {
        va_list vl;
        va_start(vl, data1_length);

        void const* data = nullptr;
        while ((data = va_arg(vl, void const*)) != nullptr)
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
        if (data == nullptr)
        {
            return tr_sha1_final(sha, hash);
        }
    }

    tr_sha1_final(sha, nullptr);
    return false;
}

/***
****
***/

int tr_rand_int(int upper_bound)
{
    TR_ASSERT(upper_bound > 0);

    unsigned int noise = 0;
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

    thread_local auto random_engine = std::mt19937{ std::random_device{}() };
    using distribution_type = std::uniform_int_distribution<>;
    thread_local distribution_type distribution;

    // Upper bound is inclusive in std::uniform_int_distribution.
    return distribution(random_engine, distribution_type::param_type{ 0, upper_bound - 1 });
}

/***
****
***/

std::string tr_ssha1(std::string_view plain_text)
{
    auto constexpr SaltvalLen = int{ 8 };
    auto constexpr SalterLen = int{ 64 };

    static char const* salter =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "./";

    unsigned char salt[SaltvalLen];
    uint8_t sha[SHA_DIGEST_LENGTH];
    char buf[2 * SHA_DIGEST_LENGTH + SaltvalLen + 2];

    tr_rand_buffer(salt, SaltvalLen);

    for (auto& ch : salt)
    {
        ch = salter[ch % SalterLen];
    }

    tr_sha1(sha, std::data(plain_text), std::size(plain_text), salt, SaltvalLen, nullptr);
    tr_sha1_to_hex(&buf[1], sha);
    memcpy(&buf[1 + 2 * SHA_DIGEST_LENGTH], &salt, SaltvalLen);
    buf[1 + 2 * SHA_DIGEST_LENGTH + SaltvalLen] = '\0';
    buf[0] = '{'; /* signal that this is a hash. this makes saving/restoring easier */

    return std::string{ buf };
}

bool tr_ssha1_matches(std::string_view ssha1, std::string_view plain_text)
{
    size_t const brace_len = 1;
    size_t const brace_and_hash_len = brace_len + 2 * SHA_DIGEST_LENGTH;

    size_t const source_len = std::size(ssha1);

    if (source_len < brace_and_hash_len || ssha1[0] != '{')
    {
        return false;
    }

    /* extract the salt */
    char const* const salt = std::data(ssha1) + brace_and_hash_len;
    size_t const salt_len = source_len - brace_and_hash_len;

    uint8_t buf[SHA_DIGEST_LENGTH * 2 + 1];

    /* hash pass + salt */
    tr_sha1(buf, std::data(plain_text), std::size(plain_text), salt, (int)salt_len, nullptr);
    tr_sha1_to_hex((char*)buf, buf);

    return strncmp(std::data(ssha1) + brace_len, (char const*)buf, SHA_DIGEST_LENGTH * 2) == 0;
}

/***
****
***/

void* tr_base64_encode(void const* input, size_t input_length, size_t* output_length)
{
    char* ret = nullptr;

    if (input != nullptr)
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
            ret_length = base64_encode_block(static_cast<char const*>(input), input_length, ret, &state);
            ret_length += base64_encode_blockend(ret + ret_length, &state);

            if (output_length != nullptr)
            {
                *output_length = ret_length;
            }

            ret[ret_length] = '\0';

            return ret;
        }

        ret = tr_strdup("");
    }

    if (output_length != nullptr)
    {
        *output_length = 0;
    }

    return ret;
}

void* tr_base64_encode_str(char const* input, size_t* output_length)
{
    return tr_base64_encode(input, input == nullptr ? 0 : strlen(input), output_length);
}

void* tr_base64_decode(void const* input, size_t input_length, size_t* output_length)
{
    char* ret = nullptr;

    if (input != nullptr)
    {
        if (input_length != 0)
        {
            size_t ret_length = input_length / 4 * 3;
            base64_decodestate state;

            ret = tr_new(char, ret_length + 8);

            base64_init_decodestate(&state);
            ret_length = base64_decode_block(static_cast<char const*>(input), input_length, ret, &state);

            if (output_length != nullptr)
            {
                *output_length = ret_length;
            }

            ret[ret_length] = '\0';

            return ret;
        }

        ret = tr_strdup("");
    }

    if (output_length != nullptr)
    {
        *output_length = 0;
    }

    return ret;
}

void* tr_base64_decode_str(char const* input, size_t* output_length)
{
    return tr_base64_decode(input, input == nullptr ? 0 : strlen(input), output_length);
}

std::string tr_base64_decode_str(std::string_view input)
{
    auto len = size_t{};
    auto* buf = tr_base64_decode(std::data(input), std::size(input), &len);
    auto str = std::string{ reinterpret_cast<char const*>(buf), len };
    tr_free(buf);
    return str;
}

/***
****
***/

static void tr_binary_to_hex(void const* vinput, void* voutput, size_t byte_length)
{
    static char const hex[] = "0123456789abcdef";

    auto const* input = static_cast<uint8_t const*>(vinput);
    auto* output = static_cast<char*>(voutput);

    /* go from back to front to allow for in-place conversion */
    input += byte_length;
    output += byte_length * 2;

    *output = '\0';

    while (byte_length-- > 0)
    {
        unsigned int const val = *(--input);
        *(--output) = hex[val & 0xf];
        *(--output) = hex[val >> 4];
    }
}

void tr_sha1_to_hex(void* hex, void const* sha1)
{
    tr_binary_to_hex(sha1, hex, SHA_DIGEST_LENGTH);
}

static void tr_hex_to_binary(void const* vinput, void* voutput, size_t byte_length)
{
    static char const hex[] = "0123456789abcdef";

    auto const* input = static_cast<uint8_t const*>(vinput);
    auto* output = static_cast<uint8_t*>(voutput);

    for (size_t i = 0; i < byte_length; ++i)
    {
        int const hi = strchr(hex, tolower(*input++)) - hex;
        int const lo = strchr(hex, tolower(*input++)) - hex;
        *output++ = (uint8_t)((hi << 4) | lo);
    }
}

void tr_hex_to_sha1(void* sha1, void const* hex)
{
    tr_hex_to_binary(hex, sha1, SHA_DIGEST_LENGTH);
}
