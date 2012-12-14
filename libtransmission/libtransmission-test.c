
#include <stdio.h>

#include "transmission.h"
#include "libtransmission-test.h"

bool verbose = false;

int current_test = 0;

bool
should_print (bool pass)
{
  if (!pass)
    return true;

  if (verbose)
    return true;

  return false;
#ifdef VERBOSE
  return true;
#else
  return false;
#endif
}

bool
check_condition_impl (const char * file, int line, bool condition)
{
  const bool pass = condition;

  if (should_print (pass))
    fprintf (stderr, "%s %s:%d\n", pass?"PASS":"FAIL", file, line);

  return pass;
}

bool
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

bool
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

bool
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

int
runTests (const testFunc * const tests, int numTests)
{
  int i;
  int ret;

  (void) current_test; /* Use test even if we don't have any tests to run */

  for (i=0; i<numTests; i++)
    if ((ret = (*tests[i])()))
      return ret;

  return 0; /* All tests passed */
}
