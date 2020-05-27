/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <limits.h> /* INT_MAX */
#include <math.h> /* sqrt() */
#include <string.h> /* strlen() */
#include <stdlib.h> /* setenv(), unsetenv() */

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, NULL)
#endif

#include "transmission.h"
#include "ConvertUTF.h" /* tr_utf8_validate*/
#include "platform.h"
#include "crypto-utils.h" /* tr_rand_int_weak */
#include "utils.h"
#include "web.h"

#define SPEED_TEST 0

#if SPEED_TEST
#define VERBOSE
#endif

#include "libtransmission-test.h"

static int test_strip_positional_args(void)
{
    char const* in;
    char const* out;
    char const* expected;

    in = "Hello %1$s foo %2$.*f";
    expected = "Hello %s foo %.*f";
    out = tr_strip_positional_args(in);
    check_str(out, ==, expected);

    in = "Hello %1$'d foo %2$'f";
    expected = "Hello %d foo %f";
    out = tr_strip_positional_args(in);
    check_str(out, ==, expected);

    return 0;
}

static int test_strstrip(void)
{
    char* in;
    char* out;

    /* strstrip */
    in = tr_strdup("   test    ");
    out = tr_strstrip(in);
    check_ptr(in, ==, out);
    check_str(out, ==, "test");
    tr_free(in);

    /* strstrip */
    in = tr_strdup(" test test ");
    out = tr_strstrip(in);
    check_ptr(in, ==, out);
    check_str(out, ==, "test test");
    tr_free(in);

    /* strstrip */
    in = tr_strdup("test");
    out = tr_strstrip(in);
    check_ptr(in, ==, out);
    check_str(out, ==, "test");
    tr_free(in);

    return 0;
}

static int test_strjoin(void)
{
    char* out;

    char const* in1[] = { "one", "two" };
    out = tr_strjoin(in1, 2, ", ");
    check_str(out, ==, "one, two");
    tr_free(out);

    char const* in2[] = { "hello" };
    out = tr_strjoin(in2, 1, "###");
    check_str(out, ==, "hello");
    tr_free(out);

    char const* in3[] = { "a", "b", "ccc", "d", "eeeee" };
    out = tr_strjoin(in3, 5, " ");
    check_str(out, ==, "a b ccc d eeeee");
    tr_free(out);

    char const* in4[] = { "7", "ate", "9" };
    out = tr_strjoin(in4, 3, "");
    check_str(out, ==, "7ate9");
    tr_free(out);

    char const** in5;
    out = tr_strjoin(in5, 0, "a");
    check_str(out, ==, "");
    tr_free(out);

    return 0;
}

static int test_buildpath(void)
{
    char* out;

    out = tr_buildPath("foo", "bar", NULL);
    check_str(out, ==, "foo" TR_PATH_DELIMITER_STR "bar");
    tr_free(out);

    out = tr_buildPath("", "foo", "bar", NULL);
    check_str(out, ==, TR_PATH_DELIMITER_STR "foo" TR_PATH_DELIMITER_STR "bar");
    tr_free(out);

    return 0;
}

static int test_utf8(void)
{
    char const* in;
    char* out;

    in = "hello world";
    out = tr_utf8clean(in, TR_BAD_SIZE);
    check_str(out, ==, in);
    tr_free(out);

    in = "hello world";
    out = tr_utf8clean(in, 5);
    check_str(out, ==, "hello");
    tr_free(out);

    /* this version is not utf-8 (but cp866) */
    in = "\x92\xE0\xE3\xA4\xAD\xAE \xA1\xEB\xE2\xEC \x81\xAE\xA3\xAE\xAC";
    out = tr_utf8clean(in, 17);
    check_ptr(out, !=, NULL);
    check(strlen(out) == 17 || strlen(out) == 33);
    check(tr_utf8_validate(out, TR_BAD_SIZE, NULL));
    tr_free(out);

    /* same string, but utf-8 clean */
    in = "Трудно быть Богом";
    out = tr_utf8clean(in, TR_BAD_SIZE);
    check_ptr(out, !=, NULL);
    check(tr_utf8_validate(out, TR_BAD_SIZE, NULL));
    check_str(out, ==, in);
    tr_free(out);

    in = "\xF4\x00\x81\x82";
    out = tr_utf8clean(in, 4);
    check_ptr(out, !=, NULL);
    check(strlen(out) == 1 || strlen(out) == 2);
    check(tr_utf8_validate(out, TR_BAD_SIZE, NULL));
    tr_free(out);

    in = "\xF4\x33\x81\x82";
    out = tr_utf8clean(in, 4);
    check_ptr(out, !=, NULL);
    check(strlen(out) == 4 || strlen(out) == 7);
    check(tr_utf8_validate(out, TR_BAD_SIZE, NULL));
    tr_free(out);

    return 0;
}

