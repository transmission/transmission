/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <limits.h> /* INT_MAX */
#include <math.h> /* sqrt () */
#include <string.h> /* strlen () */
#include <stdlib.h> /* setenv (), unsetenv () */

#ifdef _WIN32
 #include <windows.h>
 #define setenv(key, value, unused) SetEnvironmentVariableA (key, value)
 #define unsetenv(key) SetEnvironmentVariableA (key, NULL)
#endif

#include "transmission.h"
#include "ConvertUTF.h" /* tr_utf8_validate*/
#include "platform.h"
#include "crypto-utils.h" /* tr_rand_int_weak */
#include "utils.h"
#include "web.h"

#define SPEED_TEST 0

#if SPEED_TEST
 #define VERBOSE
#endif

#include "libtransmission-test.h"

static int
test_strip_positional_args (void)
{
  const char * in;
  const char * out;
  const char * expected;

  in = "Hello %1$s foo %2$.*f";
  expected = "Hello %s foo %.*f";
  out = tr_strip_positional_args (in);
  check_streq (expected, out);

  in = "Hello %1$'d foo %2$'f";
  expected = "Hello %d foo %f";
  out = tr_strip_positional_args (in);
  check_streq (expected, out);

  return 0;
}

static int
test_strstrip (void)
{
  char *in, *out;

  /* strstrip */
  in = tr_strdup ("   test    ");
  out = tr_strstrip (in);
  check (in == out);
  check_streq ("test", out);
  tr_free (in);

  /* strstrip */
  in = tr_strdup (" test test ");
  out = tr_strstrip (in);
  check (in == out);
  check_streq ("test test", out);
  tr_free (in);

  /* strstrip */
  in = tr_strdup ("test");
  out = tr_strstrip (in);
  check (in == out);
  check_streq ("test", out);
  tr_free (in);

  return 0;
}

static int
test_buildpath (void)
{
  char * out;

  out = tr_buildPath ("foo", "bar", NULL);
  check_streq ("foo" TR_PATH_DELIMITER_STR "bar", out);
  tr_free (out);

  out = tr_buildPath ("", "foo", "bar", NULL);
  check_streq (TR_PATH_DELIMITER_STR "foo" TR_PATH_DELIMITER_STR "bar", out);
  tr_free (out);

  return 0;
}

static int
test_utf8 (void)
{
  const char * in;
  char * out;

  in = "hello world";
  out = tr_utf8clean (in, TR_BAD_SIZE);
  check_streq (in, out);
  tr_free (out);

  in = "hello world";
  out = tr_utf8clean (in, 5);
  check_streq ("hello", out);
  tr_free (out);

  /* this version is not utf-8 (but cp866) */
  in = "\x92\xE0\xE3\xA4\xAD\xAE \xA1\xEB\xE2\xEC \x81\xAE\xA3\xAE\xAC";
  out = tr_utf8clean (in, 17);
  check (out != NULL);
  check ((strlen (out) == 17) || (strlen (out) == 33));
  check (tr_utf8_validate (out, TR_BAD_SIZE, NULL));
  tr_free (out);

  /* same string, but utf-8 clean */
  in = "Трудно быть Богом";
  out = tr_utf8clean (in, TR_BAD_SIZE);
  check (out != NULL);
  check (tr_utf8_validate (out, TR_BAD_SIZE, NULL));
  check_streq (in, out);
  tr_free (out);

  in = "\xF4\x00\x81\x82";
  out = tr_utf8clean (in, 4);
  check (out != NULL);
  check ((strlen (out) == 1) || (strlen (out) == 2));
  check (tr_utf8_validate (out, TR_BAD_SIZE, NULL));
  tr_free (out);

  in = "\xF4\x33\x81\x82";
  out = tr_utf8clean (in, 4);
  check (out != NULL);
  check ((strlen (out) == 4) || (strlen (out) == 7));
  check (tr_utf8_validate (out, TR_BAD_SIZE, NULL));
  tr_free (out);

  return 0;
}

static int
test_numbers (void)
{
  int i;
  int count;
  int * numbers;

  numbers = tr_parseNumberRange ("1-10,13,16-19", TR_BAD_SIZE, &count);
  check_int_eq (15, count);
  check_int_eq (1, numbers[0]);
  check_int_eq (6, numbers[5]);
  check_int_eq (10, numbers[9]);
  check_int_eq (13, numbers[10]);
  check_int_eq (16, numbers[11]);
  check_int_eq (19, numbers[14]);
  tr_free (numbers);

  numbers = tr_parseNumberRange ("1-5,3-7,2-6", TR_BAD_SIZE, &count);
  check (count == 7);
  check (numbers != NULL);
  for (i=0; i<count; ++i)
    check_int_eq (i+1, numbers[i]);
  tr_free (numbers);

  numbers = tr_parseNumberRange ("1-Hello", TR_BAD_SIZE, &count);
  check_int_eq (0, count);
  check (numbers == NULL);

  numbers = tr_parseNumberRange ("1-", TR_BAD_SIZE, &count);
  check_int_eq (0, count);
  check (numbers == NULL);

  numbers = tr_parseNumberRange ("Hello", TR_BAD_SIZE, &count);
  check_int_eq (0, count);
  check (numbers == NULL);

  return 0;
}

