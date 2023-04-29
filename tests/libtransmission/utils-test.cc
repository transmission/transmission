// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cmath> // sqrt()
#include <cstdlib> // setenv(), unsetenv()
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

#include <libtransmission/transmission.h>

#include <libtransmission/crypto-utils.h> // tr_rand_int()
#include <libtransmission/platform.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>

#include "test-fixtures.h"

using UtilsTest = ::testing::Test;
using namespace std::literals;

TEST_F(UtilsTest, trStrvContains)
{
    EXPECT_FALSE(tr_strvContains("a test is this"sv, "TEST"sv));
    EXPECT_FALSE(tr_strvContains("test"sv, "testt"sv));
    EXPECT_FALSE(tr_strvContains("test"sv, "this is a test"sv));
    EXPECT_TRUE(tr_strvContains(" test "sv, "tes"sv));
    EXPECT_TRUE(tr_strvContains(" test"sv, "test"sv));
    EXPECT_TRUE(tr_strvContains("a test is this"sv, "test"sv));
    EXPECT_TRUE(tr_strvContains("test "sv, "test"sv));
    EXPECT_TRUE(tr_strvContains("test"sv, ""sv));
    EXPECT_TRUE(tr_strvContains("test"sv, "t"sv));
    EXPECT_TRUE(tr_strvContains("test"sv, "te"sv));
    EXPECT_TRUE(tr_strvContains("test"sv, "test"sv));
    EXPECT_TRUE(tr_strvContains("this is a test"sv, "test"sv));
    EXPECT_TRUE(tr_strvContains(""sv, ""sv));
}

TEST_F(UtilsTest, trStrvStartsWith)
{
    EXPECT_FALSE(tr_strvStartsWith(""sv, "this is a string"sv));
    EXPECT_FALSE(tr_strvStartsWith("this is a strin"sv, "this is a string"sv));
    EXPECT_FALSE(tr_strvStartsWith("this is a strin"sv, "this is a string"sv));
    EXPECT_FALSE(tr_strvStartsWith("this is a string"sv, " his is a string"sv));
    EXPECT_FALSE(tr_strvStartsWith("this is a string"sv, "his is a string"sv));
    EXPECT_FALSE(tr_strvStartsWith("this is a string"sv, "string"sv));
    EXPECT_TRUE(tr_strvStartsWith(""sv, ""sv));
    EXPECT_TRUE(tr_strvStartsWith("this is a string"sv, ""sv));
    EXPECT_TRUE(tr_strvStartsWith("this is a string"sv, "this "sv));
    EXPECT_TRUE(tr_strvStartsWith("this is a string"sv, "this is"sv));
    EXPECT_TRUE(tr_strvStartsWith("this is a string"sv, "this"sv));
}

TEST_F(UtilsTest, trStrvEndsWith)
{
    EXPECT_FALSE(tr_strvEndsWith(""sv, "string"sv));
    EXPECT_FALSE(tr_strvEndsWith("this is a string"sv, "alphabet"sv));
    EXPECT_FALSE(tr_strvEndsWith("this is a string"sv, "strin"sv));
    EXPECT_FALSE(tr_strvEndsWith("this is a string"sv, "this is"sv));
    EXPECT_FALSE(tr_strvEndsWith("this is a string"sv, "this"sv));
    EXPECT_FALSE(tr_strvEndsWith("tring"sv, "string"sv));
    EXPECT_TRUE(tr_strvEndsWith(""sv, ""sv));
    EXPECT_TRUE(tr_strvEndsWith("this is a string"sv, " string"sv));
    EXPECT_TRUE(tr_strvEndsWith("this is a string"sv, ""sv));
    EXPECT_TRUE(tr_strvEndsWith("this is a string"sv, "a string"sv));
    EXPECT_TRUE(tr_strvEndsWith("this is a string"sv, "g"sv));
    EXPECT_TRUE(tr_strvEndsWith("this is a string"sv, "string"sv));
}

