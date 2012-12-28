#include <math.h> /* sqrt () */
#include <string.h> /* strlen () */

#include "transmission.h"
#include "ConvertUTF.h" /* tr_utf8_validate*/
#include "platform.h"
#include "crypto.h"
#include "utils.h"
#include "web.h"

#define NUM_LOOPS 1
#define SPEED_TEST 0

#if SPEED_TEST
 #define VERBOSE
 #undef NUM_LOOPS
 #define NUM_LOOPS 200
#endif

#include "libtransmission-test.h"

static int
test_base64 (void)
{
  int len;
  char *in, *out;

  /* base64 */
  out = tr_base64_encode ("YOYO!", -1, &len);
  check_streq ("WU9ZTyE=", out);
  check_int_eq (8, len);
  in = tr_base64_decode (out, -1, &len);
  check_streq ("YOYO!", in);
  check_int_eq (5, len);
  tr_free (in);
  tr_free (out);
  out = tr_base64_encode (NULL, 0, &len);
  check (out == NULL);
  check_int_eq (0, len);

  return 0;
}

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
  out = tr_utf8clean (in, -1);
  check_streq (in, out);
  tr_free (out);

  in = "hello world";
  out = tr_utf8clean (in, 5);
  check_streq ("hello", out);
  tr_free (out);

  /* this version is not utf-8 */
  in = "“Û‰ÌÓ ·˚Ú¸ ¡Ó„ÓÏ";
  out = tr_utf8clean (in, 17);
  check (out != NULL);
  check ((strlen (out) == 17) || (strlen (out) == 32));
  check (tr_utf8_validate (out, -1, NULL));
  tr_free (out);

  /* same string, but utf-8 clean */
  in = "√í√∞√≥√§√≠√Æ √°√ª√≤√º √Å√Æ√£√Æ√¨";
  out = tr_utf8clean (in, -1);
  check (out != NULL);
  check (tr_utf8_validate (out, -1, NULL));
  check_streq (in, out);
  tr_free (out);

  return 0;
}

static int
test_numbers (void)
{
  int i;
  int count;
  int * numbers;

  numbers = tr_parseNumberRange ("1-10,13,16-19", -1, &count);
  check_int_eq (15, count);
  check_int_eq (1, numbers[0]);
  check_int_eq (6, numbers[5]);
  check_int_eq (10, numbers[9]);
  check_int_eq (13, numbers[10]);
  check_int_eq (16, numbers[11]);
  check_int_eq (19, numbers[14]);
  tr_free (numbers);

  numbers = tr_parseNumberRange ("1-5,3-7,2-6", -1, &count);
  check (count == 7);
  check (numbers != NULL);
  for (i=0; i<count; ++i)
    check_int_eq (i+1, numbers[i]);
  tr_free (numbers);

  numbers = tr_parseNumberRange ("1-Hello", -1, &count);
  check_int_eq (0, count);
  check (numbers == NULL);

  numbers = tr_parseNumberRange ("1-", -1, &count);
  check_int_eq (0, count);
  check (numbers == NULL);

  numbers = tr_parseNumberRange ("Hello", -1, &count);
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
  const int expected_exact[] = { true, true, true, false, true, false, false, true, false, false };
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
    buf[i] = tr_cryptoWeakRandInt (range);

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
  uint8_t sha1[20];

  memcpy (hex1, "fb5ef5507427b17e04b69cef31fa3379b456735a", 41);
  tr_hex_to_sha1 (sha1, hex1);
  tr_sha1_to_hex (hex2, sha1);
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
  check (!tr_urlParse (url, -1, &scheme, &host, &port, &path));
  check_streq ("http", scheme);
  check_streq ("1", host);
  check_streq ("/", path);
  check_int_eq (80, port);
  tr_free (scheme);
  tr_free (path);
  tr_free (host);

  url = "http://www.some-tracker.org/some/path";
  check (!tr_urlParse (url, -1, &scheme, &host, &port, &path));
  check_streq ("http", scheme);
  check_streq ("www.some-tracker.org", host);
  check_streq ("/some/path", path);
  check_int_eq (80, port);
  tr_free (scheme);
  tr_free (path);
  tr_free (host);

  url = "http://www.some-tracker.org:80/some/path";
  check (!tr_urlParse (url, -1, &scheme, &host, &port, &path));
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

  tr_snprintf (buf, sizeof (buf), "%.2f", tr_truncd (nan, 2));
  check (strstr (buf, "nan") != NULL);

  return 0;
}

static int
test_cryptoRand (void)
{
  int i;

  /* test that tr_cryptoRandInt () stays in-bounds */
  for (i = 0; i < 100000; ++i)
    {
      const int val = tr_cryptoRandInt (100);
       check (val >= 0);
       check (val < 100);
    }

  return 0;
}

int
main (void)
{
  const testFunc tests[] = { test_array,
                             test_base64,
                             test_buildpath,
                             test_cryptoRand,
                             test_hex,
                             test_lowerbound,
                             test_quickfindFirst,
                             test_memmem,
                             test_numbers,
                             test_strip_positional_args,
                             test_strstrip,
                             test_truncd,
                             test_url,
                             test_utf8 };

  return runTests (tests, NUM_TESTS (tests));
}

