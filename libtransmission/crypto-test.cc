/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h>

#include "transmission.h"
#include "crypto.h"
#include "crypto-utils.h"
#include "utils.h"

#include "crypto-test-ref.h"

#include "gtest/gtest.h"

TEST(Crypto, torrent_hash)
{
    tr_crypto a;
    uint8_t hash[SHA_DIGEST_LENGTH];

    for (uint8_t i = 0; i < SHA_DIGEST_LENGTH; ++i)
    {
        hash[i] = i;
    }

    tr_cryptoConstruct(&a, nullptr, true);

    EXPECT_FALSE(tr_cryptoHasTorrentHash(&a));
    EXPECT_EQ(nullptr, tr_cryptoGetTorrentHash(&a));

    tr_cryptoSetTorrentHash(&a, hash);
    EXPECT_TRUE(tr_cryptoHasTorrentHash(&a));
    EXPECT_NE(nullptr, tr_cryptoGetTorrentHash(&a));
    EXPECT_EQ(0, memcmp(tr_cryptoGetTorrentHash(&a), hash, SHA_DIGEST_LENGTH));

    tr_cryptoDestruct(&a);

    for (uint8_t i = 0; i < SHA_DIGEST_LENGTH; ++i)
    {
        hash[i] = i + 1;
    }

    tr_cryptoConstruct(&a, hash, false);

    EXPECT_TRUE(tr_cryptoHasTorrentHash(&a));
    EXPECT_NE(nullptr, tr_cryptoGetTorrentHash(&a));
    EXPECT_EQ(0, memcmp(tr_cryptoGetTorrentHash(&a), hash, SHA_DIGEST_LENGTH));

    tr_cryptoSetTorrentHash(&a, nullptr);
    EXPECT_FALSE(tr_cryptoHasTorrentHash(&a));
    EXPECT_EQ(nullptr, tr_cryptoGetTorrentHash(&a));

    tr_cryptoDestruct(&a);
}

TEST(Crypto, encrypt_decrypt)
{
    tr_crypto a;
    tr_crypto_ b;
    uint8_t hash[SHA_DIGEST_LENGTH];
    char const test1[] = { "test1" };
    char buf11[sizeof(test1)];
    char buf12[sizeof(test1)];
    char const test2[] = { "@#)C$@)#(*%bvkdjfhwbc039bc4603756VB3)" };
    char buf21[sizeof(test2)];
    char buf22[sizeof(test2)];
    int public_key_length;

    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i)
    {
        hash[i] = (uint8_t)i;
    }

    tr_cryptoConstruct(&a, hash, false);
    tr_cryptoConstruct_(&b, hash, true);
    EXPECT_TRUE(tr_cryptoComputeSecret(&a, tr_cryptoGetMyPublicKey_(&b, &public_key_length)));
    EXPECT_TRUE(tr_cryptoComputeSecret_(&b, tr_cryptoGetMyPublicKey(&a, &public_key_length)));

    tr_cryptoEncryptInit(&a);
    tr_cryptoEncrypt(&a, sizeof(test1), test1, buf11);
    tr_cryptoDecryptInit_(&b);
    tr_cryptoDecrypt_(&b, sizeof(test1), buf11, buf12);
    EXPECT_STREQ(buf12, test1);

    tr_cryptoEncryptInit_(&b);
    tr_cryptoEncrypt_(&b, sizeof(test2), test2, buf21);
    tr_cryptoDecryptInit(&a);
    tr_cryptoDecrypt(&a, sizeof(test2), buf21, buf22);
    EXPECT_STREQ(buf22, test2);

    tr_cryptoDestruct_(&b);
    tr_cryptoDestruct(&a);
}

TEST(Crypto, sha1)
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    uint8_t hash_[SHA_DIGEST_LENGTH];

    EXPECT_TRUE(tr_sha1(hash, "test", 4, nullptr));
    EXPECT_TRUE(tr_sha1_(hash_, "test", 4, nullptr));
    EXPECT_EQ(0, memcmp(hash, "\xa9\x4a\x8f\xe5\xcc\xb1\x9b\xa6\x1c\x4c\x08\x73\xd3\x91\xe9\x87\x98\x2f\xbb\xd3", SHA_DIGEST_LENGTH));
    EXPECT_EQ(0, memcmp(hash, hash_, SHA_DIGEST_LENGTH));

    EXPECT_TRUE(tr_sha1(hash, "1", 1, "22", 2, "333", 3, nullptr));
    EXPECT_TRUE(tr_sha1_(hash_, "1", 1, "22", 2, "333", 3, nullptr));
    EXPECT_EQ(0, memcmp(hash, "\x1f\x74\x64\x8e\x50\xa6\xa6\x70\x8e\xc5\x4a\xb3\x27\xa1\x63\xd5\x53\x6b\x7c\xed", SHA_DIGEST_LENGTH));
    EXPECT_EQ(0, memcmp(hash, hash_, SHA_DIGEST_LENGTH));
}