TEST_F(UtilsTest, trStrvSep)
{
    auto constexpr Delim = ',';

    auto sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1"sv, tr_strvSep(&sv, Delim));
    EXPECT_EQ("token2"sv, tr_strvSep(&sv, Delim));
    EXPECT_EQ("token3"sv, tr_strvSep(&sv, Delim));
    EXPECT_EQ(""sv, tr_strvSep(&sv, Delim));

    sv = " token1,token2"sv;
    EXPECT_EQ(" token1"sv, tr_strvSep(&sv, Delim));
    EXPECT_EQ("token2"sv, tr_strvSep(&sv, Delim));

    sv = "token1;token2"sv;
    EXPECT_EQ("token1;token2"sv, tr_strvSep(&sv, Delim));
    EXPECT_EQ(""sv, tr_strvSep(&sv, Delim));

    sv = ""sv;
    EXPECT_EQ(""sv, tr_strvSep(&sv, Delim));
}

TEST_F(UtilsTest, trStrvStrip)
{
    EXPECT_EQ(""sv, tr_strvStrip("              "sv));
    EXPECT_EQ("test test"sv, tr_strvStrip("    test test     "sv));
    EXPECT_EQ("test"sv, tr_strvStrip("   test     "sv));
    EXPECT_EQ("test"sv, tr_strvStrip("   test "sv));
    EXPECT_EQ("test"sv, tr_strvStrip(" test       "sv));
    EXPECT_EQ("test"sv, tr_strvStrip(" test "sv));
    EXPECT_EQ("test"sv, tr_strvStrip("test"sv));
}

TEST_F(UtilsTest, strvReplaceInvalid)
{
    auto in = "hello world"sv;
    auto out = tr_strv_replace_invalid(in);
    EXPECT_EQ(in, out);

    in = "hello world"sv;
    out = tr_strv_replace_invalid(in.substr(0, 5));
    EXPECT_EQ("hello"sv, out);

    // this version is not utf-8 (but cp866)
    in = "\x92\xE0\xE3\xA4\xAD\xAE \xA1\xEB\xE2\xEC \x81\xAE\xA3\xAE\xAC"sv;
    out = tr_strv_replace_invalid(in, '?');
    EXPECT_EQ(17U, std::size(out));
    EXPECT_EQ(out, tr_strv_replace_invalid(out));

    // same string, but utf-8 clean
    in = "Трудно быть Богом"sv;
    out = tr_strv_replace_invalid(in);
    EXPECT_NE(0U, std::size(out));
    EXPECT_EQ(out, tr_strv_replace_invalid(out));
    EXPECT_EQ(in, out);

    // https://trac.transmissionbt.com/ticket/6064
    // This was a fuzzer-generated string that crashed Transmission.
    // Even invalid strings shouldn't cause a crash.
    in = "\xF4\x00\x81\x82"sv;
    out = tr_strv_replace_invalid(in);
    EXPECT_NE(0U, std::size(out));
    EXPECT_EQ(out, tr_strv_replace_invalid(out));

    in = "\xF4\x33\x81\x82"sv;
    out = tr_strv_replace_invalid(in, '?');
    EXPECT_NE(nullptr, out.data());
    EXPECT_EQ(4U, std::size(out));
    EXPECT_EQ(out, tr_strv_replace_invalid(out));
}

TEST_F(UtilsTest, strvConvertUtf8Fuzz)
{
    auto buf = std::vector<char>{};
    for (size_t i = 0; i < 1000; ++i)
    {
        buf.resize(tr_rand_int(4096U));
        tr_rand_buffer(std::data(buf), std::size(buf));
        auto const out = tr_strv_convert_utf8({ std::data(buf), std::size(buf) });
        EXPECT_EQ(out, tr_strv_convert_utf8(out));
    }
}

TEST_F(UtilsTest, trParseNumberRange)
{
    auto const tostring = [](std::vector<int> const& v)
    {
        std::stringstream ss;
        for (auto const& i : v)
        {
            ss << i << ' ';
        }
        return ss.str();
    };

    auto numbers = tr_parseNumberRange("1-10,13,16-19"sv);
    EXPECT_EQ(std::string("1 2 3 4 5 6 7 8 9 10 13 16 17 18 19 "), tostring(numbers));

    numbers = tr_parseNumberRange("1-5,3-7,2-6"sv);
    EXPECT_EQ(std::string("1 2 3 4 5 6 7 "), tostring(numbers));

    numbers = tr_parseNumberRange("1-Hello"sv);
    auto const empty_string = std::string{};
    EXPECT_EQ(empty_string, tostring(numbers));

    numbers = tr_parseNumberRange("1-"sv);
    EXPECT_EQ(empty_string, tostring(numbers));

    numbers = tr_parseNumberRange("Hello"sv);
    EXPECT_EQ(empty_string, tostring(numbers));
}