static int
compareInts (const void * va, const void * vb)
{
  const int a = *(const int *)va;
  const int b = *(const int *)vb;
  return a - b;
}

static int
test_lowerbound (void)
{
  int i;
  const int A[] = { 1, 2, 3, 3, 3, 5, 8 };
  const int expected_pos[] = { 0, 1, 2, 5, 5, 6, 6, 6, 7, 7 };
  const bool expected_exact[] = { true, true, true, false, true, false, false, true, false, false };
  const int N = sizeof (A) / sizeof (A[0]);

  for (i=1; i<=10; i++)
    {
      bool exact;
      const int pos = tr_lowerBound (&i, A, N, sizeof (int), compareInts, &exact);

#if 0
      fprintf (stderr, "searching for %d. ", i);
      fprintf (stderr, "result: index = %d, ", pos);
      if (pos != N)
        fprintf (stderr, "A[%d] == %d\n", pos, A[pos]);
      else
        fprintf (stderr, "which is off the end.\n");
#endif
      check_int_eq (expected_pos[i-1], pos);
      check_int_eq (expected_exact[i-1], exact);
    }

  return 0;
}

static int
test_quickFindFirst_Iteration (const size_t k, const size_t n, int * buf, int range)
{
  size_t i;
  int highest_low;
  int lowest_high;

  /* populate buf with random ints */
  for (i=0; i<n; ++i)
    buf[i] = tr_rand_int_weak (range);

  /* find the best k */
  tr_quickfindFirstK (buf, n, sizeof(int), compareInts, k);

  /* confirm that the smallest K ints are in the first slots K slots in buf */

  highest_low = INT_MIN;
  for (i=0; i<k; ++i)
    if (highest_low < buf[i])
      highest_low = buf[i];

  lowest_high = INT_MAX;
  for (i=k; i<n; ++i)
    if (lowest_high > buf[i])
      lowest_high = buf[i];

  check (highest_low <= lowest_high);

  return 0;
}

static int
test_quickfindFirst (void)
{
  size_t i;
  const size_t k = 10;
  const size_t n = 100;
  const size_t n_trials = 1000;
  int * buf = tr_new (int, n);

  for (i=0; i<n_trials; ++i)
    check_int_eq (0, test_quickFindFirst_Iteration (k, n, buf, 100));

  tr_free (buf);
  return 0;
}

static int
test_memmem (void)
{
  char const haystack[12] = "abcabcabcabc";
  char const needle[3] = "cab";

  check (tr_memmem (haystack, sizeof haystack, haystack, sizeof haystack) == haystack);
  check (tr_memmem (haystack, sizeof haystack, needle, sizeof needle) == haystack + 2);
  check (tr_memmem (needle, sizeof needle, haystack, sizeof haystack) == NULL);

  return 0;
}

static int
test_hex (void)
{
  char hex1[41];
  char hex2[41];
  uint8_t binary[20];

  memcpy (hex1, "fb5ef5507427b17e04b69cef31fa3379b456735a", 41);
  tr_hex_to_binary (hex1, binary, 20);
  tr_binary_to_hex (binary, hex2, 20);
  check_streq (hex1, hex2);

  return 0;
}

