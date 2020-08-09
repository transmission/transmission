/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

#include "transmission.h"
#include "ConvertUTF.h" // tr_utf8_validate()
#include "platform.h"
#include "crypto-utils.h" // tr_rand_int_weak()
#include "utils.h"
#include "web.h" // tr_http_unescape()

#include "test-fixtures.h"

#include <algorithm>
#include <array>
#include <cmath> // sqrt()
#include <cstdlib> // setenv(), unsetenv()
#include <string>

using ::libtransmission::test::makeString;

using UtilsTest = ::testing::Test;

TEST_F(UtilsTest, trStripPositionalArgs)
{
    auto const* in = "Hello %1$s foo %2$.*f";
    auto const* expected = "Hello %s foo %.*f";
    auto const* out = tr_strip_positional_args(in);
    EXPECT_STREQ(expected, out);

    in = "Hello %1$'d foo %2$'f";
    expected = "Hello %d foo %f";
    out = tr_strip_positional_args(in);
    EXPECT_STREQ(expected, out);
}

TEST_F(UtilsTest, trStrstrip)
{
    auto* in = tr_strdup("   test    ");
    auto* out = tr_strstrip(in);
    EXPECT_EQ(in, out);
    EXPECT_STREQ("test", out);
    tr_free(in);

    in = tr_strdup(" test test ");
    out = tr_strstrip(in);
    EXPECT_EQ(in, out);
    EXPECT_STREQ("test test", out);
    tr_free(in);

    /* strstrip */
    in = tr_strdup("test");
    out = tr_strstrip(in);
    EXPECT_EQ(in, out);
    EXPECT_STREQ("test", out);
    tr_free(in);
}

TEST_F(UtilsTest, trStrjoin)
{
    auto const in1 = std::array<char const*, 2>{ "one", "two" };
    auto out = makeString(tr_strjoin(in1.data(), in1.size(), ", "));
    EXPECT_EQ("one, two", out);

    auto const in2 = std::array<char const*, 1>{ "hello" };
    out = makeString(tr_strjoin(in2.data(), in2.size(), "###"));
    EXPECT_EQ("hello", out);

    auto const in3 = std::array<char const*, 5>{ "a", "b", "ccc", "d", "eeeee" };
    out = makeString(tr_strjoin(in3.data(), in3.size(), " "));
    EXPECT_EQ("a b ccc d eeeee", out);

    auto const in4 = std::array<char const*, 3>{ "7", "ate", "9" };
    out = makeString(tr_strjoin(in4.data(), in4.size(), ""));
    EXPECT_EQ("7ate9", out);

    char const** in5 = nullptr;
    out = makeString(tr_strjoin(in5, 0, "a"));
    EXPECT_EQ("", out);
}

TEST_F(UtilsTest, trBuildpath)
{
    auto out = makeString(tr_buildPath("foo", "bar", nullptr));
    EXPECT_EQ("foo" TR_PATH_DELIMITER_STR "bar", out);

    out = makeString(tr_buildPath("", "foo", "bar", nullptr));
    EXPECT_EQ(TR_PATH_DELIMITER_STR "foo" TR_PATH_DELIMITER_STR "bar", out);
}

TEST_F(UtilsTest, trUtf8clean)
{
    auto const* in = "hello world";
    auto out = makeString(tr_utf8clean(in, TR_BAD_SIZE));
    EXPECT_EQ(in, out);

    in = "hello world";
    out = makeString(tr_utf8clean(in, 5));
    EXPECT_EQ("hello", out);

    // this version is not utf-8 (but cp866)
    in = "\x92\xE0\xE3\xA4\xAD\xAE \xA1\xEB\xE2\xEC \x81\xAE\xA3\xAE\xAC";
    out = makeString(tr_utf8clean(in, 17));
    EXPECT_TRUE(out.size() == 17 || out.size() == 33);
    EXPECT_TRUE(tr_utf8_validate(out.c_str(), out.size(), nullptr));

    // same string, but utf-8 clean
    in = "Трудно быть Богом";
    out = makeString(tr_utf8clean(in, TR_BAD_SIZE));
    EXPECT_NE(nullptr, out.data());
    EXPECT_TRUE(tr_utf8_validate(out.c_str(), out.size(), nullptr));
    EXPECT_EQ(in, out);

    in = "\xF4\x00\x81\x82";
    out = makeString(tr_utf8clean(in, 4));
    EXPECT_NE(nullptr, out.data());
    EXPECT_TRUE(out.size() == 1 || out.size() == 2);
    EXPECT_TRUE(tr_utf8_validate(out.c_str(), out.size(), nullptr));

    in = "\xF4\x33\x81\x82";
    out = makeString(tr_utf8clean(in, 4));
    EXPECT_NE(nullptr, out.data());
    EXPECT_TRUE(out.size() == 4 || out.size() == 7);
    EXPECT_TRUE(tr_utf8_validate(out.c_str(), out.size(), nullptr));
}

