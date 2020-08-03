/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstdlib>
#include <string>
#include <string_view>

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "subprocess.h"
#include "utils.h"

#include "gtest/internal/gtest-port.h" // GetArgvs()
#include "libtransmission-test.h"

#include "test-fixtures.h"

namespace libtransmission::test
{

std::string get_self_path()
{
    auto const exec = ::testing::internal::GetArgvs().front();
    return make_string(tr_sys_path_resolve(std::data(exec), nullptr));
}

std::string get_cmd_self_path()
{
    auto constexpr new_suffix = std::string_view {".cmd"};
    auto exec = get_self_path();
    // replace ".exe" suffix with ".cmd"
    exec.resize(std::size(exec) - std::size(new_suffix));
    exec += new_suffix;
    return exec;
}

class SubprocessTest : public ::testing::TestWithParam<std::string>
{
protected:
    Sandbox sandbox_;

    std::string build_sandbox_path(std::string_view filename) const
    {
        auto path = sandbox_.path();
        path += TR_PATH_DELIMITER;
        path += filename;
        tr_sys_path_native_separators(std::data(path));
        return path;
    }

    static std::string native_cwd()
    {
        auto path = make_string(tr_sys_dir_get_current(nullptr));
        tr_sys_path_native_separators(std::data(path));
        return path;
    }

    static auto constexpr arg_dump_args = std::string_view{ "--dump-args" };
    static auto constexpr arg_dump_env = std::string_view{ "--dump-env" };
    static auto constexpr arg_dump_cwd = std::string_view{ "--dump-cwd" };

    std::string self_path_;

    // If command-line args were passed in, then this test is being
    // invoked as a subprocess: it should dump the info requested by
    // the command-line flags and then exit without running tests.
    // FIXME: cleanup does not happen when we exit(). move this all
    // to a standalone file similar to the .cmd file on Windows
    static void process_command_line_args()
    {
        auto const argv = ::testing::internal::GetArgvs();
        if (std::size(argv) < 3)
        {
            return;
        }

        auto const& result_path = argv[1];
        auto const& test_action = argv[2];
        auto const tmp_result_path = result_path + ".tmp";

        auto const fd = tr_sys_file_open(std::data(tmp_result_path),
                TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
                0644, nullptr);

        if (fd == TR_BAD_SYS_FILE)
        {
            exit(1);
        }

        if (test_action == arg_dump_args)
        {
            for (int i = 3; i < int(std::size(argv)); ++i)
            {
                tr_sys_file_write_line(fd, std::data(argv[i]), nullptr);
            }
        }
        else if (test_action == arg_dump_env)
        {
            for (int i = 3; i < int(std::size(argv)); ++i)
            {
                auto const value = make_string(tr_env_get_string(std::data(argv[i]), "<null>"));
                tr_sys_file_write_line(fd, std::data(value), nullptr);
            }
        }
        else if (test_action == arg_dump_cwd)
        {
            char* const value = tr_sys_dir_get_current(nullptr);
            tr_sys_file_write_line(fd, value != nullptr ? value : "<null>", nullptr);
            tr_free(value);
        }
        else
        {
            tr_sys_file_close(fd, nullptr);
            tr_sys_path_remove(std::data(tmp_result_path), nullptr);
            exit(1);
        }

        tr_sys_file_close(fd, nullptr);
        tr_sys_path_rename(std::data(tmp_result_path), std::data(result_path), nullptr);
        exit(0);
    }

    void wait_for_file(std::string const& path)
    {
        auto const test = [path](){ return tr_sys_path_exists(std::data(path), nullptr); };
        EXPECT_TRUE(wait_for(test, 2000));
    }