static int test_numbers(void)
{
    int count;
    int* numbers;

    numbers = tr_parseNumberRange("1-10,13,16-19", TR_BAD_SIZE, &count);
    check_int(count, ==, 15);
    check_int(numbers[0], ==, 1);
    check_int(numbers[5], ==, 6);
    check_int(numbers[9], ==, 10);
    check_int(numbers[10], ==, 13);
    check_int(numbers[11], ==, 16);
    check_int(numbers[14], ==, 19);
    tr_free(numbers);

    numbers = tr_parseNumberRange("1-5,3-7,2-6", TR_BAD_SIZE, &count);
    check_int(count, ==, 7);
    check_ptr(numbers, !=, NULL);

    for (int i = 0; i < count; ++i)
    {
        check_int(numbers[i], ==, i + 1);
    }

    tr_free(numbers);

    numbers = tr_parseNumberRange("1-Hello", TR_BAD_SIZE, &count);
    check_int(count, ==, 0);
    check_ptr(numbers, ==, NULL);

    numbers = tr_parseNumberRange("1-", TR_BAD_SIZE, &count);
    check_int(count, ==, 0);
    check_ptr(numbers, ==, NULL);

    numbers = tr_parseNumberRange("Hello", TR_BAD_SIZE, &count);
    check_int(count, ==, 0);
    check_ptr(numbers, ==, NULL);

    return 0;
}

static int compareInts(void const* va, void const* vb)
{
    int const a = *(int const*)va;
    int const b = *(int const*)vb;
    return a - b;
}

static int test_lowerbound(void)
{
    int const A[] = { 1, 2, 3, 3, 3, 5, 8 };
    int const expected_pos[] = { 0, 1, 2, 5, 5, 6, 6, 6, 7, 7 };
    bool const expected_exact[] = { true, true, true, false, true, false, false, true, false, false };
    int const N = TR_N_ELEMENTS(A);

    for (int i = 1; i <= 10; i++)
    {
        bool exact;
        int const pos = tr_lowerBound(&i, A, N, sizeof(int), compareInts, &exact);

#if 0

        fprintf(stderr, "searching for %d. ", i);
        fprintf(stderr, "result: index = %d, ", pos);

        if (pos != N)
        {
            fprintf(stderr, "A[%d] == %d\n", pos, A[pos]);
        }
        else
        {
            fprintf(stderr, "which is off the end.\n");
        }

#endif

        check_int(pos, ==, expected_pos[i - 1]);
        check_int(exact, ==, expected_exact[i - 1]);
    }

    return 0;
}

static int test_quickFindFirst_Iteration(size_t const k, size_t const n, int* buf, int range)
{
    int highest_low;
    int lowest_high;

    /* populate buf with random ints */
    for (size_t i = 0; i < n; ++i)
    {
        buf[i] = tr_rand_int_weak(range);
    }

    /* find the best k */
    tr_quickfindFirstK(buf, n, sizeof(int), compareInts, k);

    /* confirm that the smallest K ints are in the first slots K slots in buf */

    highest_low = INT_MIN;

    for (size_t i = 0; i < k; ++i)
    {
        if (highest_low < buf[i])
        {
            highest_low = buf[i];
        }
    }

    lowest_high = INT_MAX;

    for (size_t i = k; i < n; ++i)
    {
        if (lowest_high > buf[i])
        {
            lowest_high = buf[i];
        }
    }

    check_int(highest_low, <=, lowest_high);

    return 0;
}