TEST_F(UtilsTest, numbers)
{
    auto count = int{};
    auto* numbers = tr_parseNumberRange("1-10,13,16-19", TR_BAD_SIZE, &count);
    EXPECT_EQ(15, count);
    EXPECT_EQ(1, numbers[0]);
    EXPECT_EQ(6, numbers[5]);
    EXPECT_EQ(10, numbers[9]);
    EXPECT_EQ(13, numbers[10]);
    EXPECT_EQ(16, numbers[11]);
    EXPECT_EQ(19, numbers[14]);
    tr_free(numbers);

    numbers = tr_parseNumberRange("1-5,3-7,2-6", TR_BAD_SIZE, &count);
    EXPECT_EQ(7, count);
    EXPECT_NE(nullptr, numbers);
    for (int i = 0; i < count; ++i)
    {
        EXPECT_EQ(i + 1, numbers[i]);
    }

    tr_free(numbers);

    numbers = tr_parseNumberRange("1-Hello", TR_BAD_SIZE, &count);
    EXPECT_EQ(0, count);
    EXPECT_EQ(nullptr, numbers);

    numbers = tr_parseNumberRange("1-", TR_BAD_SIZE, &count);
    EXPECT_EQ(0, count);
    EXPECT_EQ(nullptr, numbers);

    numbers = tr_parseNumberRange("Hello", TR_BAD_SIZE, &count);
    EXPECT_EQ(0, count);
    EXPECT_EQ(nullptr, numbers);
}

namespace
{

int compareInts(void const* va, void const* vb) noexcept
{
    auto const a = *static_cast<int const*>(va);
    auto const b = *static_cast<int const*>(vb);
    return a - b;
}

} // unnamed namespace

TEST_F(UtilsTest, lowerbound)
{
    auto constexpr A = std::array<int, 7>{ 1, 2, 3, 3, 3, 5, 8 };
    auto const expected_pos = std::array<int, 10>{ 0, 1, 2, 5, 5, 6, 6, 6, 7, 7 };
    auto const expected_exact = std::array<bool, 10>{ true, true, true, false, true, false, false, true, false, false };

    for (int i = 1; i <= 10; i++)
    {
        bool exact;
        auto const pos = tr_lowerBound(&i, A.data(), A.size(), sizeof(int), compareInts, &exact);
        EXPECT_EQ(expected_pos[i - 1], pos);
        EXPECT_EQ(expected_exact[i - 1], exact);
    }
}

TEST_F(UtilsTest, trQuickfindfirstk)
{
    auto const run_test = [](size_t const k, size_t const n, int* buf, int range)
        {
            // populate buf with random ints
            std::generate(buf, buf + n, [range]() { return tr_rand_int_weak(range); });

            // find the best k
            tr_quickfindFirstK(buf, n, sizeof(int), compareInts, k);

            // confirm that the smallest K ints are in the first slots K slots in buf
            auto const* highest_low = std::max_element(buf, buf + k);
            auto const* lowest_high = std::min_element(buf + k, buf + n);
            EXPECT_LE(highest_low, lowest_high);
        };

    auto constexpr K = size_t{ 10 };
    auto constexpr NumTrials = size_t{ 1000 };
    auto buf = std::array<int, 100>{};
    for (auto i = 0; i != NumTrials; ++i)
    {
        run_test(K, buf.size(), buf.data(), 100);
    }
}

