/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string_view>

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

#include "transmission.h"
#include "platform.h"
#include "web-utils.h"

#include "test-fixtures.h"

// #include <algorithm>
// #include <array>
// #include <cmath>
// #include <cstdlib>
// #include <iostream>
// #include <sstream>
// #include <string>

using ::libtransmission::test::makeString;
using WebUtilsTest = ::testing::Test;
using namespace std::literals;

TEST_F(WebUtilsTest, urlParse)
{
    auto const* url = "http://1";
    auto parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("1"sv, parsed->host);
    EXPECT_EQ("/"sv, parsed->path);
    EXPECT_EQ("80"sv, parsed->portstr);
    EXPECT_EQ(80, parsed->port);

    url = "http://www.some-tracker.org/some/path";
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("www.some-tracker.org"sv, parsed->host);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ("80"sv, parsed->portstr);
    EXPECT_EQ(80, parsed->port);

    url = "http://www.some-tracker.org:8080/some/path";
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("www.some-tracker.org"sv, parsed->host);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ("8080"sv, parsed->portstr);
    EXPECT_EQ(8080, parsed->port);
}

TEST_F(WebUtilsTest, urlNextQueryPair)
{
    auto const url = "a=1&b=two&c=si&d_has_no_val&e=&f&g=gee"sv;

    auto walk = tr_urlNextQueryPair(url);
    EXPECT_TRUE(walk);
    EXPECT_EQ("a"sv, walk->key);
    EXPECT_EQ("1"sv, walk->value);

    walk = tr_urlNextQueryPair(walk->remain);
    EXPECT_TRUE(walk);
    EXPECT_EQ("b"sv, walk->key);
    EXPECT_EQ("two"sv, walk->value);

    walk = tr_urlNextQueryPair(walk->remain);
    EXPECT_TRUE(walk);
    EXPECT_EQ("c"sv, walk->key);
    EXPECT_EQ("si"sv, walk->value);

    walk = tr_urlNextQueryPair(walk->remain);
    EXPECT_TRUE(walk);
    EXPECT_EQ("d_has_no_val"sv, walk->key);
    EXPECT_EQ(""sv, walk->value);

    walk = tr_urlNextQueryPair(walk->remain);
    EXPECT_TRUE(walk);
    EXPECT_EQ("e"sv, walk->key);
    EXPECT_EQ(""sv, walk->value);

    walk = tr_urlNextQueryPair(walk->remain);
    EXPECT_TRUE(walk);
    EXPECT_EQ("f"sv, walk->key);
    EXPECT_EQ(""sv, walk->value);

    walk = tr_urlNextQueryPair(walk->remain);
    EXPECT_TRUE(walk);
    EXPECT_EQ("g"sv, walk->key);
    EXPECT_EQ("gee"sv, walk->value);

    walk = tr_urlNextQueryPair(walk->remain);
    EXPECT_FALSE(walk);
}

TEST_F(WebUtilsTest, urlIsValid)
{
    EXPECT_FALSE(tr_urlIsValid("hello world"sv));
    EXPECT_FALSE(tr_urlIsValid("http://www.💩.com/announce/"sv));
    EXPECT_TRUE(tr_urlIsValid("http://www.example.com/announce/"sv));
    EXPECT_FALSE(tr_urlIsValid(""sv));
    EXPECT_FALSE(tr_urlIsValid("com"sv));
    EXPECT_FALSE(tr_urlIsValid("www.example.com"sv));
    EXPECT_FALSE(tr_urlIsValid("://www.example.com"sv));
    EXPECT_FALSE(tr_urlIsValid("zzz://www.example.com"sv)); // syntactically valid, but unsupported scheme
    EXPECT_TRUE(tr_urlIsValid("https://www.example.com"sv));

    EXPECT_TRUE(tr_urlIsValid("sftp://www.example.com"sv));
    EXPECT_FALSE(tr_urlIsValidTracker("sftp://www.example.com"sv)); // unsupported tracker scheme
}

TEST_F(WebUtilsTest, httpUnescape)
{
    auto const url = std::string{ "http%3A%2F%2Fwww.example.com%2F~user%2F%3Ftest%3D1%26test1%3D2" };
    auto str = makeString(tr_http_unescape(url.data(), url.size()));
    EXPECT_EQ("http://www.example.com/~user/?test=1&test1=2", str);
}
