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

#include "libtransmission-test.h"

#include "crypto-test-ref.h"

static int test_torrent_hash(void)
{
    tr_crypto a;
    uint8_t hash[SHA_DIGEST_LENGTH];

    for (uint8_t i = 0; i < SHA_DIGEST_LENGTH; ++i)
    {
        hash[i] = i;
    }

    tr_cryptoConstruct(&a, NULL, true);

    check(!tr_cryptoHasTorrentHash(&a));
    check_ptr(tr_cryptoGetTorrentHash(&a), ==, NULL);

    tr_cryptoSetTorrentHash(&a, hash);
    check(tr_cryptoHasTorrentHash(&a));
    check_ptr(tr_cryptoGetTorrentHash(&a), !=, NULL);
    check_mem(tr_cryptoGetTorrentHash(&a), ==, hash, SHA_DIGEST_LENGTH);

    tr_cryptoDestruct(&a);

    for (uint8_t i = 0; i < SHA_DIGEST_LENGTH; ++i)
    {
        hash[i] = i + 1;
    }

    tr_cryptoConstruct(&a, hash, false);

    check(tr_cryptoHasTorrentHash(&a));
    check_ptr(tr_cryptoGetTorrentHash(&a), !=, NULL);
    check_mem(tr_cryptoGetTorrentHash(&a), ==, hash, SHA_DIGEST_LENGTH);

    tr_cryptoSetTorrentHash(&a, NULL);
    check(!tr_cryptoHasTorrentHash(&a));
    check_ptr(tr_cryptoGetTorrentHash(&a), ==, NULL);

    tr_cryptoDestruct(&a);

    return 0;
}

static int test_encrypt_decrypt(void)
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
    check(tr_cryptoComputeSecret(&a, tr_cryptoGetMyPublicKey_(&b, &public_key_length)));
    check(tr_cryptoComputeSecret_(&b, tr_cryptoGetMyPublicKey(&a, &public_key_length)));

    tr_cryptoEncryptInit(&a);
    tr_cryptoEncrypt(&a, sizeof(test1), test1, buf11);
    tr_cryptoDecryptInit_(&b);
    tr_cryptoDecrypt_(&b, sizeof(test1), buf11, buf12);
    check_str(buf12, ==, test1);

    tr_cryptoEncryptInit_(&b);
    tr_cryptoEncrypt_(&b, sizeof(test2), test2, buf21);
    tr_cryptoDecryptInit(&a);
    tr_cryptoDecrypt(&a, sizeof(test2), buf21, buf22);
    check_str(buf22, ==, test2);

    tr_cryptoDestruct_(&b);
    tr_cryptoDestruct(&a);

    return 0;
}

static int test_sha1(void)
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    uint8_t hash_[SHA_DIGEST_LENGTH];

    check(tr_sha1(hash, "test", 4, NULL));
    check(tr_sha1_(hash_, "test", 4, NULL));
    check_mem(hash, ==, "\xa9\x4a\x8f\xe5\xcc\xb1\x9b\xa6\x1c\x4c\x08\x73\xd3\x91\xe9\x87\x98\x2f\xbb\xd3", SHA_DIGEST_LENGTH);
    check_mem(hash, ==, hash_, SHA_DIGEST_LENGTH);

    check(tr_sha1(hash, "1", 1, "22", 2, "333", 3, NULL));
    check(tr_sha1_(hash_, "1", 1, "22", 2, "333", 3, NULL));
    check_mem(hash, ==, "\x1f\x74\x64\x8e\x50\xa6\xa6\x70\x8e\xc5\x4a\xb3\x27\xa1\x63\xd5\x53\x6b\x7c\xed", SHA_DIGEST_LENGTH);
    check_mem(hash, ==, hash_, SHA_DIGEST_LENGTH);

    return 0;
}

static int test_ssha1(void)
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

