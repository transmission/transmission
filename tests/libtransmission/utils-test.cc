// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cmath> // sqrt()
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include "lib/base/env.h"

#include "libtransmission/transmission.h"
#include "libtransmission/utils.h"

#include "test-fixtures.h"

using UtilsTest = ::tr::test::TransmissionTest;
using namespace std::literals;

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

    auto numbers = tr_num_parse_range("1-10,13,16-19"sv);
    EXPECT_EQ(std::string("1 2 3 4 5 6 7 8 9 10 13 16 17 18 19 "), tostring(numbers));

    numbers = tr_num_parse_range("1-5,3-7,2-6"sv);
    EXPECT_EQ(std::string("1 2 3 4 5 6 7 "), tostring(numbers));

    numbers = tr_num_parse_range("1-Hello"sv);
    auto const empty_string = std::string{};
    EXPECT_EQ(empty_string, tostring(numbers));

    numbers = tr_num_parse_range("1-"sv);
    EXPECT_EQ(empty_string, tostring(numbers));

    numbers = tr_num_parse_range("Hello"sv);
    EXPECT_EQ(empty_string, tostring(numbers));
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

TEST_F(UtilsTest, mimeTypes)
{
    EXPECT_EQ("audio/x-flac"sv, tr_get_mime_type_for_filename("music.flac"sv));
    EXPECT_EQ("audio/x-flac"sv, tr_get_mime_type_for_filename("music.FLAC"sv));
    EXPECT_EQ("video/x-msvideo"sv, tr_get_mime_type_for_filename(".avi"sv));
    EXPECT_EQ("video/x-msvideo"sv, tr_get_mime_type_for_filename("/path/to/FILENAME.AVI"sv));
    EXPECT_EQ("application/octet-stream"sv, tr_get_mime_type_for_filename("music.ajoijfeisfe"sv));
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

    ASSERT_EQ(tr_strratio(TR_RATIO_NA, "None", "Ratio is NaN"), "None");
    ASSERT_EQ(tr_strratio(TR_RATIO_INF, "None", "A bit longer text for infinity"), "A bit longer text for infinity");
    // Inf contains only null character
    ASSERT_EQ(tr_strratio(TR_RATIO_INF, "None", &nullchar), "");

    for (auto const& [input, expected] : Tests)
    {
        ASSERT_EQ(tr_strratio(input, "None", "inf"), expected);
    }
}
