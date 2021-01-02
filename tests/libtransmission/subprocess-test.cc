/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "subprocess.h"
#include "utils.h"

#include "gtest/internal/gtest-port.h" // GetArgvs()

#include "test-fixtures.h"

#include <array>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#endif

namespace libtransmission
{

namespace test
{

std::string getSelfPath()
{
    auto const exec = ::testing::internal::GetArgvs().front();
    return makeString(tr_sys_path_resolve(exec.data(), nullptr));
}

std::string getCmdSelfPath()
{
    auto const new_suffix = std::string { ".cmd" };
    auto exec = getSelfPath();
    // replace ".exe" suffix with ".cmd"
    exec.resize(exec.size() - new_suffix.size());
    exec.append(new_suffix.data(), new_suffix.size());
    return exec;
}

class SubprocessTest :
    public ::testing::Test,
    public testing::WithParamInterface<std::string>
{
protected:
    Sandbox sandbox_;

    [[nodiscard]] std::string buildSandboxPath(std::string const& filename) const
    {
        auto path = sandbox_.path();
        path += TR_PATH_DELIMITER;
        path += filename;
        tr_sys_path_native_separators(&path.front());
        return path;
    }

    [[nodiscard]] static std::string nativeCwd()
    {
        auto path = makeString(tr_sys_dir_get_current(nullptr));
        tr_sys_path_native_separators(&path.front());
        return path;
    }

    std::string const arg_dump_args_ { "--dump-args" };
    std::string const arg_dump_env_ { "--dump-env" };
    std::string const arg_dump_cwd_ { "--dump-cwd" };

    std::string self_path_;

    // If command-line args were passed in, then this test is being
    // invoked as a subprocess: it should dump the info requested by
    // the command-line flags and then exit without running tests.
    // FIXME: cleanup does not happen when we exit(). move this all
    // to a standalone file similar to the .cmd file on Windows
    void processCommandLineArgs() const
    {
        auto const argv = ::testing::internal::GetArgvs();
        if (argv.size() < 3)
        {
            return;
        }

        auto const& result_path = argv[1];
        auto const& test_action = argv[2];
        auto const tmp_result_path = result_path + ".tmp";

        auto fd = tr_sys_file_open(tmp_result_path.data(), // NOLINT
            TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
            0644, nullptr);

        if (fd == TR_BAD_SYS_FILE)
        {
            exit(1);
        }

        if (test_action == arg_dump_args_)
        {
            for (size_t i = 3; i < argv.size(); ++i)
            {
                tr_sys_file_write_line(fd, argv[i].data(), nullptr);
            }
        }
        else if (test_action == arg_dump_env_)
        {
            for (size_t i = 3; i < argv.size(); ++i)
            {
                auto const value = makeString(tr_env_get_string(argv[i].data(), "<null>"));
                tr_sys_file_write_line(fd, value.data(), nullptr);
            }
        }
        else if (test_action == arg_dump_cwd_)
        {
            char* const value = tr_sys_dir_get_current(nullptr);
            tr_sys_file_write_line(fd, value != nullptr ? value : "<null>", nullptr);
            tr_free(value);
        }
        else
        {
            tr_sys_file_close(fd, nullptr);
            tr_sys_path_remove(tmp_result_path.data(), nullptr);
            exit(1);
        }

        tr_sys_file_close(fd, nullptr);
        tr_sys_path_rename(tmp_result_path.data(), result_path.data(), nullptr);
        exit(0);
    }

    void waitForFileToExist(std::string const& path)
    {
        auto const test = [path]() { return tr_sys_path_exists(path.data(), nullptr); };
        EXPECT_TRUE(waitFor(test, 30000));
    }

    void SetUp() override
    {
        processCommandLineArgs();
        self_path_ = GetParam();
    }
};

TEST_P(SubprocessTest, SpawnAsyncMissingExec)
{
    auto const missing_exe_path = std::string { TR_IF_WIN32("C:\\", "/") "tr-missing-test-exe" TR_IF_WIN32(".exe", "") };

    auto args = std::array<char*, 2>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(missing_exe_path.data()),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(args.data(), nullptr, nullptr, &error);
    EXPECT_FALSE(ret);
    EXPECT_NE(nullptr, error);
    EXPECT_NE(0, error->code);
    EXPECT_NE(nullptr, error->message);

    tr_error_clear(&error);
}

TEST_P(SubprocessTest, SpawnAsyncArgs)
{
    auto const result_path = buildSandboxPath("result.txt");
    bool const allow_batch_metachars = TR_IF_WIN32(false, true) || !tr_str_has_suffix(self_path_.c_str(), ".cmd");

    auto const test_arg1 = std::string { "arg1 " };
    auto const test_arg2 = std::string { " arg2" };
    auto const test_arg3 = std::string {};
    auto const test_arg4 = std::string { "\"arg3'^! $PATH %PATH% \\" };

    auto args = std::array<char*, 8>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(result_path.data()),
        tr_strdup(arg_dump_args_.data()),
        tr_strdup(test_arg1.data()),
        tr_strdup(test_arg2.data()),
        tr_strdup(test_arg3.data()),
        tr_strdup(allow_batch_metachars ? test_arg4.data() : nullptr),
        nullptr
    };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args.data(), nullptr, nullptr, &error);
    EXPECT_TRUE(ret) << args[0] << ' ' << args[1];
    EXPECT_EQ(nullptr, error) << error->code << ", " << error->message;