static int
test_array (void)
{
  int i;
  int array[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  int n = sizeof (array) / sizeof (array[0]);

  tr_removeElementFromArray (array, 5u, sizeof (int), n--);
  for (i=0; i<n; ++i)
    check_int_eq ((i<5 ? i : i+1), array[i]);

  tr_removeElementFromArray (array, 0u, sizeof (int), n--);
  for (i=0; i<n; ++i)
    check_int_eq ((i<4 ? i+1 : i+2), array[i]);

  tr_removeElementFromArray (array, n-1, sizeof (int), n); n--;
  for (i=0; i<n; ++i)
    check_int_eq ((i<4 ? i+1 : i+2), array[i]);

  return 0;
}

static int
test_url (void)
{
  int port;
  char * scheme;
  char * host;
  char * path;
  char * str;
  const char * url;

  url = "http://1";
  check (tr_urlParse (url, TR_BAD_SIZE, &scheme, &host, &port, &path));
  check_streq ("http", scheme);
  check_streq ("1", host);
  check_streq ("/", path);
  check_int_eq (80, port);
  tr_free (scheme);
  tr_free (path);
  tr_free (host);

  url = "http://www.some-tracker.org/some/path";
  check (tr_urlParse (url, TR_BAD_SIZE, &scheme, &host, &port, &path));
  check_streq ("http", scheme);
  check_streq ("www.some-tracker.org", host);
  check_streq ("/some/path", path);
  check_int_eq (80, port);
  tr_free (scheme);
  tr_free (path);
  tr_free (host);

  url = "http://www.some-tracker.org:80/some/path";
  check (tr_urlParse (url, TR_BAD_SIZE, &scheme, &host, &port, &path));
  check_streq ("http", scheme);
  check_streq ("www.some-tracker.org", host);
  check_streq ("/some/path", path);
  check_int_eq (80, port);
  tr_free (scheme);
  tr_free (path);
  tr_free (host);

  url = "http%3A%2F%2Fwww.example.com%2F~user%2F%3Ftest%3D1%26test1%3D2";
  str = tr_http_unescape (url, strlen (url));
  check_streq ("http://www.example.com/~user/?test=1&test1=2", str);
  tr_free (str);

  return 0;
}

static int
test_truncd (void)
{
  char buf[32];
  const double nan = sqrt (-1);

  tr_snprintf (buf, sizeof (buf), "%.2f%%", 99.999);
  check_streq ("100.00%", buf);

  tr_snprintf (buf, sizeof (buf), "%.2f%%", tr_truncd (99.999, 2));
  check_streq ("99.99%", buf);

  tr_snprintf (buf, sizeof (buf), "%.4f", tr_truncd (403650.656250, 4));
  check_streq ("403650.6562", buf);

  tr_snprintf (buf, sizeof (buf), "%.2f", tr_truncd (2.15, 2));
  check_streq ("2.15", buf);

  tr_snprintf (buf, sizeof (buf), "%.2f", tr_truncd (2.05, 2));
  check_streq ("2.05", buf);

  tr_snprintf (buf, sizeof (buf), "%.2f", tr_truncd (3.3333, 2));
  check_streq ("3.33", buf);

  tr_snprintf (buf, sizeof (buf), "%.0f", tr_truncd (3.3333, 0));
  check_streq ("3", buf);

#if !(defined (_MSC_VER) || (defined (__MINGW32__) && defined (__MSVCRT__)))
  /* FIXME: MSCVRT behaves differently in case of nan */
  tr_snprintf (buf, sizeof (buf), "%.2f", tr_truncd (nan, 2));
  check (strstr (buf, "nan") != NULL);
#else
  (void) nan;
#endif

  return 0;
}

static char *
test_strdup_printf_valist (const char * fmt, ...)
{
  va_list args;
  char * ret;

  va_start (args, fmt);
  ret = tr_strdup_vprintf (fmt, args);
  va_end (args);

  return ret;
}

static int
test_strdup_printf (void)
{
  char * s, * s2, * s3;

  s = tr_strdup_printf ("%s", "test");
  check_streq ("test", s);
  tr_free (s);

  s = tr_strdup_printf ("%d %s %c %u", -1, "0", '1', 2);
  check_streq ("-1 0 1 2", s);
  tr_free (s);

  s3 = tr_malloc0 (4098);
  memset (s3, '-', 4097);
  s3[2047] = 't';
  s3[2048] = 'e';
  s3[2049] = 's';
  s3[2050] = 't';

  s2 = tr_malloc0 (4096);
  memset (s2, '-', 4095);
  s2[2047] = '%';
  s2[2048] = 's';

  s = tr_strdup_printf (s2, "test");
  check_streq (s3, s);
  tr_free (s);

  tr_free (s2);

  s = tr_strdup_printf ("%s", s3);
  check_streq (s3, s);
  tr_free (s);

  tr_free (s3);

  s = test_strdup_printf_valist ("\n-%s-%s-%s-\n", "\r", "\t", "\b");
  check_streq ("\n-\r-\t-\b-\n", s);
  tr_free (s);

  return 0;
}

static int
test_env (void)
{
  const char * test_key = "TR_TEST_ENV";
  int x;
  char * s;

  unsetenv (test_key);

  check (!tr_env_key_exists (test_key));
  x = tr_env_get_int (test_key, 123);
  check_int_eq (123, x);
  s = tr_env_get_string (test_key, NULL);
  check (s == NULL);
  s = tr_env_get_string (test_key, "a");
  check_streq ("a", s);
  tr_free (s);

  setenv (test_key, "", 1);

  check (tr_env_key_exists (test_key));
  x = tr_env_get_int (test_key, 456);
  check_int_eq (456, x);
  s = tr_env_get_string (test_key, NULL);
  check_streq ("", s);
  tr_free (s);
  s = tr_env_get_string (test_key, "b");
  check_streq ("", s);
  tr_free (s);

  setenv (test_key, "135", 1);

  check (tr_env_key_exists (test_key));
  x = tr_env_get_int (test_key, 789);
  check_int_eq (135, x);
  s = tr_env_get_string (test_key, NULL);
  check_streq ("135", s);
  tr_free (s);
  s = tr_env_get_string (test_key, "c");
  check_streq ("135", s);
  tr_free (s);

  return 0;
}

int
main (void)
{
  const testFunc tests[] = { test_array,
                             test_buildpath,
                             test_hex,
                             test_lowerbound,
                             test_quickfindFirst,
                             test_memmem,
                             test_numbers,
                             test_strip_positional_args,
                             test_strdup_printf,
                             test_strstrip,
                             test_truncd,
                             test_url,
                             test_utf8,
                             test_env };

  return runTests (tests, NUM_TESTS (tests));
}

