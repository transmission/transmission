/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

/* Note VERBOSE needs to be (un)defined before including this file */

#ifndef LIBTRANSMISSION_TEST_H
#define LIBTRANSMISSION_TEST_H 1

#include <stdio.h>
#include <string.h> /* strlen() */

#include "transmission.h"
#include "utils.h" /* tr_strcmp0 () */

extern int current_test;

extern bool verbose;

bool should_print (bool pass);

bool check_condition_impl (const char * file, int line, bool condition);
bool check_int_eq_impl (const char * file, int line, int64_t expected, int64_t actual);
bool check_ptr_eq_impl (const char * file, int line, const void * expected, const void * actual);
bool check_streq_impl (const char * file, int line, const char * expected, const char * actual);

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

int runTests (const testFunc * const tests, int numTests);

#define MAIN_SINGLE_TEST(test) \
int main (void) { \
    const testFunc tests[] = { test }; \
    return runTests (tests, 1); \
}

tr_session * libttest_session_init (struct tr_variant * settings);
void         libttest_session_close (tr_session * session);

void         libttest_zero_torrent_populate (tr_torrent * tor, bool complete);
tr_torrent * libttest_zero_torrent_init (tr_session * session);

void         libttest_blockingTorrentVerify (tr_torrent * tor);

void         libtest_create_file_with_contents (const char * path, const void* contents, size_t n);
void         libtest_create_tmpfile_with_contents (char* tmpl, const void* payload, size_t n);
void         libtest_create_file_with_string_contents (const char * path, const char* str);

char*        libtest_sandbox_create (void);
void         libtest_sandbox_destroy (const char * sandbox);

void         libttest_sync (void);

#endif /* !LIBTRANSMISSION_TEST_H */