    virtual void SetUp() override
    {
        process_command_line_args();
        self_path_ = GetParam();
    }
};

TEST_P(SubprocessTest, spawn_async_missing_exec)
{
    char missing_exe_path[] = TR_IF_WIN32("C:\\", "/") "tr-missing-test-exe" TR_IF_WIN32(".exe", "");

    char* const args[] =
    {
        missing_exe_path,
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(args, nullptr, nullptr, &error);
    EXPECT_FALSE(ret);
    EXPECT_NE(nullptr, error);
    EXPECT_NE(0, error->code);
    EXPECT_NE(nullptr, error->message);

    tr_error_clear(&error);
}

TEST_P(SubprocessTest, spawn_async_args)
{
    auto const result_path = build_sandbox_path("result.txt");
    bool const allow_batch_metachars = TR_IF_WIN32(false, true) || tr_str_has_suffix(std::data(self_path_), ".cmd");

    auto constexpr test_arg_1 = std::string_view { "arg1 " };
    auto constexpr test_arg_2 = std::string_view { " arg2" };
    auto constexpr test_arg_3 = std::string_view { "" };
    auto constexpr test_arg_4 = std::string_view { "\"arg3'^! $PATH %PATH% \\" };

    char* const args[] =
    {
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(std::data(self_path_)),
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(arg_dump_args)),
        tr_strdup(std::data(test_arg_1)),
        tr_strdup(std::data(test_arg_2)),
        tr_strdup(std::data(test_arg_3)),
        tr_strdup(allow_batch_metachars ? std::data(test_arg_4) : nullptr),
        nullptr
    };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args, nullptr, nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    wait_for_file(result_path);

    auto const fd = tr_sys_file_open(std::data(result_path), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    char buffer[1024];

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_EQ(test_arg_1, buffer);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_EQ(test_arg_2, buffer);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_EQ(test_arg_3, buffer);

    if (allow_batch_metachars)
    {
        EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
        EXPECT_EQ(test_arg_4, buffer);
    }

    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, spawn_async_env)
{
    auto const result_path = build_sandbox_path("result.txt");

    static constexpr char test_env_key_1[] = "VAR1";
    static constexpr char test_env_key_2[] = "_VAR_2_";
    static constexpr char test_env_key_3[] = "vAr#";
    static constexpr char test_env_key_4[] = "FOO";
    static constexpr char test_env_key_5[] = "ZOO";
    static constexpr char test_env_key_6[] = "TR_MISSING_TEST_ENV_KEY";

    static constexpr char test_env_value_1[] = "value1 ";
    static constexpr char test_env_value_2[] = " value2";
    static constexpr char test_env_value_3[] = " \"value3'^! $PATH %PATH% ";
    static constexpr char test_env_value_4[] = "bar";
    static constexpr char test_env_value_5[] = "jar";

    char* const args[] =
    {
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(std::data(self_path_)),
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(arg_dump_env)),
        tr_strdup(test_env_key_1),
        tr_strdup(test_env_key_2),
        tr_strdup(test_env_key_3),
        tr_strdup(test_env_key_4),
        tr_strdup(test_env_key_5),
        tr_strdup(test_env_key_6),
        nullptr
    };

    char* const env[] =
    {
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup_printf("%s=%s", test_env_key_1, test_env_value_1),
        tr_strdup_printf("%s=%s", test_env_key_2, test_env_value_2),
        tr_strdup_printf("%s=%s", test_env_key_3, test_env_value_3),
        tr_strdup_printf("%s=%s", test_env_key_5, test_env_value_5),
        nullptr
    };

    // inherited
    char foo_env_value[] = "FOO=bar";
    putenv(foo_env_value);

    // overridden
    char zoo_env_value[] = "ZOO=tar";
    putenv(zoo_env_value);

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args, env, nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    wait_for_file(result_path);

    auto const fd = tr_sys_file_open(std::data(result_path), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    char buffer[1024];

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_STREQ(test_env_value_1, buffer);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_STREQ(test_env_value_2, buffer);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_STREQ(test_env_value_3, buffer);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_STREQ(test_env_value_4, buffer);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_STREQ(test_env_value_5, buffer);

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_STREQ("<null>", buffer);

    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));

    tr_sys_file_close(fd, nullptr);

    tr_free_ptrv((void* const*)env);
}

TEST_P(SubprocessTest, spawn_async_cwd_explicit)
{
    auto test_dir = sandbox_.path();
    auto const result_path = build_sandbox_path("result.txt");

    char* const args[] =
    {
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(std::data(self_path_)),
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(arg_dump_cwd)),
        nullptr
    };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args, nullptr, std::data(test_dir), &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    wait_for_file(result_path);

    auto const fd = tr_sys_file_open(std::data(result_path), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    char buffer[1024];
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    tr_sys_path_native_separators(std::data(test_dir));
    EXPECT_EQ(test_dir, tr_sys_path_native_separators(buffer));

    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, spawn_async_cwd_inherit)
{
    auto const result_path = build_sandbox_path("result.txt");
    auto const expected_cwd = native_cwd();

    char* const args[] =
    {
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(std::data(self_path_)),
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(arg_dump_cwd)),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(args, nullptr, nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    wait_for_file(result_path);
    auto const fd = tr_sys_file_open(std::data(result_path), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    char buffer[1024];
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));
    EXPECT_EQ(expected_cwd, tr_sys_path_native_separators(buffer));
    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer, sizeof(buffer), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, spawn_async_cwd_missing)
{
    auto const result_path = build_sandbox_path("result.txt");

    char* const args[] =
    {
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(std::data(self_path_)),
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(arg_dump_cwd)),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(args, nullptr, TR_IF_WIN32("C:\\", "/") "tr-missing-test-work-dir", &error);
    EXPECT_FALSE(ret);
    EXPECT_NE(nullptr, error);
    EXPECT_NE(0, error->code);
    EXPECT_NE(nullptr, error->message);
    tr_error_clear(&error);
}

INSTANTIATE_TEST_SUITE_P(
    Subprocess,
    SubprocessTest,
#ifdef _WIN32
    ::testing::Values(get_self_path(), get_cmd_self_path())
#else
    ::testing::Values(get_self_path())
#endif
);


}  // namespace libtransmission::test
