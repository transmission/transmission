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

class GetoptTest: public ::testing::Test {
protected:
    std::array<tr_option,8> options = {
        tr_option{ 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", false, nullptr },
        { 'o', "outfile", "Save the generated .torrent to this filename", "o", true, "<file>" },
        { 's', "piecesize", "Set how many KiB each piece should be, overriding the preferred default", "s", true, "<size in KiB>" },
        { 'c', "comment", "Add a comment", "c", true, "<comment>" },
        { 't', "tracker", "Add a tracker's announce URL", "t", true, "<url>" },
        { 'q', "pooka", "Pooka", "pk", false, nullptr },
        { 'V', "version", "Show version number and exit", "V", false, nullptr },
        { 0, nullptr, nullptr, nullptr, false, nullptr }
    };

    void run_test(int argc, char const** argv,
                  int expected_n,
                  int* expected_c,
                  char const** expected_optarg) const
    {
        auto n = int {};
        tr_optind = 1;

        int c;
        char const* optarg;
        while ((c = tr_getopt("summary", argc, argv, std::data(options), &optarg)) != TR_OPT_DONE)
        {
            EXPECT_LT(n, expected_n);
            EXPECT_EQ(expected_c[n], c);
            EXPECT_STREQ(expected_optarg[n], optarg);
            ++n;
        }

        EXPECT_EQ(expected_n, n);
    }
};

TEST_F(GetoptTest, no_options)
{
    int argc = 1;
    char const* argv[] = { "/some/path/tr-getopt-test" };
    int expected_n = 0;
    int expected_c[] = { 0 };
    char const* expected_optarg[] = { nullptr };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, short_noarg)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-p" };
    int expected_n = 1;
    int expected_c[] = { 'p' };
    char const* expected_optarg[] = { nullptr };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, long_noarg)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "--private" };
    int expected_n = 1;
    int expected_c[] = { 'p' };
    char const* expected_optarg[] = { nullptr };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, short_with_arg)
{
    int argc = 3;
    char const* argv[] = { "/some/path/tr-getopt-test", "-o", "/tmp/outfile" };
    int expected_n = 1;
    int expected_c[] = { 'o' };
    char const* expected_optarg[] = { "/tmp/outfile" };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, long_with_arg)
{
    int argc = 3;
    char const* argv[] = { "/some/path/tr-getopt-test", "--outfile", "/tmp/outfile" };
    int expected_n = 1;
    int expected_c[] = { 'o' };
    char const* expected_optarg[] = { "/tmp/outfile" };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, short_with_arg_after_eq)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-o=/tmp/outfile" };
    int expected_n = 1;
    int expected_c[] = { 'o' };
    char const* expected_optarg[] = { "/tmp/outfile" };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, long_with_arg_after_eq)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "--outfile=/tmp/outfile" };
    int expected_n = 1;
    int expected_c[] = { 'o' };
    char const* expected_optarg[] = { "/tmp/outfile" };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, unknown_option)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-z" };
    int expected_n = 1;
    int expected_c[] = { TR_OPT_UNK };
    char const* expected_optarg[] = { "-z" };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, missing_arg)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-o" };
    int expected_n = 1;
    int expected_c[] = { TR_OPT_ERR };
    char const* expected_optarg[] = { nullptr };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, lots_of_options)
{
    int argc = 6;
    char const* argv[] = { "/some/path/tr-getopt-test", "--piecesize=4", "-c", "hello world", "-p", "--tracker=foo" };
    int expected_n = 4;
    int expected_c[] = { 's', 'c', 'p', 't' };
    char const* expected_optarg[] = { "4", "hello world", nullptr, "foo" };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

TEST_F(GetoptTest, match_longer_key)
{
    // confirm that this resolves to 'q' and not 'p'
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-pk" };
    int expected_n = 1;
    int expected_c[] = { 'q' };
    char const* expected_optarg[] = { nullptr };
    run_test(argc, argv, expected_n, expected_c, expected_optarg);
}
