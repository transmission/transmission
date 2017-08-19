/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <event2/event.h>

#include "transmission.h"
#include "file.h"
#include "net.h"
#include "utils.h"
#include "watchdir.h"

#include "libtransmission-test.h"

/***
****
***/

typedef struct callback_data
{
    tr_watchdir_t dir;
    char* name;
    tr_watchdir_status result;
}
callback_data;

#define CB_DATA_STATIC_INIT { NULL, NULL, 0 }

static struct event_base* ev_base = NULL;

extern struct timeval tr_watchdir_generic_interval;
extern unsigned int tr_watchdir_retry_limit;
extern struct timeval tr_watchdir_retry_start_interval;
extern struct timeval tr_watchdir_retry_max_interval;

static struct timeval const FIFTY_MSEC = { .tv_sec = 0, .tv_usec = 50000 };
static struct timeval const ONE_HUNDRED_MSEC = { .tv_sec = 0, .tv_usec = 100000 };
static struct timeval const TWO_HUNDRED_MSEC = { .tv_sec = 0, .tv_usec = 200000 };

static void process_events(void)
{
    event_base_loopexit(ev_base, &TWO_HUNDRED_MSEC);
    event_base_dispatch(ev_base);
}

static tr_watchdir_status callback(tr_watchdir_t dir, char const* name, void* context)
{
    callback_data* const data = context;

    if (data->result != TR_WATCHDIR_RETRY)
    {
        data->dir = dir;

        if (data->name != NULL)
        {
            tr_free(data->name);
        }

        data->name = tr_strdup(name);
    }

    return data->result;
}

static void reset_callback_data(callback_data* data, tr_watchdir_status result)
{
    tr_free(data->name);

    data->dir = NULL;
    data->name = NULL;
    data->result = result;
}

static void create_file(char const* parent_dir, char const* name)
{
    char* const path = tr_buildPath(parent_dir, name, NULL);
    libtest_create_file_with_string_contents(path, "");
    tr_free(path);
}

static void create_dir(char const* parent_dir, char const* name)
{
    char* const path = tr_buildPath(parent_dir, name, NULL);
    tr_sys_dir_create(path, 0, 0700, NULL);
    tr_free(path);
}

static tr_watchdir_t create_watchdir(char const* path, tr_watchdir_cb callback, void* callback_user_data,
    struct event_base* event_base)
{
#ifdef WATCHDIR_TEST_FORCE_GENERIC
    bool const force_generic = true;
#else
    bool const force_generic = false;
#endif

    return tr_watchdir_new(path, callback, callback_user_data, event_base, force_generic);
}

/***
****
***/

