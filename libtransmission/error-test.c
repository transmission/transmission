/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "transmission.h"
#include "error.h"

#include "libtransmission-test.h"

static int
test_error_set (void)
{
  tr_error * err = NULL;

  tr_error_prefix (&err, "error: ");
  check (err == NULL);

  tr_error_set (&err, 1, "error: %s (%d)", "oops", 2);
  check (err != NULL);
  check_int_eq (1, err->code);
  check_streq ("error: oops (2)", err->message);
  tr_error_clear (&err);
  check (err == NULL);

  tr_error_set_literal (&err, 2, "oops");
  check (err != NULL);
  check_int_eq (2, err->code);
  check_streq ("oops", err->message);

  tr_error_prefix (&err, "error: ");
  check (err != NULL);
  check_int_eq (2, err->code);
  check_streq ("error: oops", err->message);

  tr_error_free (err);

  return 0;
}

static int
test_error_propagate (void)
{
  tr_error * err = NULL;
  tr_error * err2 = NULL;

  tr_error_set_literal (&err, 1, "oops");
  check (err != NULL);
  check_int_eq (1, err->code);
  check_streq ("oops", err->message);

  tr_error_propagate (&err2, &err);
  check (err2 != NULL);
  check_int_eq (1, err2->code);
  check_streq ("oops", err2->message);
  check (err == NULL);

  tr_error_propagate_prefixed (&err, &err2, "error: ");
  check (err != NULL);
  check_int_eq (1, err->code);
  check_streq ("error: oops", err->message);
  check (err2 == NULL);

  tr_error_propagate (NULL, &err);
  check (err == NULL);

  tr_error_free (err2);

  return 0;
}

int
main (void)
{
  const testFunc tests[] = { test_error_set,
                             test_error_propagate };

  return runTests (tests, NUM_TESTS (tests));
}