static int test_quickfindFirst(void)
{
    size_t const k = 10;
    size_t const n = 100;
    size_t const n_trials = 1000;
    int* buf = tr_new(int, n);

    for (size_t i = 0; i < n_trials; ++i)
    {
        check_int(test_quickFindFirst_Iteration(k, n, buf, 100), ==, 0);
    }

    tr_free(buf);
    return 0;
}

static int test_memmem(void)
{
    char const haystack[12] = "abcabcabcabc";
    char const needle[3] = "cab";

    check_ptr(tr_memmem(haystack, sizeof(haystack), haystack, sizeof(haystack)), ==, haystack);
    check_ptr(tr_memmem(haystack, sizeof(haystack), needle, sizeof(needle)), ==, haystack + 2);
    check_ptr(tr_memmem(needle, sizeof(needle), haystack, sizeof(haystack)), ==, NULL);

    return 0;
}

static int test_hex(void)
{
    char hex1[41];
    char hex2[41];
    uint8_t binary[20];

    memcpy(hex1, "fb5ef5507427b17e04b69cef31fa3379b456735a", 41);
    tr_hex_to_binary(hex1, binary, 20);
    tr_binary_to_hex(binary, hex2, 20);
    check_str(hex1, ==, hex2);

    return 0;
}

static int test_array(void)
{
    size_t array[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    size_t n = TR_N_ELEMENTS(array);

    tr_removeElementFromArray(array, 5U, sizeof(size_t), n);
    --n;

    for (size_t i = 0; i < n; ++i)
    {
        check_int(array[i], ==, i < 5 ? i : i + 1);
    }

    tr_removeElementFromArray(array, 0U, sizeof(size_t), n);
    --n;

    for (size_t i = 0; i < n; ++i)
    {
        check_int(array[i], ==, i < 4 ? i + 1 : i + 2);
    }

    tr_removeElementFromArray(array, n - 1, sizeof(size_t), n);
    --n;

    for (size_t i = 0; i < n; ++i)
    {
        check_int(array[i], ==, i < 4 ? i + 1 : i + 2);
    }

    return 0;
}

static int test_url(void)
{
    int port;
    char* scheme;
    char* host;
    char* path;
    char* str;
    char const* url;

    url = "http://1";
    check(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    check_str(scheme, ==, "http");
    check_str(host, ==, "1");
    check_str(path, ==, "/");
    check_int(port, ==, 80);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);

    url = "http://www.some-tracker.org/some/path";
    check(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    check_str(scheme, ==, "http");
    check_str(host, ==, "www.some-tracker.org");
    check_str(path, ==, "/some/path");
    check_int(port, ==, 80);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);

    url = "http://www.some-tracker.org:80/some/path";
    check(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    check_str(scheme, ==, "http");
    check_str(host, ==, "www.some-tracker.org");
    check_str(path, ==, "/some/path");
    check_int(port, ==, 80);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);

    url = "http%3A%2F%2Fwww.example.com%2F~user%2F%3Ftest%3D1%26test1%3D2";
    str = tr_http_unescape(url, strlen(url));
    check_str(str, ==, "http://www.example.com/~user/?test=1&test1=2");
    tr_free(str);

    return 0;
}

static int test_truncd(void)
{
    char buf[32];
    double const nan = sqrt(-1);

    tr_snprintf(buf, sizeof(buf), "%.2f%%", 99.999);
    check_str(buf, ==, "100.00%");

    tr_snprintf(buf, sizeof(buf), "%.2f%%", tr_truncd(99.999, 2));
    check_str(buf, ==, "99.99%");

    tr_snprintf(buf, sizeof(buf), "%.4f", tr_truncd(403650.656250, 4));
    check_str(buf, ==, "403650.6562");

    tr_snprintf(buf, sizeof(buf), "%.2f", tr_truncd(2.15, 2));
    check_str(buf, ==, "2.15");

    tr_snprintf(buf, sizeof(buf), "%.2f", tr_truncd(2.05, 2));
    check_str(buf, ==, "2.05");

    tr_snprintf(buf, sizeof(buf), "%.2f", tr_truncd(3.3333, 2));
    check_str(buf, ==, "3.33");

    tr_snprintf(buf, sizeof(buf), "%.0f", tr_truncd(3.3333, 0));
    check_str(buf, ==, "3");

    tr_snprintf(buf, sizeof(buf), "%.0f", tr_truncd(3.9999, 0));
    check_str(buf, ==, "3");

#if !(defined(_MSC_VER) || (defined(__MINGW32__) && defined(__MSVCRT__)))
    /* FIXME: MSCVRT behaves differently in case of nan */
    tr_snprintf(buf, sizeof(buf), "%.2f", tr_truncd(nan, 2));
    check(strstr(buf, "nan") != NULL || strstr(buf, "NaN") != NULL);
#else
    (void)nan;
#endif

    return 0;
}

static char* test_strdup_printf_valist(char const* fmt, ...) TR_GNUC_PRINTF(1, 2);

static char* test_strdup_printf_valist(char const* fmt, ...)
{
    va_list args;
    char* ret;

    va_start(args, fmt);
    ret = tr_strdup_vprintf(fmt, args);
    va_end(args);

    return ret;
}

static int test_strdup_printf(void)
{
    char* s;
    char* s2;
    char* s3;

    s = tr_strdup_printf("%s", "test");
    check_str(s, ==, "test");
    tr_free(s);

    s = tr_strdup_printf("%d %s %c %u", -1, "0", '1', 2);
    check_str(s, ==, "-1 0 1 2");
    tr_free(s);

    s3 = tr_malloc0(4098);
    memset(s3, '-', 4097);
    s3[2047] = 't';
    s3[2048] = 'e';
    s3[2049] = 's';
    s3[2050] = 't';

    s2 = tr_malloc0(4096);
    memset(s2, '-', 4095);
    s2[2047] = '%';
    s2[2048] = 's';

    s = tr_strdup_printf(s2, "test");
    check_str(s, ==, s3);
    tr_free(s);

    tr_free(s2);

    s = tr_strdup_printf("%s", s3);
    check_str(s, ==, s3);
    tr_free(s);

    tr_free(s3);

    s = test_strdup_printf_valist("\n-%s-%s-%s-\n", "\r", "\t", "\b");
    check_str(s, ==, "\n-\r-\t-\b-\n");
    tr_free(s);

    return 0;
}

static int test_env(void)
{
    char const* test_key = "TR_TEST_ENV";
    int x;
    char* s;

    unsetenv(test_key);

    check(!tr_env_key_exists(test_key));
    x = tr_env_get_int(test_key, 123);
    check_int(x, ==, 123);
    s = tr_env_get_string(test_key, NULL);
    check_str(s, ==, NULL);
    s = tr_env_get_string(test_key, "a");
    check_str(s, ==, "a");
    tr_free(s);

    setenv(test_key, "", 1);

    check(tr_env_key_exists(test_key));
    x = tr_env_get_int(test_key, 456);
    check_int(x, ==, 456);
    s = tr_env_get_string(test_key, NULL);
    check_str(s, ==, "");
    tr_free(s);
    s = tr_env_get_string(test_key, "b");
    check_str(s, ==, "");
    tr_free(s);

    setenv(test_key, "135", 1);

    check(tr_env_key_exists(test_key));
    x = tr_env_get_int(test_key, 789);
    check_int(x, ==, 135);
    s = tr_env_get_string(test_key, NULL);
    check_str(s, ==, "135");
    tr_free(s);
    s = tr_env_get_string(test_key, "c");
    check_str(s, ==, "135");
    tr_free(s);

    return 0;
}

int main(void)
{
    testFunc const tests[] =
    {
        test_array,
        test_buildpath,
        test_hex,
        test_lowerbound,
        test_quickfindFirst,
        test_memmem,
        test_numbers,
        test_strip_positional_args,
        test_strdup_printf,
        test_strstrip,
        test_strjoin,
        test_truncd,
        test_url,
        test_utf8,
        test_env
    };

    return runTests(tests, NUM_TESTS(tests));
}
