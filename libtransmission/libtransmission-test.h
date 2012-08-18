/* Note VERBOSE needs to be (un)defined before including this file */

#ifndef LIBTRANSMISSION_TEST_H
#define LIBTRANSMISSION_TEST_H 1

#include <stdio.h>
#include <string.h>

static int test = 0;

#define REPORT_TEST(test, res) \
    fprintf( stderr, "%s test #%d (%s, %d)\n", res, test, __FILE__, __LINE__ )

#ifdef VERBOSE
  #define check( A )						\
    do {							\
        ++test;							\
        if( A )							\
	    REPORT_TEST(test, "PASS");				\
        else {							\
	    REPORT_TEST(test, "FAIL");				\
            return test;					\
        }							\
    } while(0)
#else
  #define check( A )						\
    do {							\
        ++test;							\
        if( !( A ) ){						\
	    REPORT_TEST(test, "FAIL");				\
            return test;					\
        }							\
    } while(0)
#endif


typedef int (*testFunc)( void );
#define NUM_TESTS(tarray) ((int) (sizeof(tarray)/sizeof(tarray[0])))

static inline int
runTests( const testFunc * const tests, int numTests )
{
    int ret, i;

    (void) test; /* Use test even if we don't have any tests to run */

    for( i = 0; i < numTests; i++ )
	if( (ret = (*tests[i])()) )
	    return ret;

    return 0; 	/* All tests passed */
}

#define MAIN_SINGLE_TEST(test)					\
int main( void )							\
{								\
    const testFunc tests[] = { test };				\
								\
    return runTests(tests, 1);					\
}

#endif /* !LIBTRANSMISSION_TEST_H */
