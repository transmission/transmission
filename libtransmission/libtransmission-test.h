/* Note VERBOSE needs to be (un)defined before including this file */

#ifndef LIBTRANSMISSION_TEST_H
#define LIBTRANSMISSION_TEST_H 1

#include <stdio.h>

#include "utils.h" /* tr_strcmp0 () */

static int current_test = 0;

#define REPORT_TEST(test, result) \
    fprintf (stderr, "%s %s:%d\n", result, __FILE__, __LINE__)

static inline bool
should_print (bool pass)
{
  if (!pass)
    return true;

#ifdef VERBOSE
  return true;
#else
  return false;
#endif
}

static inline bool
check_condition_impl (const char * file, int line, bool condition)
{
  const bool pass = condition;

  if (should_print (pass))
    fprintf (stderr, "%s %s:%d\n", pass?"PASS":"FAIL", file, line);

  return pass;
}

static inline bool
check_streq_impl (const char * file, int line, const char * expected, const char * actual)
{
  const bool pass = !tr_strcmp0 (expected, actual);

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%s\", got \"%s\"\n", file, line, expected?expected:" (null)", actual?actual:" (null)");
  }

  return pass;
}

static inline bool
check_int_eq_impl (const char * file, int line, int64_t expected, int64_t actual)
{
  const bool pass = expected == actual;

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%"PRId64"\", got \"%"PRId64"\"\n", file, line, expected, actual);
  }

  return pass;
}

static inline bool
check_ptr_eq_impl (const char * file, int line, const void * expected, const void * actual)
{
  const bool pass = expected == actual;

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%p\", got \"%p\"\n", file, line, expected, actual);
  }

  return pass;
}

/***
****
***/

#define check(condition) \
  do { \
    ++current_test; \
    if (!check_condition_impl (__FILE__, __LINE__, (condition))) \
      return current_test; \
  } while (0)

#define check_streq(expected, actual) \
  do { \
    ++current_test; \
    if (!check_streq_impl (__FILE__, __LINE__, (expected), (actual))) \
      return current_test; \
  } while (0)

#define check_int_eq(expected, actual) \
  do { \
    ++current_test; \
    if (!check_int_eq_impl (__FILE__, __LINE__, (expected), (actual))) \
      return current_test; \
  } while (0)

#define check_ptr_eq(expected, actual) \
  do { \
    ++current_test; \
    if (!check_ptr_eq_impl (__FILE__, __LINE__, (expected), (actual))) \
      return current_test; \
  } while (0)

/***
****
***/

typedef int (*testFunc)(void);
#define NUM_TESTS(tarray)((int)(sizeof (tarray)/sizeof (tarray[0])))

static inline int
runTests (const testFunc * const tests, int numTests)
{
    int ret, i;

  (void) current_test; /* Use test even if we don't have any tests to run */

    for (i = 0; i < numTests; i++)
	if ((ret = (*tests[i])()))
	    return ret;

    return 0; 	/* All tests passed */
}

#define MAIN_SINGLE_TEST(test) \
int main (void) { \
    const testFunc tests[] = { test }; \
    return runTests (tests, 1); \
}

#endif /* !LIBTRANSMISSION_TEST_H */
