/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "transmission.h"
#include "tr-getopt.h"

#include "libtransmission-test.h"

static const struct tr_option options[] =
{
 { 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", 0, NULL },
 { 'o', "outfile", "Save the generated .torrent to this filename", "o", 1, "<file>" },
 { 's', "piecesize", "Set how many KiB each piece should be, overriding the preferred default", "s", 1, "<size in KiB>" },
 { 'c', "comment", "Add a comment", "c", 1, "<comment>" },
 { 't', "tracker", "Add a tracker's announce URL", "t", 1, "<url>" },
 { 'q', "pooka", "Pooka", "pk", 0, NULL },
 { 'V', "version", "Show version number and exit", "V", 0, NULL },
 { 0, NULL, NULL, NULL, 0, NULL }
};

static int
run_test (int           argc,
          const char ** argv,
          int           expected_n,
          int         * expected_c,
          const char ** expected_optarg)
{
  int c;
  int n;
  const char * optarg;

  n = 0;
  tr_optind = 1;
  while ((c = tr_getopt ("summary", argc, argv, options, &optarg)))
    {
      check (n < expected_n);
      check_int_eq (expected_c[n], c);
      check_streq (optarg, expected_optarg[n]);
      ++n;
    }

  check_int_eq (expected_n, n);
  return 0;
}

/***
****
***/

static int
test_no_options (void)
{
  int argc = 1;
  const char * argv[] = { "/some/path/tr-getopt-test" };
  int expected_n = 0;
  int expected_c[] = { 0 };
  const char * expected_optarg[] = { NULL };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_short_noarg (void)
{
  int argc = 2;
  const char * argv[] = { "/some/path/tr-getopt-test", "-p" };
  int expected_n = 1;
  int expected_c[] = { 'p' };
  const char * expected_optarg[] = { NULL };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_long_noarg (void)
{
  int argc = 2;
  const char * argv[] = { "/some/path/tr-getopt-test", "--private" };
  int expected_n = 1;
  int expected_c[] = { 'p' };
  const char * expected_optarg[] = { NULL };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_short_with_arg (void)
{
  int argc = 3;
  const char * argv[] = { "/some/path/tr-getopt-test", "-o", "/tmp/outfile" };
  int expected_n = 1;
  int expected_c[] = { 'o' };
  const char * expected_optarg[] = { "/tmp/outfile" };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_long_with_arg (void)
{
  int argc = 3;
  const char * argv[] = { "/some/path/tr-getopt-test", "--outfile", "/tmp/outfile" };
  int expected_n = 1;
  int expected_c[] = { 'o' };
  const char * expected_optarg[] = { "/tmp/outfile" };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_short_with_arg_after_eq (void)
{
  int argc = 2;
  const char * argv[] = { "/some/path/tr-getopt-test", "-o=/tmp/outfile" };
  int expected_n = 1;
  int expected_c[] = { 'o' };
  const char * expected_optarg[] = { "/tmp/outfile" };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_long_with_arg_after_eq (void)
{
  int argc = 2;
  const char * argv[] = { "/some/path/tr-getopt-test", "--outfile=/tmp/outfile" };
  int expected_n = 1;
  int expected_c[] = { 'o' };
  const char * expected_optarg[] = { "/tmp/outfile" };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_unknown_option (void)
{
  int argc = 2;
  const char * argv[] = { "/some/path/tr-getopt-test", "-z" };
  int expected_n = 1;
  int expected_c[] = { TR_OPT_UNK };
  const char * expected_optarg[] = { "-z" };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_missing_arg (void)
{
  int argc = 2;
  const char * argv[] = { "/some/path/tr-getopt-test", "-o" };
  int expected_n = 1;
  int expected_c[] = { TR_OPT_ERR };
  const char * expected_optarg[] = { NULL };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_lots_of_options (void)
{
  int argc = 6;
  const char * argv[] = { "/some/path/tr-getopt-test", "--piecesize=4", "-c", "hello world", "-p", "--tracker=foo" };
  int expected_n = 4;
  int expected_c[] = { 's', 'c', 'p', 't' };
  const char * expected_optarg[] = { "4", "hello world", NULL, "foo" };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}

static int
test_match_longer_key (void)
{
  /* confirm that this resolves to 'q' and not 'p' */
  int argc = 2;
  const char * argv[] = { "/some/path/tr-getopt-test", "-pk" };
  int expected_n = 1;
  int expected_c[] = { 'q' };
  const char * expected_optarg[] = { NULL };
  return run_test (argc, argv, expected_n, expected_c, expected_optarg);
}


/***
****
***/

int
main (void)
{
  const testFunc tests[] = { test_no_options,
                             test_short_noarg,
                             test_long_noarg,
                             test_short_with_arg,
                             test_long_with_arg,
                             test_short_with_arg_after_eq,
                             test_long_with_arg_after_eq,
                             test_unknown_option,
                             test_missing_arg,
                             test_match_longer_key,
                             test_lots_of_options };

  return runTests (tests, NUM_TESTS (tests));
}

