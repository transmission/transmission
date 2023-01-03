// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/tr-getopt.h>

#include "gtest/gtest.h"

#include <array>

namespace
{

auto const Options = std::array<tr_option, 8>{
    tr_option{ 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", false, nullptr },
    { 'o', "outfile", "Save the generated .torrent to this filename", "o", true, "<file>" },
    { 's', "piecesize", "Set how many KiB each piece should be, overriding the preferred default", "s", true, "<size in KiB>" },
    { 'c', "comment", "Add a comment", "c", true, "<comment>" },
    { 't', "tracker", "Add a tracker's announce URL", "t", true, "<url>" },
    { 'q', "pooka", "Pooka", "pk", false, nullptr },
    { 'V', "version", "Show version number and exit", "V", false, nullptr },
    { 0, nullptr, nullptr, nullptr, false, nullptr }
};

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
    auto const args = std::array<char const*, 1>{ "/some/path/tr-getopt-test" };
    auto constexpr ExpectedN = 0;
    auto const expected_c = std::array<int, ExpectedN>{};
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{};
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, shortNoarg)
{
    auto const args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-p" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ 'p' };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, longNoarg)
{
    auto const args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "--private" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ 'p' };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, shortWithArg)
{
    auto const args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "-o", "/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ 'o' };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, longWithArg)
{
    auto const args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "--outfile", "/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ 'o' };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, shortWithArgAfterEq)
{
    auto const args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-o=/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ 'o' };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, longWithArgAfterEq)
{
    auto const args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "--outfile=/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ 'o' };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, unknownOption)
{
    auto const args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-z" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ TR_OPT_UNK };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ "-z" };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, missingArg)
{
    auto const args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-o" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ TR_OPT_ERR };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, lotsOfOptions)
{
    auto const args = std::array<char const*, 6>{
        "/some/path/tr-getopt-test", "--piecesize=4", "-c", "hello world", "-p", "--tracker=foo"
    };
    auto constexpr ExpectedN = 4;
    auto const expected_c = std::array<int, ExpectedN>{ 's', 'c', 'p', 't' };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ "4", "hello world", nullptr, "foo" };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}

TEST_F(GetoptTest, matchLongerKey)
{
    // confirm that this resolves to 'q' and not 'p'
    auto const args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-pk" };
    auto constexpr ExpectedN = 1;
    auto const expected_c = std::array<int, ExpectedN>{ 'q' };
    auto const expected_opt_arg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(args.size(), args.data(), ExpectedN, expected_c.data(), expected_opt_arg.data());
}
