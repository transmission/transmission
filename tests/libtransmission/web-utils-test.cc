// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

#include <libtransmission/transmission.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/platform.h>
#include <libtransmission/web-utils.h>

#include "test-fixtures.h"

using namespace std::literals;

using WebUtilsTest = ::testing::Test;
using namespace std::literals;

TEST_F(WebUtilsTest, urlParse)
{
    auto url = "http://1"sv;
    auto parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("1"sv, parsed->host);
    EXPECT_EQ("1"sv, parsed->sitename);
    EXPECT_EQ(""sv, parsed->path);
    EXPECT_EQ(""sv, parsed->query);
    EXPECT_EQ(""sv, parsed->fragment);
    EXPECT_EQ(80, parsed->port);

    url = "http://www.some-tracker.org/some/path"sv;
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("www.some-tracker.org"sv, parsed->host);
    EXPECT_EQ("some-tracker"sv, parsed->sitename);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ(""sv, parsed->query);
    EXPECT_EQ(""sv, parsed->fragment);
    EXPECT_EQ(80, parsed->port);

    url = "http://www.some-tracker.org:8080/some/path"sv;
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("www.some-tracker.org"sv, parsed->host);
    EXPECT_EQ("some-tracker"sv, parsed->sitename);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ(""sv, parsed->query);
    EXPECT_EQ(""sv, parsed->fragment);
    EXPECT_EQ(8080, parsed->port);

    url = "http://www.some-tracker.org:8080/some/path?key=val&foo=bar#fragment"sv;
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("www.some-tracker.org"sv, parsed->host);
    EXPECT_EQ("some-tracker"sv, parsed->sitename);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ("key=val&foo=bar"sv, parsed->query);
    EXPECT_EQ("fragment"sv, parsed->fragment);
    EXPECT_EQ(8080, parsed->port);

    url =
        "magnet:"
        "?xt=urn:btih:14ffe5dd23188fd5cb53a1d47f1289db70abf31e"
        "&dn=ubuntu_12_04_1_desktop_32_bit"
        "&tr=http%3A%2F%2Ftracker.publicbt.com%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
        "&ws=http%3A%2F%2Ftransmissionbt.com"sv;
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("magnet"sv, parsed->scheme);
    EXPECT_EQ(""sv, parsed->host);
    EXPECT_EQ(""sv, parsed->sitename);
    EXPECT_EQ(""sv, parsed->path);
    EXPECT_EQ(
        "xt=urn:btih:14ffe5dd23188fd5cb53a1d47f1289db70abf31e"
        "&dn=ubuntu_12_04_1_desktop_32_bit"
        "&tr=http%3A%2F%2Ftracker.publicbt.com%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
        "&ws=http%3A%2F%2Ftransmissionbt.com"sv,
        parsed->query);

    // test a host whose public suffix contains >1 dot
    url = "https://www.example.co.uk:8080/some/path"sv;
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("https"sv, parsed->scheme);
    EXPECT_EQ("example"sv, parsed->sitename);
    EXPECT_EQ("www.example.co.uk"sv, parsed->host);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ(8080, parsed->port);

    // test a host that lacks a subdomain
    url = "http://some-tracker.co.uk/some/other/path"sv;
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("some-tracker"sv, parsed->sitename);
    EXPECT_EQ("some-tracker.co.uk"sv, parsed->host);
    EXPECT_EQ("/some/other/path"sv, parsed->path);
    EXPECT_EQ(80, parsed->port);

    // test a host with an IPv4 address
    url = "https://127.0.0.1:8080/some/path"sv;
    parsed = tr_urlParse(url);
    EXPECT_TRUE(parsed);
    EXPECT_EQ("https"sv, parsed->scheme);
    EXPECT_EQ("127.0.0.1"sv, parsed->sitename);
    EXPECT_EQ("127.0.0.1"sv, parsed->host);
    EXPECT_EQ("/some/path"sv, parsed->path);
    EXPECT_EQ(8080, parsed->port);

    // test a host with a bracketed IPv6 address and explicit port
    url = "http://[2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d]:8080/announce"sv;
    parsed = tr_urlParse(url);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d"sv, parsed->sitename);
    EXPECT_EQ("2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d"sv, parsed->host);
    EXPECT_EQ("/announce"sv, parsed->path);
    EXPECT_EQ(8080, parsed->port);

    // test a host with a bracketed IPv6 address and implicit port
    url = "http://[2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d]/announce"sv;
    parsed = tr_urlParse(url);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d"sv, parsed->sitename);
    EXPECT_EQ("2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d"sv, parsed->host);
    EXPECT_EQ("/announce"sv, parsed->path);
    EXPECT_EQ(80, parsed->port);

    // test a host with an unbracketed IPv6 address and implicit port
    url = "http://2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d/announce"sv;
    parsed = tr_urlParse(url);
    EXPECT_EQ("http"sv, parsed->scheme);
    EXPECT_EQ("2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d"sv, parsed->sitename);
    EXPECT_EQ("2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d"sv, parsed->host);
    EXPECT_EQ("/announce"sv, parsed->path);
    EXPECT_EQ(80, parsed->port);
}

