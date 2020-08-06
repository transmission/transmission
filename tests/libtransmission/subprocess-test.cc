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
#if !defined(__has_include) || __has_include(<string_view>)
# include <string_view>
#else
# include <experimental/string_view>
# define string_view experimental::string_view
#endif

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

namespace libtransmission::test
{

std::string getSelfPath()
{
    auto const exec = ::testing::internal::GetArgvs().front();
    return makeString(tr_sys_path_resolve(exec.data(), nullptr));
}

std::string getCmdSelfPath()
{
    auto constexpr NewSuffix = std::string_view { ".cmd" };
    auto exec = getSelfPath();
    // replace ".exe" suffix with ".cmd"
    exec.resize(exec.size() - NewSuffix.size());
    exec.append(NewSuffix.data(), NewSuffix.size());
    return exec;
}

class SubprocessTest :
    public ::testing::Test,
    public testing::WithParamInterface<std::string>
{
protected:
    Sandbox sandbox_;

    [[nodiscard]] std::string buildSandboxPath(std::string_view filename) const
    {
        auto* tmp = tr_strdup_printf("%s%c%*.*s",
            sandbox_.path().c_str(), TR_PATH_DELIMITER,
            int(filename.size()), int(filename.size()), filename.data());
        tr_sys_path_native_separators(tmp);
        auto const path = std::string { tmp };
        tr_free(tmp);
        return path;
    }

    [[nodiscard]] static std::string nativeCwd()
    {
        auto* tmp = tr_sys_dir_get_current(nullptr);
        tr_sys_path_native_separators(tmp);
        auto const path = std::string { tmp };
        tr_free(tmp);
        return path;
    }

    static auto constexpr ArgDumpArgs = std::string_view{ "--dump-args" };
    static auto constexpr ArgDumpEnv = std::string_view{ "--dump-env" };
    static auto constexpr ArgDumpCwd = std::string_view{ "--dump-cwd" };

    std::string self_path_;

    // If command-line args were passed in, then this test is being
    // invoked as a subprocess: it should dump the info requested by
    // the command-line flags and then exit without running tests.
    // FIXME: cleanup does not happen when we exit(). move this all
    // to a standalone file similar to the .cmd file on Windows
    static void processCommandLineArgs()
    {
        auto const argv = ::testing::internal::GetArgvs();
        if (argv.size() < 3)
        {
            return;
        }

        auto const& result_path = argv[1];
        auto const& test_action = argv[2];
        auto const tmp_result_path = result_path + ".tmp";

        auto const fd = tr_sys_file_open(tmp_result_path.data(),
            TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
            0644, nullptr);

        if (fd == TR_BAD_SYS_FILE)
        {
            exit(1);
        }

        if (test_action == ArgDumpArgs)
        {
            for (int i = 3; i < int(argv.size()); ++i)
            {
                tr_sys_file_write_line(fd, argv[i].data(), nullptr);
            }
        }
        else if (test_action == ArgDumpEnv)
        {
            for (int i = 3; i < int(argv.size()); ++i)
            {
                auto const value = makeString(tr_env_get_string(argv[i].data(), "<null>"));
                tr_sys_file_write_line(fd, value.data(), nullptr);
            }
        }
        else if (test_action == ArgDumpCwd)
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
        EXPECT_TRUE(waitFor(test, 2000));
    }

    void SetUp() override
    {
        processCommandLineArgs();
        self_path_ = GetParam();
    }
};

TEST_P(SubprocessTest, SpawnAsyncMissingExec)
{
    auto constexpr MissingExePath = std::string_view { TR_IF_WIN32("C:\\", "/") "tr-missing-test-exe" TR_IF_WIN32(".exe", "") };

    auto args = std::array<char*, 2>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(MissingExePath.data()),
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
    bool const allow_batch_metachars = TR_IF_WIN32(false, true) || tr_str_has_suffix(self_path_.c_str(), ".cmd");

    auto constexpr TestArg1 = std::string_view { "arg1 " };
    auto constexpr TestArg2 = std::string_view { " arg2" };
    auto constexpr TestArg3 = std::string_view { "" };
    auto constexpr TestArg4 = std::string_view { "\"arg3'^! $PATH %PATH% \\" };

    auto args = std::array<char*, 8>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(result_path.data()),
        tr_strdup(ArgDumpArgs.data()),
        tr_strdup(TestArg1.data()),
        tr_strdup(TestArg2.data()),
        tr_strdup(TestArg3.data()),
        tr_strdup(allow_batch_metachars ? TestArg4.data() : nullptr),
        nullptr
    };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args.data(), nullptr, nullptr, &error);
    EXPECT_TRUE(ret) << args[0] << ' ' << args[1];
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);

    auto const fd = tr_sys_file_open(result_path.data(), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(TestArg1, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(TestArg2, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(TestArg3, buffer.data());

    if (allow_batch_metachars)
    {
        EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
        EXPECT_EQ(TestArg4, buffer.data());
    }

    EXPECT_FALSE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, SpawnAsyncEnv)
{
    auto const result_path = buildSandboxPath("result.txt");

    static auto constexpr TestEnvKey1 = std::string_view { "VAR1" };
    static auto constexpr TestEnvKey2 = std::string_view { "_VAR_2_" };
    static auto constexpr TestEnvKey3 = std::string_view { "vAr#" };
    static auto constexpr TestEnvKey4 = std::string_view { "FOO" };
    static auto constexpr TestEnvKey5 = std::string_view { "ZOO" };
    static auto constexpr TestEnvKey6 = std::string_view { "TR_MISSING_TEST_ENV_KEY" };

    static auto constexpr TestEnvValue1 = std::string_view { "value1 " };
    static auto constexpr TestEnvValue2 = std::string_view { " value2" };
    static auto constexpr TestEnvValue3 = std::string_view { " \"value3'^! $PATH %PATH% " };
    static auto constexpr TestEnvValue4 = std::string_view { "bar" };
    static auto constexpr TestEnvValue5 = std::string_view { "jar" };

    auto args = std::array<char*, 10>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(result_path.data()),
        tr_strdup(ArgDumpEnv.data()),
        tr_strdup(TestEnvKey1.data()),
        tr_strdup(TestEnvKey2.data()),
        tr_strdup(TestEnvKey3.data()),
        tr_strdup(TestEnvKey4.data()),
        tr_strdup(TestEnvKey5.data()),
        tr_strdup(TestEnvKey6.data()),
        nullptr
    };

    auto env = std::array<char*, 5>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup_printf("%s=%s", TestEnvKey1.data(), TestEnvValue1.data()),
        tr_strdup_printf("%s=%s", TestEnvKey2.data(), TestEnvValue2.data()),
        tr_strdup_printf("%s=%s", TestEnvKey3.data(), TestEnvValue3.data()),
        tr_strdup_printf("%s=%s", TestEnvKey5.data(), TestEnvValue5.data()),
        nullptr
    };

    setenv("FOO", "bar", true); // inherited
    setenv("ZOO", "tar", true); // overridden

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args.data(), env.data(), nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);

    auto const fd = tr_sys_file_open(result_path.data(), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(TestEnvValue1, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(TestEnvValue2, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(TestEnvValue3, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(TestEnvValue4, buffer.data());

    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(TestEnvValue5, buffer.data());

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
        tr_strdup(ArgDumpCwd.data()),
        nullptr
    };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(args.data(), nullptr, test_dir.c_str(), &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);

    auto const fd = tr_sys_file_open(result_path.data(), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(makeString(tr_sys_path_native_separators(tr_strdup(test_dir.c_str()))),
        tr_sys_path_native_separators(buffer.data()));

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
        tr_strdup(ArgDumpCwd.data()),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(args.data(), nullptr, nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);
    auto const fd = tr_sys_file_open(result_path.data(), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, buffer.data(), buffer.size(), nullptr));
    EXPECT_EQ(expected_cwd, tr_sys_path_native_separators(buffer.data()));
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
        tr_strdup(ArgDumpCwd.data()),
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

} // namespace libtransmission::test