#define HASH_COUNT (4 * 1024)

    for (size_t i = 0; i < TR_N_ELEMENTS(test_data); ++i)
    {
        char* const phrase = tr_strdup(test_data[i].plain_text);
        char** hashes = tr_new(char*, HASH_COUNT);

        check(tr_ssha1_matches(test_data[i].ssha1, phrase));
        check(tr_ssha1_matches_(test_data[i].ssha1, phrase));

        for (size_t j = 0; j < HASH_COUNT; ++j)
        {
            hashes[j] = j % 2 == 0 ? tr_ssha1(phrase) : tr_ssha1_(phrase);

            check_ptr(hashes[j], !=, NULL);

            /* phrase matches each of generated hashes */
            check(tr_ssha1_matches(hashes[j], phrase));
            check(tr_ssha1_matches_(hashes[j], phrase));
        }

        for (size_t j = 0; j < HASH_COUNT; ++j)
        {
            /* all hashes are different */
            for (size_t k = 0; k < HASH_COUNT; ++k)
            {
                if (k != j)
                {
                    check_str(hashes[j], !=, hashes[k]);
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
            check(!tr_ssha1_matches(hashes[j], phrase));
            check(!tr_ssha1_matches_(hashes[j], phrase));
        }

        for (size_t j = 0; j < HASH_COUNT; ++j)
        {
            tr_free(hashes[j]);
        }

        tr_free(hashes);
        tr_free(phrase);
    }

#undef HASH_COUNT

    /* should work with different salt lengths as well */
    check(tr_ssha1_matches("{a94a8fe5ccb19ba61c4c0873d391e987982fbbd3", "test"));
    check(tr_ssha1_matches("{d209a21d3bc4f8fc4f8faf347e69f3def597eb170pySy4ai1ZPMjeU1", "test"));

    return 0;
}

static int test_random(void)
{
    /* test that tr_rand_int() stays in-bounds */
    for (int i = 0; i < 100000; ++i)
    {
        int const val = tr_rand_int(100);
        check_int(val, >=, 0);
        check_int(val, <, 100);
    }

    return 0;
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

static int test_base64(void)
{
    size_t len;
    char* in;
    char* out;

    out = tr_base64_encode_str("YOYO!", &len);
    check_uint(len, ==, strlen(out));
    check(base64_eq("WU9ZTyE=", out));
    in = tr_base64_decode_str(out, &len);
    check_uint(len, ==, 5);
    check_str(in, ==, "YOYO!");
    tr_free(in);
    tr_free(out);

    out = tr_base64_encode("", 0, &len);
    check_uint(len, ==, 0);
    check_str(out, ==, "");
    tr_free(out);
    out = tr_base64_decode("", 0, &len);
    check_uint(len, ==, 0);
    check_str(out, ==, "");
    tr_free(out);

    out = tr_base64_encode(NULL, 0, &len);
    check_uint(len, ==, 0);
    check_str(out, ==, NULL);
    out = tr_base64_decode(NULL, 0, &len);
    check_uint(len, ==, 0);
    check_str(out, ==, NULL);

#define MAX_BUF_SIZE 1024

    for (size_t i = 1; i <= MAX_BUF_SIZE; ++i)
    {
        char buf[MAX_BUF_SIZE + 1];

        for (size_t j = 0; j < i; ++j)
        {
            buf[j] = (char)tr_rand_int_weak(256);
        }

        out = tr_base64_encode(buf, i, &len);
        check_uint(len, ==, strlen(out));
        in = tr_base64_decode(out, len, &len);
        check_uint(len, ==, i);
        check_mem(in, ==, buf, len);
        tr_free(in);
        tr_free(out);

        for (size_t j = 0; j < i; ++j)
        {
            buf[j] = (char)(1 + tr_rand_int_weak(255));
        }

        buf[i] = '\0';

        out = tr_base64_encode_str(buf, &len);
        check_uint(len, ==, strlen(out));
        in = tr_base64_decode_str(out, &len);
        check_uint(len, ==, i);
        check_str(buf, ==, in);
        tr_free(in);
        tr_free(out);
    }

#undef MAX_BUF_SIZE

    return 0;
}

int main(void)
{
    testFunc const tests[] =
    {
        test_torrent_hash,
        test_encrypt_decrypt,
        test_sha1,
        test_ssha1,
        test_random,
        test_base64
    };

    return runTests(tests, NUM_TESTS(tests));
}
