/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

/* Note VERBOSE needs to be (un)defined before including this file */

#pragma once

#include <stdio.h>
#include <string.h> /* strlen() */

#include "transmission.h"
#include "utils.h" /* tr_strcmp0() */

extern int current_test;

extern bool verbose;

bool should_print(bool pass);

bool check_condition_impl(char const* file, int line, bool condition);
bool check_uint_eq_impl(char const* file, int line, uintmax_t expected, uintmax_t actual);
bool check_ptr_eq_impl(char const* file, int line, void const* expected, void const* actual);

bool libtest_check_str(char const* file, int line, bool pass, char const* lhs, char const* rhs, char const* lhs_str,
    char const* op_str, char const* rhs_str);
bool libtest_check_int(char const* file, int line, bool pass, intmax_t lhs, intmax_t rhs, char const* lhs_str,
    char const* op_str, char const* rhs_str);

/***
****
***/

#define check(condition) \
    do \
    { \
        ++current_test; \
        \
        if (!check_condition_impl(__FILE__, __LINE__, (condition))) \
        { \
            return current_test; \
        } \
    } \
    while (0)

#define check_str(lhs, op, rhs) \
    do \
    { \
        ++current_test; \
        \
        char const* const check_str_lhs = (lhs); \
        char const* const check_str_rhs = (rhs); \
        \
        if (!libtest_check_str(__FILE__, __LINE__, tr_strcmp0(check_str_lhs, check_str_rhs) op 0, check_str_lhs, \
            check_str_rhs, #lhs, #op, #rhs)) \
        { \
            return current_test; \
        } \
    } \
    while (0)

#define check_int(lhs, op, rhs) \
    do \
    { \
        ++current_test; \
        \
        intmax_t const check_int_lhs = (lhs); \
        intmax_t const check_int_rhs = (rhs); \
        \
        if (!libtest_check_int(__FILE__, __LINE__, check_int_lhs op check_int_rhs, check_int_lhs, check_int_rhs, #lhs, #op, \
            #rhs)) \
        { \
            return current_test; \
        } \
    } \
    while (0)

#define check_uint_eq(expected, actual) \
    do \
    { \
        ++current_test; \
        \
        if (!check_uint_eq_impl(__FILE__, __LINE__, (expected), (actual))) \
        { \
            return current_test; \
        } \
    } \
    while (0)

#define check_ptr_eq(expected, actual) \
    do \
    { \
        ++current_test; \
        \
        if (!check_ptr_eq_impl(__FILE__, __LINE__, (expected), (actual))) \
        { \
            return current_test; \
        } \
    } \
    while (0)

/***
****
***/

typedef int (* testFunc)(void);

#define NUM_TESTS(tarray) ((int)(sizeof(tarray) / sizeof(tarray[0])))

int runTests(testFunc const* const tests, int numTests);

#define MAIN_SINGLE_TEST(test) \
    int main(void) \
    { \
        testFunc const tests[] = { test }; \
        return runTests(tests, 1); \
    }

tr_session* libttest_session_init(struct tr_variant* settings);
void libttest_session_close(tr_session* session);

void libttest_zero_torrent_populate(tr_torrent* tor, bool complete);
tr_torrent* libttest_zero_torrent_init(tr_session* session);

void libttest_blockingTorrentVerify(tr_torrent* tor);

void libtest_create_file_with_contents(char const* path, void const* contents, size_t n);
void libtest_create_tmpfile_with_contents(char* tmpl, void const* payload, size_t n);
void libtest_create_file_with_string_contents(char const* path, char const* str);

char* libtest_sandbox_create(void);
void libtest_sandbox_destroy(char const* sandbox);

void libttest_sync(void);
