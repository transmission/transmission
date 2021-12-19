/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cstdarg>
#include <cstring> /* memcpy(), memmove(), memset(), strcmp(), strlen() */
#include <random> /* random_device, mt19937, uniform_int_distribution*/
#include <string_view>

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

using namespace std::literals;

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

int tr_rand_int(int upper_bound)
{
    TR_ASSERT(upper_bound > 0);

    if (unsigned int noise = 0; tr_rand_buffer(&noise, sizeof(noise)))
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

static auto constexpr DigestStringSize = TR_SHA1_DIGEST_LEN * 2;
static auto constexpr SaltedPrefix = std::string_view{ "{" };

std::string tr_ssha1(std::string_view plaintext)
{
    auto constexpr Salter = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ./"sv;
    static_assert(std::size(Salter) == 64);

    // build an array of random Salter chars
    auto constexpr SaltSize = size_t{ 8 };
    auto salt = std::array<char, SaltSize>{};
    tr_rand_buffer(std::data(salt), std::size(salt));
    std::transform(
        std::begin(salt),
        std::end(salt),
        std::begin(salt),
        [&Salter](auto ch) { return Salter[ch % std::size(Salter)]; });

    // build a sha1 digest of the original content and the salt
    auto const digest = tr_sha1(plaintext, salt);

    // convert it to a string.
    // prepend with a '{' marker so the codebase can identify salted strings

    auto constexpr BufSize = std::size(SaltedPrefix) + DigestStringSize + SaltSize;
    auto buf = std::array<char, BufSize>{};
    auto* walk = std::begin(buf);
    walk = std::copy(std::begin(SaltedPrefix), std::end(SaltedPrefix), walk);
    walk = tr_sha1_to_string(*digest, walk);
    walk = std::copy(std::begin(salt), std::end(salt), walk);
    TR_ASSERT(walk == std::begin(buf) + std::size(buf));

    return std::string{ std::data(buf), std::size(buf) };
}

bool tr_ssha1_test(std::string_view text)
{
    return tr_strvStartsWith(text, SaltedPrefix) && std::size(text) >= std::size(SaltedPrefix) + DigestStringSize;
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

    /* hash pass + salt */
    auto const salted_digest = tr_sha1(plain_text, std::string_view{ salt, salt_len });
    if (!salted_digest)
    {
        return false;
    }

    char strbuf[SHA_DIGEST_LENGTH * 2 + 1];
    tr_sha1_to_string(*salted_digest, strbuf);
    return strncmp(std::data(ssha1) + brace_len, strbuf, SHA_DIGEST_LENGTH * 2) == 0;
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
    static char constexpr Hex[] = "0123456789abcdef";

    auto const* input = static_cast<uint8_t const*>(vinput);
    auto* output = static_cast<char*>(voutput);

    /* go from back to front to allow for in-place conversion */
    input += byte_length;
    output += byte_length * 2;

    *output = '\0';

    while (byte_length-- > 0)
    {
        unsigned int const val = *(--input);
        *(--output) = Hex[val & 0xf];
        *(--output) = Hex[val >> 4];
    }
}

std::string tr_sha1_to_string(tr_sha1_digest_t const& digest)
{
    auto str = std::string(std::size(digest) * 2, '?');
    tr_binary_to_hex(std::data(digest), std::data(str), std::size(digest));
    return str;
}

char* tr_sha1_to_string(tr_sha1_digest_t const& digest, char* strbuf)
{
    tr_binary_to_hex(std::data(digest), strbuf, std::size(digest));
    return strbuf + (std::size(digest) * 2);
}

static void tr_hex_to_binary(void const* vinput, void* voutput, size_t byte_length)
{
    static char constexpr Hex[] = "0123456789abcdef";

    auto const* input = static_cast<uint8_t const*>(vinput);
    auto* output = static_cast<uint8_t*>(voutput);

    for (size_t i = 0; i < byte_length; ++i)
    {
        int const hi = strchr(Hex, tolower(*input++)) - Hex;
        int const lo = strchr(Hex, tolower(*input++)) - Hex;
        *output++ = (uint8_t)((hi << 4) | lo);
    }
}

tr_sha1_digest_t tr_sha1_from_string(char const* hex)
{
    auto digest = tr_sha1_digest_t{};
    tr_hex_to_binary(hex, std::data(digest), SHA_DIGEST_LENGTH);
    return digest;
}