TEST_F(UtilsTest, trStrlower)
{
    EXPECT_EQ(""sv, tr_strlower(""sv));
    EXPECT_EQ("apple"sv, tr_strlower("APPLE"sv));
    EXPECT_EQ("apple"sv, tr_strlower("Apple"sv));
    EXPECT_EQ("apple"sv, tr_strlower("aPPLe"sv));
    EXPECT_EQ("apple"sv, tr_strlower("applE"sv));
    EXPECT_EQ("hello"sv, tr_strlower("HELLO"sv));
    EXPECT_EQ("hello"sv, tr_strlower("hello"sv));
}

TEST_F(UtilsTest, truncd)
{
    EXPECT_EQ("100.00%"sv, fmt::format("{:.2f}%", 99.999));
    EXPECT_EQ("99.99%"sv, fmt::format("{:.2f}%", tr_truncd(99.999, 2)));
    EXPECT_EQ("403650.6562"sv, fmt::format("{:.4f}", tr_truncd(403650.656250, 4)));
    EXPECT_EQ("2.15"sv, fmt::format("{:.2f}", tr_truncd(2.15, 2)));
    EXPECT_EQ("2.05"sv, fmt::format("{:.2f}", tr_truncd(2.05, 2)));
    EXPECT_EQ("3.33"sv, fmt::format("{:.2f}", tr_truncd(3.333333, 2)));
    EXPECT_EQ("3"sv, fmt::format("{:.0f}", tr_truncd(3.333333, 0)));
    EXPECT_EQ("3"sv, fmt::format("{:.0f}", tr_truncd(3.9999, 0)));

#if !(defined(_MSC_VER) || (defined(__MINGW32__) && defined(__MSVCRT__)))
    /* FIXME: MSCVRT behaves differently in case of nan */
    auto const nan = sqrt(-1.0);
    auto const nanstr = fmt::format("{:.2f}", tr_truncd(nan, 2));
    EXPECT_TRUE(strstr(nanstr.c_str(), "nan") != nullptr || strstr(nanstr.c_str(), "NaN") != nullptr);
#endif
}

TEST_F(UtilsTest, trStrlcpy)
{
    // destination will be initialized with this char
    char const initial_char = '1';
    std::array<char, 100> destination = { initial_char };

    std::vector<std::string> const tests{
        "a",
        "",
        "12345678901234567890",
        "This, very useful string contains a total of 105 characters not counting null. Almost like an easter egg!"
    };

    for (auto const& test : tests)
    {
        auto c_string = test.c_str();
        auto length = strlen(c_string);

        destination.fill(initial_char);

        auto response = tr_strlcpy(&destination, c_string, 98);

        // Check response length
        ASSERT_EQ(response, length);

        // Check what was copied
        for (unsigned i = 0U; i < 97U; ++i)
        {
            if (i <= length)
            {
                ASSERT_EQ(destination[i], c_string[i]);
            }
            else
            {
                ASSERT_EQ(destination[i], initial_char);
            }
        }

        // tr_strlcpy should only write this far if (length >= 98)
        if (length >= 98)
        {
            ASSERT_EQ(destination[97], '\0');
        }
        else
        {
            ASSERT_EQ(destination[97], initial_char);
        }

        // tr_strlcpy should not write this far
        ASSERT_EQ(destination[98], initial_char);
        ASSERT_EQ(destination[99], initial_char);
    }
}

