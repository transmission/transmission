/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "tr-getopt.h"

#include "libtransmission-test.h"

static struct tr_option const options[] =
{
    { 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", false, NULL },
    { 'o', "outfile", "Save the generated .torrent to this filename", "o", true, "<file>" },
    { 's', "piecesize", "Set how many KiB each piece should be, overriding the preferred default", "s", true, "<size in KiB>" },
    { 'c', "comment", "Add a comment", "c", true, "<comment>" },
    { 't', "tracker", "Add a tracker's announce URL", "t", true, "<url>" },
    { 'q', "pooka", "Pooka", "pk", false, NULL },
    { 'V', "version", "Show version number and exit", "V", false, NULL },
    { 0, NULL, NULL, NULL, false, NULL }
};

static int run_test(int argc, char const** argv, int expected_n, int* expected_c, char const** expected_optarg)
{
    int c;
    int n;
    char const* optarg;

    n = 0;
    tr_optind = 1;

    while ((c = tr_getopt("summary", argc, argv, options, &optarg)) != TR_OPT_DONE)
    {
        check_int(n, <, expected_n);
        check_int(c, ==, expected_c[n]);
        check_str(optarg, ==, expected_optarg[n]);
        ++n;
    }

    check_int(n, ==, expected_n);
    return 0;
}

/***
****
***/

static int test_no_options(void)
{
    int argc = 1;
    char const* argv[] = { "/some/path/tr-getopt-test" };
    int expected_n = 0;
    int expected_c[] = { 0 };
    char const* expected_optarg[] = { NULL };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_short_noarg(void)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-p" };
    int expected_n = 1;
    int expected_c[] = { 'p' };
    char const* expected_optarg[] = { NULL };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_long_noarg(void)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "--private" };
    int expected_n = 1;
    int expected_c[] = { 'p' };
    char const* expected_optarg[] = { NULL };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_short_with_arg(void)
{
    int argc = 3;
    char const* argv[] = { "/some/path/tr-getopt-test", "-o", "/tmp/outfile" };
    int expected_n = 1;
    int expected_c[] = { 'o' };
    char const* expected_optarg[] = { "/tmp/outfile" };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_long_with_arg(void)
{
    int argc = 3;
    char const* argv[] = { "/some/path/tr-getopt-test", "--outfile", "/tmp/outfile" };
    int expected_n = 1;
    int expected_c[] = { 'o' };
    char const* expected_optarg[] = { "/tmp/outfile" };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_short_with_arg_after_eq(void)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-o=/tmp/outfile" };
    int expected_n = 1;
    int expected_c[] = { 'o' };
    char const* expected_optarg[] = { "/tmp/outfile" };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_long_with_arg_after_eq(void)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "--outfile=/tmp/outfile" };
    int expected_n = 1;
    int expected_c[] = { 'o' };
    char const* expected_optarg[] = { "/tmp/outfile" };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_unknown_option(void)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-z" };
    int expected_n = 1;
    int expected_c[] = { TR_OPT_UNK };
    char const* expected_optarg[] = { "-z" };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_missing_arg(void)
{
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-o" };
    int expected_n = 1;
    int expected_c[] = { TR_OPT_ERR };
    char const* expected_optarg[] = { NULL };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_lots_of_options(void)
{
    int argc = 6;
    char const* argv[] = { "/some/path/tr-getopt-test", "--piecesize=4", "-c", "hello world", "-p", "--tracker=foo" };
    int expected_n = 4;
    int expected_c[] = { 's', 'c', 'p', 't' };
    char const* expected_optarg[] = { "4", "hello world", NULL, "foo" };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

static int test_match_longer_key(void)
{
    /* confirm that this resolves to 'q' and not 'p' */
    int argc = 2;
    char const* argv[] = { "/some/path/tr-getopt-test", "-pk" };
    int expected_n = 1;
    int expected_c[] = { 'q' };
    char const* expected_optarg[] = { NULL };
    return run_test(argc, argv, expected_n, expected_c, expected_optarg);
}

/***
****
***/

int main(void)
{
    testFunc const tests[] =
    {
        test_no_options,
        test_short_noarg,
        test_long_noarg,
        test_short_with_arg,
        test_long_with_arg,
        test_short_with_arg_after_eq,
        test_long_with_arg_after_eq,
        test_unknown_option,
        test_missing_arg,
        test_match_longer_key,
        test_lots_of_options
    };

    return runTests(tests, NUM_TESTS(tests));
}