static int test_construct(void)
{
    char* const test_dir = libtest_sandbox_create();
    tr_watchdir_t wd;

    ev_base = event_base_new();

    wd = create_watchdir(test_dir, &callback, NULL, ev_base);
    check_ptr(wd, !=, NULL);
    check(tr_sys_path_is_same(test_dir, tr_watchdir_get_path(wd), NULL));

    process_events();

    tr_watchdir_free(wd);

    event_base_free(ev_base);

    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

static int test_initial_scan(void)
{
    char* const test_dir = libtest_sandbox_create();

    ev_base = event_base_new();

    /* Speed up generic implementation */
    tr_watchdir_generic_interval = ONE_HUNDRED_MSEC;

    {
        callback_data wd_data = CB_DATA_STATIC_INIT;
        reset_callback_data(&wd_data, TR_WATCHDIR_ACCEPT);

        tr_watchdir_t wd = create_watchdir(test_dir, &callback, &wd_data, ev_base);
        check_ptr(wd, !=, NULL);

        process_events();
        check_ptr(wd_data.dir, ==, NULL);
        check_ptr(wd_data.name, ==, NULL);

        tr_watchdir_free(wd);
        reset_callback_data(&wd_data, TR_WATCHDIR_ACCEPT);
    }

    create_file(test_dir, "test");

    {
        callback_data wd_data = CB_DATA_STATIC_INIT;
        reset_callback_data(&wd_data, TR_WATCHDIR_ACCEPT);

        tr_watchdir_t wd = create_watchdir(test_dir, &callback, &wd_data, ev_base);
        check_ptr(wd, !=, NULL);

        process_events();
        check_ptr(wd_data.dir, ==, wd);
        check_str(wd_data.name, ==, "test");

        tr_watchdir_free(wd);
        reset_callback_data(&wd_data, TR_WATCHDIR_ACCEPT);
    }

    event_base_free(ev_base);

    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

static int test_watch(void)
{
    char* const test_dir = libtest_sandbox_create();
    callback_data wd_data = CB_DATA_STATIC_INIT;
    tr_watchdir_t wd;

    ev_base = event_base_new();

    /* Speed up generic implementation */
    tr_watchdir_generic_interval = ONE_HUNDRED_MSEC;

    reset_callback_data(&wd_data, TR_WATCHDIR_ACCEPT);
    wd = create_watchdir(test_dir, &callback, &wd_data, ev_base);
    check_ptr(wd, !=, NULL);

    process_events();
    check_ptr(wd_data.dir, ==, NULL);
    check_ptr(wd_data.name, ==, NULL);

    create_file(test_dir, "test");

    process_events();
    check_ptr(wd_data.dir, ==, wd);
    check_str(wd_data.name, ==, "test");

    reset_callback_data(&wd_data, TR_WATCHDIR_IGNORE);
    create_file(test_dir, "test2");

    process_events();
    check_ptr(wd_data.dir, ==, wd);
    check_str(wd_data.name, ==, "test2");

    reset_callback_data(&wd_data, TR_WATCHDIR_IGNORE);
    create_dir(test_dir, "test3");

    process_events();
    check_ptr(wd_data.dir, ==, NULL);
    check_ptr(wd_data.name, ==, NULL);

    tr_watchdir_free(wd);
    reset_callback_data(&wd_data, TR_WATCHDIR_ACCEPT);

    event_base_free(ev_base);

    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

static int test_watch_two_dirs(void)
{
    char* const test_dir = libtest_sandbox_create();
    char* const dir1 = tr_buildPath(test_dir, "a", NULL);
    char* const dir2 = tr_buildPath(test_dir, "b", NULL);
    callback_data wd1_data = CB_DATA_STATIC_INIT;
    callback_data wd2_data = CB_DATA_STATIC_INIT;
    tr_watchdir_t wd1;
    tr_watchdir_t wd2;

    ev_base = event_base_new();

    /* Speed up generic implementation */
    tr_watchdir_generic_interval = ONE_HUNDRED_MSEC;

    create_dir(dir1, NULL);
    create_dir(dir2, NULL);

    reset_callback_data(&wd1_data, TR_WATCHDIR_ACCEPT);
    wd1 = create_watchdir(dir1, &callback, &wd1_data, ev_base);
    check_ptr(wd1, !=, NULL);

    reset_callback_data(&wd2_data, TR_WATCHDIR_ACCEPT);
    wd2 = create_watchdir(dir2, &callback, &wd2_data, ev_base);
    check_ptr(wd2, !=, NULL);

    process_events();
    check_ptr(wd1_data.dir, ==, NULL);
    check_ptr(wd1_data.name, ==, NULL);
    check_ptr(wd2_data.dir, ==, NULL);
    check_ptr(wd2_data.name, ==, NULL);

    create_file(dir1, "test");

    process_events();
    check_ptr(wd1_data.dir, ==, wd1);
    check_str(wd1_data.name, ==, "test");
    check_ptr(wd2_data.dir, ==, NULL);
    check_ptr(wd2_data.name, ==, NULL);

    reset_callback_data(&wd1_data, TR_WATCHDIR_ACCEPT);
    reset_callback_data(&wd2_data, TR_WATCHDIR_ACCEPT);
    create_file(dir2, "test2");

    process_events();
    check_ptr(wd1_data.dir, ==, NULL);
    check_ptr(wd1_data.name, ==, NULL);
    check_ptr(wd2_data.dir, ==, wd2);
    check_str(wd2_data.name, ==, "test2");

    reset_callback_data(&wd1_data, TR_WATCHDIR_IGNORE);
    reset_callback_data(&wd2_data, TR_WATCHDIR_IGNORE);
    create_file(dir1, "test3");
    create_file(dir2, "test4");

    process_events();
    check_ptr(wd1_data.dir, ==, wd1);
    check_str(wd1_data.name, ==, "test3");
    check_ptr(wd2_data.dir, ==, wd2);
    check_str(wd2_data.name, ==, "test4");

    reset_callback_data(&wd1_data, TR_WATCHDIR_ACCEPT);
    reset_callback_data(&wd2_data, TR_WATCHDIR_ACCEPT);
    create_file(dir1, "test5");
    create_dir(dir2, "test5");

    process_events();
    check_ptr(wd1_data.dir, ==, wd1);
    check_str(wd1_data.name, ==, "test5");
    check_ptr(wd2_data.dir, ==, NULL);
    check_ptr(wd2_data.name, ==, NULL);

    reset_callback_data(&wd1_data, TR_WATCHDIR_ACCEPT);
    reset_callback_data(&wd2_data, TR_WATCHDIR_ACCEPT);
    create_dir(dir1, "test6");
    create_file(dir2, "test6");

    process_events();
    check_ptr(wd1_data.dir, ==, NULL);
    check_ptr(wd1_data.name, ==, NULL);
    check_ptr(wd2_data.dir, ==, wd2);
    check_str(wd2_data.name, ==, "test6");

    reset_callback_data(&wd1_data, TR_WATCHDIR_ACCEPT);
    reset_callback_data(&wd2_data, TR_WATCHDIR_ACCEPT);
    create_dir(dir1, "test7");
    create_dir(dir2, "test7");

    process_events();
    check_ptr(wd1_data.dir, ==, NULL);
    check_ptr(wd1_data.name, ==, NULL);
    check_ptr(wd2_data.dir, ==, NULL);
    check_ptr(wd2_data.name, ==, NULL);

    tr_watchdir_free(wd2);
    reset_callback_data(&wd2_data, TR_WATCHDIR_ACCEPT);

    tr_watchdir_free(wd1);
    reset_callback_data(&wd1_data, TR_WATCHDIR_ACCEPT);

    event_base_free(ev_base);

    tr_free(dir2);
    tr_free(dir1);
    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

static int test_retry(void)
{
    char* const test_dir = libtest_sandbox_create();
    callback_data wd_data = CB_DATA_STATIC_INIT;
    tr_watchdir_t wd;

    ev_base = event_base_new();

    /* Speed up generic implementation */
    tr_watchdir_generic_interval = ONE_HUNDRED_MSEC;

    /* Tune retry logic */
    tr_watchdir_retry_limit = 10;
    tr_watchdir_retry_start_interval = FIFTY_MSEC;
    tr_watchdir_retry_max_interval = tr_watchdir_retry_start_interval;

    reset_callback_data(&wd_data, TR_WATCHDIR_RETRY);
    wd = create_watchdir(test_dir, &callback, &wd_data, ev_base);
    check_ptr(wd, !=, NULL);

    process_events();
    check_ptr(wd_data.dir, ==, NULL);
    check_ptr(wd_data.name, ==, NULL);

    create_file(test_dir, "test");

    process_events();
    check_ptr(wd_data.dir, ==, NULL);
    check_ptr(wd_data.name, ==, NULL);

    reset_callback_data(&wd_data, TR_WATCHDIR_ACCEPT);

    process_events();
    check_ptr(wd_data.dir, ==, wd);
    check_str(wd_data.name, ==, "test");

    tr_watchdir_free(wd);
    reset_callback_data(&wd_data, TR_WATCHDIR_ACCEPT);

    event_base_free(ev_base);

    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

/***
****
***/

int main(void)
{
    testFunc const tests[] =
    {
        test_construct,
        test_initial_scan,
        test_watch,
        test_watch_two_dirs,
        test_retry
    };

    tr_net_init();

    return runTests(tests, NUM_TESTS(tests));
}