TEST(Crypto, ssha1)
{
    struct
    {
        char const* const plain_text;
        char const* const ssha1;
    }
    test_data[] =
    {
        { "test", "{15ad0621b259a84d24dcd4e75b09004e98a3627bAMbyRHJy" },
        { "QNY)(*#$B)!_X$B !_B#($^!)*&$%CV!#)&$C!@$(P*)", "{10e2d7acbb104d970514a147cd16d51dfa40fb3c0OSwJtOL" }
    };

    auto constexpr HASH_COUNT = size_t { 4 * 1024 };

    for (size_t i = 0; i < TR_N_ELEMENTS(test_data); ++i)
    {
        char* const phrase = tr_strdup(test_data[i].plain_text);
        char** hashes = tr_new(char*, HASH_COUNT);

        EXPECT_TRUE(tr_ssha1_matches(test_data[i].ssha1, phrase));
        EXPECT_TRUE(tr_ssha1_matches_(test_data[i].ssha1, phrase));

        for (size_t j = 0; j < HASH_COUNT; ++j)
        {
            hashes[j] = j % 2 == 0 ? tr_ssha1(phrase) : tr_ssha1_(phrase);
            EXPECT_NE(nullptr, hashes[j]);

            /* phrase matches each of generated hashes */
            EXPECT_TRUE(tr_ssha1_matches(hashes[j], phrase));
            EXPECT_TRUE(tr_ssha1_matches_(hashes[j], phrase));
        }

        for (size_t j = 0; j < HASH_COUNT; ++j)
        {
            /* all hashes are different */
            for (size_t k = 0; k < HASH_COUNT; ++k)
            {
                if (k != j)
                {
                    EXPECT_STRNE(hashes[j], hashes[k]);
                }
            }
        }

        /* exchange two first chars */
        phrase[0] ^= phrase[1];
        phrase[1] ^= phrase[0];
        phrase[0] ^= phrase[1];

        for (size_t j = 0; j < HASH_COUNT; ++j)
        {
            /* changed phrase doesn't match the hashes */
            EXPECT_FALSE(tr_ssha1_matches(hashes[j], phrase));
            EXPECT_FALSE(tr_ssha1_matches_(hashes[j], phrase));
        }

        for (size_t j = 0; j < HASH_COUNT; ++j)
        {
            tr_free(hashes[j]);
        }

        tr_free(hashes);
        tr_free(phrase);
    }

    /* should work with different salt lengths as well */
    EXPECT_TRUE(tr_ssha1_matches("{a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", "test"));
    EXPECT_TRUE(tr_ssha1_matches("{d209a21d3bc4f8fc4f8faf347e69f3def597eb170pySy4ai1ZPMjeU1", "test"));
}

TEST(Crypto, random)
{
    /* test that tr_rand_int() stays in-bounds */
    for (int i = 0; i < 100000; ++i)
    {
        int const val = tr_rand_int(100);
        EXPECT_LE(0, val);
        EXPECT_LT(val, 100);
    }
}

static bool base64_eq(char const* a, char const* b)
{
    for (;; ++a, ++b)
    {
        while (*a == '\r' || *a == '\n')
        {
            ++a;
        }

        while (*b == '\r' || *b == '\n')
        {
            ++b;
        }

        if (*a == '\0' || *b == '\0' || *a != *b)
        {
            break;
        }
    }

    return *a == *b;
}

TEST(Crypto, base64)
{
    size_t len;
    char* in;
    char* out;

    out = static_cast<char*>(tr_base64_encode_str("YOYO!", &len));
    EXPECT_EQ(strlen(out), len);
    EXPECT_TRUE(base64_eq("WU9ZTyE=", out));
    in = static_cast<char*>(tr_base64_decode_str(out, &len));
    EXPECT_EQ(5, len);
    EXPECT_STREQ("YOYO!", in);
    tr_free(in);
    tr_free(out);

    out = static_cast<char*>(tr_base64_encode("", 0, &len));
    EXPECT_EQ(0, len);
    EXPECT_STREQ("", out);
    tr_free(out);
    out = static_cast<char*>(tr_base64_decode("", 0, &len));
    EXPECT_EQ(0, len);
    EXPECT_STREQ("", out);
    tr_free(out);

    out = static_cast<char*>(tr_base64_encode(nullptr, 0, &len));
    EXPECT_EQ(0, len);
    EXPECT_EQ(nullptr, out);
    out = static_cast<char*>(tr_base64_decode(nullptr, 0, &len));
    EXPECT_EQ(0, len);
    EXPECT_EQ(nullptr, out);

    static auto constexpr MAX_BUF_SIZE = size_t { 1024 };
    for (size_t i = 1; i <= MAX_BUF_SIZE; ++i)
    {
        char buf[MAX_BUF_SIZE + 1];

        for (size_t j = 0; j < i; ++j)
        {
            buf[j] = (char)tr_rand_int_weak(256);
        }

        out = static_cast<char*>(tr_base64_encode(buf, i, &len));
        EXPECT_EQ(strlen(out), len);
        in = static_cast<char*>(tr_base64_decode(out, len, &len));
        EXPECT_EQ(i, len);
        EXPECT_EQ(0, memcmp(in, buf, len));
        tr_free(in);
        tr_free(out);

        for (size_t j = 0; j < i; ++j)
        {
            buf[j] = (char)(1 + tr_rand_int_weak(255));
        }

        buf[i] = '\0';

        out = static_cast<char*>(tr_base64_encode_str(buf, &len));
        EXPECT_EQ(strlen(out), len);
        in = static_cast<char*>(tr_base64_decode_str(out, &len));
        EXPECT_EQ(i, len);
        EXPECT_STREQ(buf, in);
        tr_free(in);
        tr_free(out);
    }
}
