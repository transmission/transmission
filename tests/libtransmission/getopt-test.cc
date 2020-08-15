/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "tr-getopt.h"

#include "gtest/gtest.h"

#include <array>

namespace
{

auto constexpr Options = std::array<tr_option, 8>{
    tr_option{ 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", false, nullptr },
    { 'o', "outfile", "Save the generated .torrent to this filename", "o", true, "<file>" },
    { 's', "piecesize", "Set how many KiB each piece should be, overriding the preferred default", "s", true, "<size in KiB>" },
    { 'c', "comment", "Add a comment", "c", true, "<comment>" },
    { 't', "tracker", "Add a tracker's announce URL", "t", true, "<url>" },
    { 'q', "pooka", "Pooka", "pk", false, nullptr },
    { 'V', "version", "Show version number and exit", "V", false, nullptr },
    { 0, nullptr, nullptr, nullptr, false, nullptr }
};

} // anonymous namespace

class GetoptTest : public ::testing::Test
{
protected:
    void runTest(int argc, char const* const* argv, int expected_n, int const* expected_c,
        char const* const* expected_args) const
    {
        auto n = int{};
        tr_optind = 1;

        int c;
        char const* argstr;
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
    auto constexpr Args = std::array<char const*, 1>{ "/some/path/tr-getopt-test" };
    auto constexpr ExpectedN = 0;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{};
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{};
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortNoarg)
{
    auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-p" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'p' };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longNoarg)
{
    auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "--private" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'p' };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortWithArg)
{
    auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "-o", "/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'o' };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longWithArg)
{
    auto constexpr Args = std::array<char const*, 3>{ "/some/path/tr-getopt-test", "--outfile", "/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'o' };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, shortWithArgAfterEq)
{
    auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-o=/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'o' };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, longWithArgAfterEq)
{
    auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "--outfile=/tmp/outfile" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'o' };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "/tmp/outfile" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, unknownOption)
{
    auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-z" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ TR_OPT_UNK };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "-z" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, missingArg)
{
    auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-o" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ TR_OPT_ERR };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, lotsOfOptions)
{
    auto constexpr Args =
        std::array<char const*, 6>{ "/some/path/tr-getopt-test", "--piecesize=4", "-c", "hello world", "-p", "--tracker=foo" };
    auto constexpr ExpectedN = 4;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ 's', 'c', 'p', 't' };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ "4", "hello world", nullptr, "foo" };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}

TEST_F(GetoptTest, matchLongerKey)
{
    // confirm that this resolves to 'q' and not 'p'
    auto constexpr Args = std::array<char const*, 2>{ "/some/path/tr-getopt-test", "-pk" };
    auto constexpr ExpectedN = 1;
    auto constexpr ExpectedC = std::array<int, ExpectedN>{ 'q' };
    auto constexpr ExpectedOptArg = std::array<char const*, ExpectedN>{ nullptr };
    runTest(Args.size(), Args.data(), ExpectedN, ExpectedC.data(), ExpectedOptArg.data());
}