    waitForFileToExist(result_path);

    auto fd = tr_sys_file_open(result_path.data(), TR_SYS_FILE_READ, 0, nullptr); // NOLINT
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(test_arg1, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(test_arg2, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(test_arg3, buffer.data());

    if (allow_batch_metachars)
    {
        EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
        EXPECT_EQ(test_arg4, buffer.data());
    }

    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, SpawnAsyncEnv)
{
    auto const result_path = buildSandboxPath("result.txt");

    auto const test_env_key1 = std::string { "VAR1" };
    auto const test_env_key2 = std::string { "_VAR_2_" };
    auto const test_env_key3 = std::string { "vAr#" };
    auto const test_env_key4 = std::string { "FOO" };
    auto const test_env_key5 = std::string { "ZOO" };
    auto const test_env_key6 = std::string { "TR_MISSING_TEST_ENV_KEY" };

    auto const test_env_value1 = std::string { "value1 " };
    auto const test_env_value2 = std::string { " value2" };
    auto const test_env_value3 = std::string { " \"value3'^! $PATH %PATH% " };
    auto const test_env_value4 = std::string { "bar" };
    auto const test_env_value5 = std::string { "jar" };

    auto args = std::array<char*, 10>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(result_path.data()),
        tr_strdup(arg_dump_env_.data()),
        tr_strdup(test_env_key1.data()),
        tr_strdup(test_env_key2.data()),
        tr_strdup(test_env_key3.data()),
        tr_strdup(test_env_key4.data()),
        tr_strdup(test_env_key5.data()),
        tr_strdup(test_env_key6.data()),
        nullptr
    };

    auto env = std::array<char*, 5>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup_printf("%s=%s", test_env_key1.data(), test_env_value1.data()),
        tr_strdup_printf("%s=%s", test_env_key2.data(), test_env_value2.data()),
        tr_strdup_printf("%s=%s", test_env_key3.data(), test_env_value3.data()),
        tr_strdup_printf("%s=%s", test_env_key5.data(), test_env_value5.data()),
        nullptr
    };

    setenv("FOO", "bar", true); // inherited
    setenv("ZOO", "tar", true); // overridden

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args.data(), env.data(), nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);

    auto fd = tr_sys_file_open(result_path.data(), TR_SYS_FILE_READ, 0, nullptr); // NOLINT
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(test_env_value1, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(test_env_value2, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(test_env_value3, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(test_env_value4, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(test_env_value5, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_STREQ("<null>", buffer.data());

    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));

    tr_sys_file_close(fd, nullptr);

    for (auto& env_item : env)
    {
        tr_free(env_item);
    }
}

TEST_P(SubprocessTest, SpawnAsyncCwdExplicit)
{
    auto const test_dir = sandbox_.path();
    auto const result_path = buildSandboxPath("result.txt");

    auto args = std::array<char*, 4>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(result_path.data()),
        tr_strdup(arg_dump_cwd_.data()),
        nullptr
    };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args.data(), nullptr, test_dir.c_str(), &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);

    auto fd = tr_sys_file_open(result_path.data(), TR_SYS_FILE_READ, 0, nullptr); // NOLINT
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(makeString(tr_sys_path_native_separators(tr_strdup(test_dir.c_str()))),
        tr_sys_path_native_separators(&buffer.front()));

    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, SpawnAsyncCwdInherit)
{
    auto const result_path = buildSandboxPath("result.txt");
    auto const expected_cwd = nativeCwd();

    auto args = std::array<char*, 4>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(result_path.data()),
        tr_strdup(arg_dump_cwd_.data()),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(args.data(), nullptr, nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);
    auto fd = tr_sys_file_open(result_path.data(), TR_SYS_FILE_READ, 0, nullptr); // NOLINT
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(expected_cwd, tr_sys_path_native_separators(&buffer.front()));
    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, SpawnAsyncCwdMissing)
{
    auto const result_path = buildSandboxPath("result.txt");

    auto args = std::array<char*, 4>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(result_path.data()),
        tr_strdup(arg_dump_cwd_.data()),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(args.data(), nullptr, TR_IF_WIN32("C:\\", "/") "tr-missing-test-work-dir", &error);
    EXPECT_FALSE(ret);
    EXPECT_NE(nullptr, error);
    EXPECT_NE(0, error->code);
    EXPECT_NE(nullptr, error->message);
    tr_error_clear(&error);
}

#ifdef _WIN32
INSTANTIATE_TEST_SUITE_P(
    Subprocess,
    SubprocessTest,
    ::testing::Values(getSelfPath(), getCmdSelfPath())
    );
#else
INSTANTIATE_TEST_SUITE_P(
    Subprocess,
    SubprocessTest,
    ::testing::Values(getSelfPath())
    );
#endif

} // namespace test

} // namespace libtransmission
