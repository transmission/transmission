/* Note VERBOSE needs to be (un)defined before including this file */

#ifndef LIBTRANSMISSION_TEST_H
#define LIBTRANSMISSION_TEST_H 1

#include <stdio.h>

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

extern tr_session * session;
extern char * sandbox;
extern char * downloadDir;
extern char * blocklistDir;

void libtransmission_test_session_init_formatters (void);
void libtransmission_test_session_init_sandbox (void);
void libtransmission_test_session_init_session (void);
void libtransmission_test_session_init (void); /* utility; calls the other 3 */

void libtransmission_test_session_close (void);

void         libtransmission_test_zero_torrent_populate (tr_torrent * tor, bool complete);
tr_torrent * libtransmission_test_zero_torrent_init (void);

void         libttest_blockingTorrentVerify (tr_torrent * tor);



#endif /* !LIBTRANSMISSION_TEST_H */
