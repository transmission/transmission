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
    int port;
    char* scheme = nullptr;
    char* host = nullptr;
    char* path = nullptr;
    EXPECT_TRUE(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    EXPECT_STREQ("http", scheme);
    EXPECT_STREQ("1", host);
    EXPECT_STREQ("/", path);
    EXPECT_EQ(80, port);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);

    auto parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("1"sv, parsed->host);
    EXPECT_EQ("/"sv, parsed->path);
    EXPECT_EQ("80"sv, parsed->portstr);
    EXPECT_EQ(80, parsed->port);

    url = "http://www.some-tracker.org/some/path";
    scheme = nullptr;
    host = nullptr;
    path = nullptr;
    EXPECT_TRUE(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    EXPECT_STREQ("http", scheme);
    EXPECT_STREQ("www.some-tracker.org", host);
    EXPECT_STREQ("/some/path", path);
    EXPECT_EQ(80, port);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);

    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("www.some-tracker.org"sv, parsed->host);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ("80"sv, parsed->portstr);
    EXPECT_EQ(80, parsed->port);

    url = "http://www.some-tracker.org:8080/some/path";
    scheme = nullptr;
    host = nullptr;
    path = nullptr;
    EXPECT_TRUE(tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, &path));
    EXPECT_STREQ("http", scheme);
    EXPECT_STREQ("www.some-tracker.org", host);
    EXPECT_STREQ("/some/path", path);
    EXPECT_EQ(8080, port);
    tr_free(scheme);
    tr_free(path);
    tr_free(host);

    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("www.some-tracker.org"sv, parsed->host);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ("8080"sv, parsed->portstr);
    EXPECT_EQ(8080, parsed->port);

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