TEST_F(UtilsTest, trMemmem)
{
    auto const haystack = std::string { "abcabcabcabc" };
    auto const needle = std::string { "cab" };

    EXPECT_EQ(haystack, tr_memmem(haystack.data(), haystack.size(), haystack.data(), haystack.size()));
    EXPECT_EQ(haystack.substr(2), tr_memmem(haystack.data(), haystack.size(), needle.data(), needle.size()));
    EXPECT_EQ(nullptr, tr_memmem(needle.data(), needle.size(), haystack.data(), haystack.size()));
}

TEST_F(UtilsTest, trBinaryHex)
{
    auto const hex_in = std::string { "fb5ef5507427b17e04b69cef31fa3379b456735a" };

    auto binary = std::array<uint8_t, SHA_DIGEST_LENGTH>{};
    tr_hex_to_binary(hex_in.data(), binary.data(), hex_in.size() / 2);

    auto hex_out = std::array<uint8_t, SHA_DIGEST_LENGTH*2 + 1>{};
    tr_binary_to_hex(binary.data(), hex_out.data(), 20);
    EXPECT_EQ(hex_in, reinterpret_cast<char const*>(hex_out.data()));
}

TEST_F(UtilsTest, array)
{
    auto array = std::array<size_t, 10>{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    auto n = array.size();

    tr_removeElementFromArray(array.data(), 5U, sizeof(size_t), n);
    --n;

    for (size_t i = 0; i < n; ++i)
    {
        EXPECT_EQ(array[i], i < 5 ? i : i + 1);
    }

    tr_removeElementFromArray(array.data(), 0U, sizeof(size_t), n);
    --n;

    for (size_t i = 0; i < n; ++i)
    {
        EXPECT_EQ(array[i], i < 4 ? i + 1 : i + 2);
    }

    tr_removeElementFromArray(array.data(), n - 1, sizeof(size_t), n);
    --n;

    for (size_t i = 0; i < n; ++i)
    {
        EXPECT_EQ(array[i], i < 4 ? i + 1 : i + 2);
    }
}

TEST_F(UtilsTest, url)
{
    auto const* url = "http://1";
    int port;
    char* scheme;
    char* host;
    char* path;
    EXPECT_TRUE(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    EXPECT_STREQ("http", scheme);
    EXPECT_STREQ("1", host);
    EXPECT_STREQ("/", path);
    EXPECT_EQ(80, port);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);

    url = "http://www.some-tracker.org/some/path";
    EXPECT_TRUE(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    EXPECT_STREQ("http", scheme);
    EXPECT_STREQ("www.some-tracker.org", host);
    EXPECT_STREQ("/some/path", path);
    EXPECT_EQ(80, port);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);

    url = "http://www.some-tracker.org:8080/some/path";
    EXPECT_TRUE(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    EXPECT_STREQ("http", scheme);
    EXPECT_STREQ("www.some-tracker.org", host);
    EXPECT_STREQ("/some/path", path);
    EXPECT_EQ(8080, port);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);
}

TEST_F(UtilsTest, trHttpUnescape)
{
    auto const url = std::string { "http%3A%2F%2Fwww.example.com%2F~user%2F%3Ftest%3D1%26test1%3D2" };
    auto str = makeString(tr_http_unescape(url.data(), url.size()));
    EXPECT_EQ("http://www.example.com/~user/?test=1&test1=2", str);
}

TEST_F(UtilsTest, truncd)
{
    auto buf = std::array<char, 32>{};

    tr_snprintf(buf.data(), buf.size(), "%.2f%%", 99.999);
    EXPECT_STREQ("100.00%", buf.data());

    tr_snprintf(buf.data(), buf.size(), "%.2f%%", tr_truncd(99.999, 2));
    EXPECT_STREQ("99.99%", buf.data());

    tr_snprintf(buf.data(), buf.size(), "%.4f", tr_truncd(403650.656250, 4));
    EXPECT_STREQ("403650.6562", buf.data());

    tr_snprintf(buf.data(), buf.size(), "%.2f", tr_truncd(2.15, 2));
    EXPECT_STREQ("2.15", buf.data());

    tr_snprintf(buf.data(), buf.size(), "%.2f", tr_truncd(2.05, 2));
    EXPECT_STREQ("2.05", buf.data());

    tr_snprintf(buf.data(), buf.size(), "%.2f", tr_truncd(3.3333, 2));
    EXPECT_STREQ("3.33", buf.data());

    tr_snprintf(buf.data(), buf.size(), "%.0f", tr_truncd(3.3333, 0));
    EXPECT_STREQ("3", buf.data());

    tr_snprintf(buf.data(), buf.size(), "%.0f", tr_truncd(3.9999, 0));
    EXPECT_STREQ("3", buf.data());

#if !(defined(_MSC_VER) || (defined(__MINGW32__) && defined(__MSVCRT__)))
    /* FIXME: MSCVRT behaves differently in case of nan */
    auto const nan = sqrt(-1.0);
    tr_snprintf(buf.data(), buf.size(), "%.2f", tr_truncd(nan, 2));
    EXPECT_TRUE(strstr(buf.data(), "nan") != nullptr || strstr(buf.data(), "NaN") != nullptr);
#endif
}

namespace
{

char* testStrdupPrintfValist(char const* fmt, ...) TR_GNUC_PRINTF(1, 2);

char* testStrdupPrintfValist(char const* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    auto* ret = tr_strdup_vprintf(fmt, args);
    va_end(args);
    return ret;
}

} // unnamed namespace

TEST_F(UtilsTest, trStrdupVprintf)
{
    // NOLINTNEXTLINE(cert-dcl50-cpp)
    auto s = makeString(testStrdupPrintfValist("\n-%s-%s-%s-\n", "\r", "\t", "\b"));
    EXPECT_EQ("\n-\r-\t-\b-\n", s);
}

TEST_F(UtilsTest, trStrdupPrintfFmtS)
{
    auto s = makeString(tr_strdup_printf("%s", "test"));
    EXPECT_EQ("test", s);
}

TEST_F(UtilsTest, trStrdupPrintf)
{
    auto s = makeString(tr_strdup_printf("%d %s %c %u", -1, "0", '1', 2));
    EXPECT_EQ("-1 0 1 2", s);

    auto* s3 = reinterpret_cast<char*>(tr_malloc0(4098));
    memset(s3, '-', 4097);
    s3[2047] = 't';
    s3[2048] = 'e';
    s3[2049] = 's';
    s3[2050] = 't';

    auto* s2 = reinterpret_cast<char*>(tr_malloc0(4096));
    memset(s2, '-', 4095);
    s2[2047] = '%';
    s2[2048] = 's';

    // NOLINTNEXTLINE(clang-diagnostic-format-nonliteral)
    s = makeString(tr_strdup_printf(s2, "test"));
    EXPECT_EQ(s3, s);

    tr_free(s2);

    s = makeString(tr_strdup_printf("%s", s3));
    EXPECT_EQ(s3, s);

    tr_free(s3);
}

TEST_F(UtilsTest, env)
{
    char const* test_key = "TR_TEST_ENV";

    unsetenv(test_key);

    EXPECT_FALSE(tr_env_key_exists(test_key));
    EXPECT_EQ(123, tr_env_get_int(test_key, 123));
    EXPECT_EQ(nullptr, tr_env_get_string(test_key, nullptr));
    auto s = makeString(tr_env_get_string(test_key, "a"));
    EXPECT_EQ("a", s);

    setenv(test_key, "", 1);

    EXPECT_TRUE(tr_env_key_exists(test_key));
    EXPECT_EQ(456, tr_env_get_int(test_key, 456));
    s = makeString(tr_env_get_string(test_key, nullptr));
    EXPECT_EQ("", s);
    s = makeString(tr_env_get_string(test_key, "b"));
    EXPECT_EQ("", s);

    setenv(test_key, "135", 1);

    EXPECT_TRUE(tr_env_key_exists(test_key));
    EXPECT_EQ(135, tr_env_get_int(test_key, 789));
    s = makeString(tr_env_get_string(test_key, nullptr));
    EXPECT_EQ("135", s);
    s = makeString(tr_env_get_string(test_key, "c"));
    EXPECT_EQ("135", s);
}
