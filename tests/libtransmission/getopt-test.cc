// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>

#include <libtransmission/tr-getopt.h>

#include "gtest/gtest.h"

namespace
{
using Arg = tr_option::Arg;
auto constexpr Options = std::array<tr_option, 9>{ {
    { 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", Arg::None, nullptr },
    { 'o', "outfile", "Save the generated .torrent to this filename", "o", Arg::Required, "<file>" },
    { 's',
      "piecesize",
      "Set how many KiB each piece should be, overriding the preferred default",
      "s",
      Arg::Required,
      "<size in KiB>" },
    { 'c', "comment", "Add a comment", "c", Arg::Required, "<comment>" },
    { 't', "tracker", "Add a tracker's announce URL", "t", Arg::Required, "<url>" },
    { 'q', "pooka", "Pooka", "pk", Arg::None, nullptr },
    { 'V', "version", "Show version number and exit", "V", Arg::None, nullptr },
    { 994, "sequential-download", "Download the torrent sequentially", "seq", Arg::Optional, "<piece>" },
    { 0, nullptr, nullptr, nullptr, Arg::None, nullptr },
} };
static_assert(Options[std::size(Options) - 2].val != 0);
} // namespace

class GetoptTest : public ::testing::Test
{
protected:
    static void runTest( //
        int argc,
        char const* const* argv,
        int expected_n,
        int const* expected_c,
        char const* const* expected_args)
    {
        auto n = int{};
        tr_optind = 1;

        auto c = int{};
        char const* argstr = nullptr;
        while ((c = tr_getopt("summary", argc, argv, Options.data(), &argstr)) != TR_OPT_DONE)
        {
            EXPECT_LT(n, expected_n);
            EXPECT_EQ(expected_c[n], c);
            EXPECT_STREQ(expected_args[n], argstr);
            ++n;
        }

        EXPECT_EQ(expected_n, n);
    }
};

TEST_F(GetoptTest, noOptions)
{
    static auto constexpr Args = std::array<char const*, 1>{ "/some/path/tr-getopt-test" };
    static auto constexpr ExpectedN = 0;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{};
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{};
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortNoarg)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-p" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'p' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longNoarg)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "--private" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'p' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortWithRequiredArg)
{
    static auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "-o", "/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'o' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longWithRequiredArg)
{
    static auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "--outfile", "/tmp/outfile" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'o' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortWithRequiredArgAfterEq)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-o=/tmp/outfile" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'o' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longWithRequiredArgAfterEq)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "--outfile=/tmp/outfile" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'o' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, unknownOption)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-z" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ TR_OPT_UNK };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "-z" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, missingArgEnd)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-o" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ TR_OPT_ERR };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, missingArgMiddle)
{
    static auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "-o", "-p" };
    static auto constexpr ExpectedN = 2;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ TR_OPT_ERR, 'p' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr, nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, lotsOfOptions)
{
    static auto constexpr Args = std::array<char const*, 6>{
        "/some/path/tr-getopt-test", "--piecesize=4", "-c", "hello world", "-p", "--tracker=foo"
    };
    static auto constexpr ExpectedN = 4;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 's', 'c', 'p', 't' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "4", "hello world", nullptr, "foo" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, matchLongerKey)
{
    // confirm that this resolves to 'q' and not 'p'
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-pk" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'q' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortWithOptionalArg)
{
    static auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "-seq", "12" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 994 };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "12" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longWithOptionalArg)
{
    static auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "--sequential-download", "12" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 994 };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "12" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortWithOptionalArgAfterEq)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-seq=12" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 994 };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "12" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longWithOptionalArgAfterEq)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "--sequential-download=12" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 994 };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "12" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortWithoutOptionalArgEnd)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-seq" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 994 };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longWithoutOptionalArgEnd)
{
    static auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "--sequential-download" };
    static auto constexpr ExpectedN = 1;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 994 };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortWithoutOptionalArgMiddle)
{
    static auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "-seq", "-p" };
    static auto constexpr ExpectedN = 2;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 994, 'p' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr, nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longWithoutOptionalArgMiddle)
{
    static auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "--sequential-download", "-p" };
    static auto constexpr ExpectedN = 2;
    static auto constexpr ExpectedC = std::array<int, ExpectedN>{ 994, 'p' };
    static auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr, nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}