TEST_F(UtilsTest, env)
{
    char const* test_key = "TR_TEST_ENV";

    unsetenv(test_key);

    EXPECT_FALSE(tr_env_key_exists(test_key));
    EXPECT_EQ(123, tr_env_get_int(test_key, 123));
    EXPECT_EQ(""sv, tr_env_get_string(test_key));
    EXPECT_EQ("a"sv, tr_env_get_string(test_key, "a"sv));

    setenv(test_key, "", 1);

    EXPECT_TRUE(tr_env_key_exists(test_key));
    EXPECT_EQ(456, tr_env_get_int(test_key, 456));
    EXPECT_EQ("", tr_env_get_string(test_key, ""));
    EXPECT_EQ("", tr_env_get_string(test_key, "b"));

    setenv(test_key, "135", 1);

    EXPECT_TRUE(tr_env_key_exists(test_key));
    EXPECT_EQ(135, tr_env_get_int(test_key, 789));
    EXPECT_EQ("135", tr_env_get_string(test_key, ""));
    EXPECT_EQ("135", tr_env_get_string(test_key, "c"));
}

TEST_F(UtilsTest, mimeTypes)
{
    EXPECT_EQ("audio/x-flac"sv, tr_get_mime_type_for_filename("music.flac"sv));
    EXPECT_EQ("audio/x-flac"sv, tr_get_mime_type_for_filename("music.FLAC"sv));
    EXPECT_EQ("video/x-msvideo"sv, tr_get_mime_type_for_filename(".avi"sv));
    EXPECT_EQ("video/x-msvideo"sv, tr_get_mime_type_for_filename("/path/to/FILENAME.AVI"sv));
    EXPECT_EQ("application/octet-stream"sv, tr_get_mime_type_for_filename("music.ajoijfeisfe"sv));
}

TEST_F(UtilsTest, saveFile)
{
    auto filename = tr_pathbuf{};

    // save a file to GoogleTest's temp dir
    filename.assign(::testing::TempDir(), "filename.txt"sv);
    auto contents = "these are the contents"sv;
    tr_error* error = nullptr;
    EXPECT_TRUE(tr_saveFile(filename.sv(), contents, &error));
    EXPECT_EQ(nullptr, error) << *error;

    // now read the file back in and confirm the contents are the same
    auto buf = std::vector<char>{};
    EXPECT_TRUE(tr_loadFile(filename.sv(), buf, &error));
    EXPECT_EQ(nullptr, error) << *error;
    auto sv = std::string_view{ std::data(buf), std::size(buf) };
    EXPECT_EQ(contents, sv);

    // remove the tempfile
    EXPECT_TRUE(tr_sys_path_remove(filename, &error));
    EXPECT_EQ(nullptr, error) << *error;

    // try saving a file to a path that doesn't exist
    filename = "/this/path/does/not/exist/foo.txt";
    EXPECT_FALSE(tr_saveFile(filename.sv(), contents, &error));
    ASSERT_NE(nullptr, error);
    EXPECT_NE(0, error->code);
    tr_error_clear(&error);
}

TEST_F(UtilsTest, ratioToString)
{
    // Testpairs contain ratio as a double and a string
    static auto constexpr Tests = std::array<std::pair<double, std::string_view>, 16>{ { { 0.0, "0.00" },
                                                                                         { 0.01, "0.01" },
                                                                                         { 0.1, "0.10" },
                                                                                         { 1.0, "1.00" },
                                                                                         { 1.015, "1.01" },
                                                                                         { 4.99, "4.99" },
                                                                                         { 4.996, "4.99" },
                                                                                         { 5.0, "5.0" },
                                                                                         { 5.09999, "5.0" },
                                                                                         { 5.1, "5.1" },
                                                                                         { 99.99, "99.9" },
                                                                                         { 100.0, "100" },
                                                                                         { 4000.4, "4000" },
                                                                                         { 600000.0, "600000" },
                                                                                         { 900000000.0, "900000000" },
                                                                                         { TR_RATIO_INF, "inf" } } };
    char const nullchar = '\0';

    ASSERT_EQ(tr_strratio(TR_RATIO_NA, "Ratio is NaN"), "None");
    ASSERT_EQ(tr_strratio(TR_RATIO_INF, "A bit longer text for infinity"), "A bit longer text for infinity");
    // Inf contains only null character
    ASSERT_EQ(tr_strratio(TR_RATIO_INF, &nullchar), "");

    for (auto const& [input, expected] : Tests)
    {
        ASSERT_EQ(tr_strratio(input, "inf"), expected);
    }
}
