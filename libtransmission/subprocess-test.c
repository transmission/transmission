/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <stdlib.h>

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "subprocess.h"
#include "utils.h"

#include "libtransmission-test.h"

static char arg_dump_args[] = "--dump-args";
static char arg_dump_env[] = "--dump-env";
static char arg_dump_cwd[] = "--dump-cwd";

static char* self_path = NULL;

static int test_spawn_async_missing_exe(void)
{
    char missing_exe_path[] = TR_IF_WIN32("C:\\", "/") "tr-missing-test-exe" TR_IF_WIN32(".exe", "");

    char* const args[] =
    {
        missing_exe_path,
        NULL
    };

    tr_error* error = NULL;
    bool const ret = tr_spawn_async(args, NULL, NULL, &error);
    check_bool(ret, ==, false);
    check_ptr(error, !=, NULL);
    check_int(error->code, !=, 0);
    check_str(error->message, !=, NULL);

    tr_error_clear(&error);

    return 0;
}

static int test_spawn_async_args(void)
{
    char* const test_dir = libtest_sandbox_create();
    char* const result_path = tr_sys_path_native_separators(tr_buildPath(test_dir, "result.txt", NULL));
    bool const allow_batch_metachars = TR_IF_WIN32(false, true) || !tr_str_has_suffix(self_path, ".cmd");

    char test_arg_1[] = "arg1 ";
    char test_arg_2[] = " arg2";
    char test_arg_3[] = "";
    char test_arg_4[] = "\"arg3'^! $PATH %PATH% \\";

    char* const args[] =
    {
        self_path,
        result_path,
        arg_dump_args,
        test_arg_1,
        test_arg_2,
        test_arg_3,
        allow_batch_metachars ? test_arg_4 : NULL,
        NULL
    };

    tr_error* error = NULL;
    bool const ret = tr_spawn_async(args, NULL, NULL, &error);
    check_bool(ret, ==, true);
    check_ptr(error, ==, NULL);

    while (!tr_sys_path_exists(result_path, NULL))
    {
        tr_wait_msec(10);
    }

    tr_sys_file_t fd = tr_sys_file_open(result_path, TR_SYS_FILE_READ, 0, NULL);
    check_int(fd, !=, TR_BAD_SYS_FILE);

    char buffer[1024];

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, test_arg_1);

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, test_arg_2);

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, test_arg_3);

    if (allow_batch_metachars)
    {
        check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
        check_str(buffer, ==, test_arg_4);
    }

    check(!tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));

    tr_sys_file_close(fd, NULL);

    tr_free(result_path);
    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

static int test_spawn_async_env(void)
{
    char* const test_dir = libtest_sandbox_create();
    char* const result_path = tr_sys_path_native_separators(tr_buildPath(test_dir, "result.txt", NULL));

    char test_env_key_1[] = "VAR1";
    char test_env_key_2[] = "_VAR_2_";
    char test_env_key_3[] = "vAr#";
    char test_env_key_4[] = "FOO";
    char test_env_key_5[] = "ZOO";
    char test_env_key_6[] = "TR_MISSING_TEST_ENV_KEY";

    char test_env_value_1[] = "value1 ";
    char test_env_value_2[] = " value2";
    char test_env_value_3[] = " \"value3'^! $PATH %PATH% ";
    char test_env_value_4[] = "bar";
    char test_env_value_5[] = "jar";

    char* const args[] =
    {
        self_path,
        result_path,
        arg_dump_env,
        test_env_key_1,
        test_env_key_2,
        test_env_key_3,
        test_env_key_4,
        test_env_key_5,
        test_env_key_6,
        NULL
    };

    char* const env[] =
    {
        tr_strdup_printf("%s=%s", test_env_key_1, test_env_value_1),
        tr_strdup_printf("%s=%s", test_env_key_2, test_env_value_2),
        tr_strdup_printf("%s=%s", test_env_key_3, test_env_value_3),
        tr_strdup_printf("%s=%s", test_env_key_5, test_env_value_5),
        NULL
    };

    /* Inherited */
    char foo_env_value[] = "FOO=bar";
    putenv(foo_env_value);

    /* Overridden */
    char zoo_env_value[] = "ZOO=tar";
    putenv(zoo_env_value);

    tr_error* error = NULL;
    bool const ret = tr_spawn_async(args, env, NULL, &error);
    check_bool(ret, ==, true);
    check_ptr(error, ==, NULL);

    while (!tr_sys_path_exists(result_path, NULL))
    {
        tr_wait_msec(10);
    }

    tr_sys_file_t fd = tr_sys_file_open(result_path, TR_SYS_FILE_READ, 0, NULL);
    check_int(fd, !=, TR_BAD_SYS_FILE);

    char buffer[1024];

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, test_env_value_1);

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, test_env_value_2);

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, test_env_value_3);

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, test_env_value_4);

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, test_env_value_5);

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(buffer, ==, "<null>");

    check(!tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));

    tr_sys_file_close(fd, NULL);

    tr_free_ptrv((void* const*)env);
    tr_free(result_path);
    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