TEST(WebUtilsTest, urlParseFuzz)
{
    auto buf = std::vector<char>{};

    for (size_t i = 0; i < 100000U; ++i)
    {
        buf.resize(tr_rand_int(1024U));
        tr_rand_buffer(std::data(buf), std::size(buf));
        (void)tr_urlParse({ std::data(buf), std::size(buf) });
    }
}

TEST_F(WebUtilsTest, urlNextQueryPair)
{
    auto constexpr Query = "a=1&b=two&c=si&d_has_no_val&e=&f&g=gee"sv;
    auto const query_view = tr_url_query_view{ Query };
    auto const end = std::end(query_view);

    auto it = std::begin(query_view);
    EXPECT_NE(end, it);
    EXPECT_EQ("a"sv, it->first);
    EXPECT_EQ("1"sv, it->second);

    ++it;
    EXPECT_NE(end, it);
    EXPECT_EQ("b"sv, it->first);
    EXPECT_EQ("two"sv, it->second);

    ++it;
    EXPECT_NE(end, it);
    EXPECT_EQ("c"sv, it->first);
    EXPECT_EQ("si"sv, it->second);

    ++it;
    EXPECT_NE(end, it);
    EXPECT_EQ("d_has_no_val"sv, it->first);
    EXPECT_EQ(""sv, it->second);

    ++it;
    EXPECT_NE(end, it);
    EXPECT_EQ("e"sv, it->first);
    EXPECT_EQ(""sv, it->second);

    ++it;
    EXPECT_NE(end, it);
    EXPECT_EQ("f"sv, it->first);
    EXPECT_EQ(""sv, it->second);

    ++it;
    EXPECT_NE(end, it);
    EXPECT_EQ("g"sv, it->first);
    EXPECT_EQ("gee"sv, it->second);

    ++it;
    EXPECT_EQ(end, it);
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

TEST_F(WebUtilsTest, urlPercentDecode)
{
    auto constexpr Tests = std::array<std::pair<std::string_view, std::string_view>, 13>{ {
        { "%-2"sv, "%-2"sv },
        { "%6 1"sv, "%6 1"sv },
        { "%6"sv, "%6"sv },
        { "%6%a"sv, "%6%a"sv },
        { "%61 "sv, "a "sv },
        { "%61"sv, "a"sv },
        { "%61a"sv, "aa"sv },
        { "%61b"sv, "ab"sv },
        { "%6a"sv, "j"sv },
        { "%FF"sv, "\xff"sv },
        { "%FF%00%ff"sv, "\xff\x00\xff"sv },
        { "%FG"sv, "%FG"sv },
        { "http%3A%2F%2Fwww.example.com%2F~user%2F%3Ftest%3D1%26test1%3D2"sv,
          "http://www.example.com/~user/?test=1&test1=2"sv },
    } };

    for (auto const& [encoded, decoded] : Tests)
    {
        EXPECT_EQ(decoded, tr_urlPercentDecode(encoded));
    }
}
