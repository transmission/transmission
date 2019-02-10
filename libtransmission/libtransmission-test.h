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

bool libtest_check(char const* file, int line, bool pass, bool condition, char const* condition_str);
bool libtest_check_bool(char const* file, int line, bool pass, bool lhs, bool rhs, char const* lhs_str, char const* op_str,
    char const* rhs_str);
bool libtest_check_str(char const* file, int line, bool pass, char const* lhs, char const* rhs, char const* lhs_str,
    char const* op_str, char const* rhs_str);
bool libtest_check_mem(char const* file, int line, bool pass, void const* lhs, void const* rhs, size_t size,
    char const* lhs_str, char const* op_str, char const* rhs_str);
bool libtest_check_int(char const* file, int line, bool pass, intmax_t lhs, intmax_t rhs, char const* lhs_str,
    char const* op_str, char const* rhs_str);
bool libtest_check_uint(char const* file, int line, bool pass, uintmax_t lhs, uintmax_t rhs, char const* lhs_str,
    char const* op_str, char const* rhs_str);
bool libtest_check_ptr(char const* file, int line, bool pass, void const* lhs, void const* rhs, char const* lhs_str,
    char const* op_str, char const* rhs_str);

/***
****
***/

#define check(condition) \
    do \
    { \
        ++current_test; \
        \
        bool const check_result = (condition); \
        \
        if (!libtest_check(__FILE__, __LINE__, check_result, check_result, #condition)) \
        { \
            return current_test; \
        } \
    } \
    while (0)

#define check_bool(lhs, op, rhs) \
    do \
    { \
        ++current_test; \
        \
        bool const check_lhs = (lhs); \
        bool const check_rhs = (rhs); \
        \
        bool const check_result = check_lhs op check_rhs; \
        \
        if (!libtest_check_bool(__FILE__, __LINE__, check_result, check_lhs, check_rhs, #lhs, #op, #rhs)) \
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
        char const* const check_lhs = (lhs); \
        char const* const check_rhs = (rhs); \
        \
        bool const check_result = tr_strcmp0(check_lhs, check_rhs) op 0; \
        \
        if (!libtest_check_str(__FILE__, __LINE__, check_result, check_lhs, check_rhs, #lhs, #op, #rhs)) \
        { \
            return current_test; \
        } \
    } \
    while (0)

#define check_mem(lhs, op, rhs, size) \
    do \
    { \
        ++current_test; \
        \
        void const* const check_lhs = (lhs); \
        void const* const check_rhs = (rhs); \
        size_t const check_mem_size = (size); \
        \
        bool const check_result = tr_memcmp0(check_lhs, check_rhs, check_mem_size) op 0; \
        \
        if (!libtest_check_mem(__FILE__, __LINE__, check_result, check_lhs, check_rhs, check_mem_size, #lhs, #op, #rhs)) \
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
        intmax_t const check_lhs = (lhs); \
        intmax_t const check_rhs = (rhs); \
        \
        bool const check_result = check_lhs op check_rhs; \
        \
        if (!libtest_check_int(__FILE__, __LINE__, check_result, check_lhs, check_rhs, #lhs, #op, #rhs)) \
        { \
            return current_test; \
        } \
    } \
    while (0)

#define check_uint(lhs, op, rhs) \
    do \
    { \
        ++current_test; \
        \
        uintmax_t const check_lhs = (lhs); \
        uintmax_t const check_rhs = (rhs); \
        \
        bool const check_result = check_lhs op check_rhs; \
        \
        if (!libtest_check_uint(__FILE__, __LINE__, check_result, check_lhs, check_rhs, #lhs, #op, #rhs)) \
        { \
            return current_test; \
        } \
    } \
    while (0)

#define check_ptr(lhs, op, rhs) \
    do \
    { \
        ++current_test; \
        \
        void const* const check_lhs = (lhs); \
        void const* const check_rhs = (rhs); \
        \
        bool const check_result = check_lhs op check_rhs; \
        \
        if (!libtest_check_ptr(__FILE__, __LINE__, check_result, check_lhs, check_rhs, #lhs, #op, #rhs)) \
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