static int test_spawn_async_cwd_explicit(void)
{
    char* const test_dir = libtest_sandbox_create();
    char* const result_path = tr_sys_path_native_separators(tr_buildPath(test_dir, "result.txt", NULL));

    char* const args[] =
    {
        self_path,
        result_path,
        arg_dump_cwd,
        NULL
    };

    tr_error* error = NULL;
    bool const ret = tr_spawn_async(args, NULL, test_dir, &error);
    check_bool(ret, ==, true);
    check_ptr(error, ==, NULL);

    while (!tr_sys_path_exists(result_path, NULL))
    {
        tr_wait_msec(10);
    }

    tr_sys_file_t fd = tr_sys_file_open(result_path, TR_SYS_FILE_READ, 0, NULL);
    check_int(fd, !=, TR_BAD_SYS_FILE);

    char buffer[1024];

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(tr_sys_path_native_separators(buffer), ==, tr_sys_path_native_separators(test_dir));

    check(!tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));

    tr_sys_file_close(fd, NULL);

    tr_free(result_path);
    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

static int test_spawn_async_cwd_inherit(void)
{
    char* const test_dir = libtest_sandbox_create();
    char* const result_path = tr_sys_path_native_separators(tr_buildPath(test_dir, "result.txt", NULL));

    char* const expected_cwd = tr_sys_dir_get_current(NULL);

    char* const args[] =
    {
        self_path,
        result_path,
        arg_dump_cwd,
        NULL
    };

    tr_error* error = NULL;
    bool const ret = tr_spawn_async(args, NULL, NULL, &error);
    check_bool(ret, ==, true);
    check_ptr(error, ==, NULL);

    while (!tr_sys_path_exists(result_path, NULL))
    {
        tr_wait_msec(10);
    }

    tr_sys_file_t fd = tr_sys_file_open(result_path, TR_SYS_FILE_READ, 0, NULL);
    check_int(fd, !=, TR_BAD_SYS_FILE);

    char buffer[1024];

    check(tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));
    check_str(tr_sys_path_native_separators(buffer), ==, tr_sys_path_native_separators(expected_cwd));

    check(!tr_sys_file_read_line(fd, buffer, sizeof(buffer), NULL));

    tr_sys_file_close(fd, NULL);

    tr_free(expected_cwd);
    tr_free(result_path);
    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

static int test_spawn_async_cwd_missing(void)
{
    char* const test_dir = libtest_sandbox_create();
    char* const result_path = tr_sys_path_native_separators(tr_buildPath(test_dir, "result.txt", NULL));

    char* const args[] =
    {
        self_path,
        result_path,
        arg_dump_cwd,
        NULL
    };

    tr_error* error = NULL;
    bool const ret = tr_spawn_async(args, NULL, TR_IF_WIN32("C:\\", "/") "tr-missing-test-work-dir", &error);
    check_bool(ret, ==, false);
    check_ptr(error, !=, NULL);
    check_int(error->code, !=, 0);
    check_str(error->message, !=, NULL);

    tr_error_clear(&error);

    tr_free(result_path);
    libtest_sandbox_destroy(test_dir);
    tr_free(test_dir);
    return 0;
}

int main(int argc, char** argv)
{
    self_path = tr_sys_path_resolve(argv[0], NULL);

    if (argc >= 3)
    {
        char* const result_path = argv[1];
        char* const test_action = argv[2];

        char* const tmp_result_path = tr_strdup_printf("%s.tmp", result_path);

        tr_sys_file_t const fd = tr_sys_file_open(tmp_result_path, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE |
            TR_SYS_FILE_TRUNCATE, 0644, NULL);

        if (fd == TR_BAD_SYS_FILE)
        {
            tr_free(tmp_result_path);
            return 1;
        }

        if (strcmp(test_action, arg_dump_args) == 0)
        {
            for (int i = 3; i < argc; ++i)
            {
                tr_sys_file_write_line(fd, argv[i], NULL);
            }
        }
        else if (strcmp(test_action, arg_dump_env) == 0)
        {
            for (int i = 3; i < argc; ++i)
            {
                char* const value = tr_env_get_string(argv[i], "<null>");
                tr_sys_file_write_line(fd, value, NULL);
                tr_free(value);
            }
        }
        else if (strcmp(test_action, arg_dump_cwd) == 0)
        {
            char* const value = tr_sys_dir_get_current(NULL);
            tr_sys_file_write_line(fd, value != NULL ? value : "<null>", NULL);
            tr_free(value);
        }
        else
        {
            tr_sys_file_close(fd, NULL);
            tr_sys_path_remove(tmp_result_path, NULL);

            tr_free(tmp_result_path);
            return 1;
        }

        tr_sys_file_close(fd, NULL);
        tr_sys_path_rename(tmp_result_path, result_path, NULL);

        tr_free(tmp_result_path);
        return 0;
    }

    testFunc const tests[] =
    {
        test_spawn_async_missing_exe,
        test_spawn_async_args,
        test_spawn_async_env,
        test_spawn_async_cwd_explicit,
        test_spawn_async_cwd_inherit,
        test_spawn_async_cwd_missing
    };

    int ret = runTests(tests, NUM_TESTS(tests));

#ifdef _WIN32

    strcpy(self_path + strlen(self_path) - 4, ".cmd");

    int ret2 = runTests(tests, NUM_TESTS(tests));

    if (ret == 0)
    {
        ret = ret2;
    }

#endif

    tr_free(self_path);
    return ret;
}
