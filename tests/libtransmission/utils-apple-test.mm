// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <algorithm>
#include <string_view>

#include <gtest/gtest.h>

#include <libtransmission/string-utils.h>

#include "test-fixtures.h"

using UtilsTest = ::tr::test::TransmissionTest;
using namespace std::literals;

namespace
{
[[nodiscard]] constexpr size_t count_replacement_char(std::string_view const sv)
{
    size_t count = 0;
    constexpr auto needle = "\xEF\xBF\xBD"sv; // U+FFFD replacement
    auto pos = std::string_view::size_type{};

    while ((pos = sv.find(needle, pos)) != std::string_view::npos)
    {
        ++count;
        pos += std::size(needle);
    }

    return count;
}

[[nodiscard]] constexpr bool has_non_ascii(std::string_view const sv)
{
    return std::any_of(std::begin(sv), std::end(sv), [](unsigned char ch) { return ch >= 0x80; });
}
} // namespace

TEST_F(UtilsTest, trStrvToUtf8NsstringValid)
{
    @autoreleasepool
    {
        NSString* str = tr_strv_to_utf8_nsstring("hello"sv);
        EXPECT_TRUE([str isEqualToString:@"hello"]);
    }
}

TEST_F(UtilsTest, trStrvToUtf8NsstringInvalid)
{
    @autoreleasepool
    {
        constexpr auto bad = "\xF4\x33\x81\x82"sv;
        NSString* str = tr_strv_to_utf8_nsstring(bad);
        EXPECT_TRUE([str isEqualToString:@""]);
    }
}

TEST_F(UtilsTest, trStrvToUtf8NsstringFallback)
{
    @autoreleasepool
    {
        constexpr auto bad = "\xF4\x33\x81\x82"sv;
        NSString* const key = @"tr.strv.to.utf8.fallback";
        NSString* const comment = @"fallback string for tests";
        NSString* str = tr_strv_to_utf8_nsstring(bad, key, comment);
        EXPECT_TRUE([str isEqualToString:key]);
    }
}

TEST_F(UtilsTest, trStrvToUtf8StringMixedInvalid)
{
    constexpr auto input = "hello \xF0\x28\x8C\x28 world"sv;
    auto const out = tr_strv_to_utf8_string(input);
    EXPECT_FALSE(out.empty());
    EXPECT_EQ(out, tr_strv_replace_invalid(out));
    EXPECT_EQ(out, tr_strv_to_utf8_string(out));
}

TEST_F(UtilsTest, trStrvToUtf8StringAutodetectImproves)
{
    // Shift_JIS-encoded filename from a real-world report in
    // https://github.com/transmission/transmission/pull/5244#issuecomment-1474442137
    constexpr auto input =
        "\x93\xC1\x96\xBD\x8C\x57\x92\xB7\x81\x45\x91\xFC\x96\xEC\x90\x6D"
        "\x83\x8A\x83\x5E\x81\x5B\x83\x93\x83\x59 (D-ABC 704x396 DivX511).avi"sv;

    auto const replace_only = tr_strv_replace_invalid(input);
    auto const autodetect = tr_strv_to_utf8_string(input);

    EXPECT_FALSE(autodetect.empty());
    EXPECT_EQ(autodetect, tr_strv_replace_invalid(autodetect));

    // Autodetect should preserve more readable text than replacement-only.
    EXPECT_LT(count_replacement_char(autodetect), count_replacement_char(replace_only));

    // If autodetect improves, it should yield valid UTF-8 with real non-ASCII characters.
    if (count_replacement_char(autodetect) < count_replacement_char(replace_only))
    {
        EXPECT_EQ(0U, count_replacement_char(autodetect));
        EXPECT_TRUE(has_non_ascii(autodetect));
    }
}
