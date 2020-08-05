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
    return makeString(tr_sys_path_resolve(std::data(exec), nullptr));
}

std::string getCmdSelfPath()
{
    auto constexpr NewSuffix = std::string_view { ".cmd" };
    auto exec = getSelfPath();
    // replace ".exe" suffix with ".cmd"
    exec.resize(std::size(exec) - std::size(NewSuffix));
    exec.append(std::data(NewSuffix), std::size(NewSuffix));
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
            int(std::size(filename)), int(std::size(filename)), std::data(filename));
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

        if (test_action == ArgDumpArgs)
        {
            for (int i = 3; i < int(std::size(argv)); ++i)
            {
                tr_sys_file_write_line(fd, std::data(argv[i]), nullptr);
            }
        }
        else if (test_action == ArgDumpEnv)
        {
            for (int i = 3; i < int(std::size(argv)); ++i)
            {
                auto const value = makeString(tr_env_get_string(std::data(argv[i]), "<null>"));
                tr_sys_file_write_line(fd, std::data(value), nullptr);
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
            tr_sys_path_remove(std::data(tmp_result_path), nullptr);
            exit(1);
        }

        tr_sys_file_close(fd, nullptr);
        tr_sys_path_rename(std::data(tmp_result_path), std::data(result_path), nullptr);
        exit(0);
    }

    void waitForFileToExist(std::string const& path)
    {
        auto const test = [path]() { return tr_sys_path_exists(std::data(path), nullptr); };
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
        tr_strdup(std::data(MissingExePath)),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(std::data(args), nullptr, nullptr, &error);
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
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(ArgDumpArgs)),
        tr_strdup(std::data(TestArg1)),
        tr_strdup(std::data(TestArg2)),
        tr_strdup(std::data(TestArg3)),
        tr_strdup(allow_batch_metachars ? std::data(TestArg4) : nullptr),
        nullptr
    };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(std::data(args), nullptr, nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);

    auto const fd = tr_sys_file_open(std::data(result_path), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(TestArg1, std::data(buffer));

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(TestArg2, std::data(buffer));

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(TestArg3, std::data(buffer));

    if (allow_batch_metachars)
    {
        EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
        EXPECT_EQ(TestArg4, std::data(buffer));
    }

    EXPECT_FALSE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));

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
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(ArgDumpEnv)),
        tr_strdup(std::data(TestEnvKey1)),
        tr_strdup(std::data(TestEnvKey2)),
        tr_strdup(std::data(TestEnvKey3)),
        tr_strdup(std::data(TestEnvKey4)),
        tr_strdup(std::data(TestEnvKey5)),
        tr_strdup(std::data(TestEnvKey6)),
        nullptr
    };

    auto env = std::array<char*, 5>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup_printf("%s=%s", std::data(TestEnvKey1), std::data(TestEnvValue1)),
        tr_strdup_printf("%s=%s", std::data(TestEnvKey2), std::data(TestEnvValue2)),
        tr_strdup_printf("%s=%s", std::data(TestEnvKey3), std::data(TestEnvValue3)),
        tr_strdup_printf("%s=%s", std::data(TestEnvKey5), std::data(TestEnvValue5)),
        nullptr
    };

    setenv("FOO", "bar", true); // inherited
    setenv("ZOO", "tar", true); // overridden

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(std::data(args), std::data(env), nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);

    auto const fd = tr_sys_file_open(std::data(result_path), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(TestEnvValue1, std::data(buffer));

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(TestEnvValue2, std::data(buffer));

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(TestEnvValue3, std::data(buffer));

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(TestEnvValue4, std::data(buffer));

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(TestEnvValue5, std::data(buffer));

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_STREQ("<null>", std::data(buffer));

    EXPECT_FALSE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));

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
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(ArgDumpCwd)),
        nullptr
    };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(std::data(args), nullptr, test_dir.c_str(), &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);

    auto const fd = tr_sys_file_open(std::data(result_path), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(makeString(tr_sys_path_native_separators(tr_strdup(test_dir.c_str()))),
        tr_sys_path_native_separators(std::data(buffer)));

    EXPECT_FALSE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, SpawnAsyncCwdInherit)
{
    auto const result_path = buildSandboxPath("result.txt");
    auto const expected_cwd = nativeCwd();

    auto args = std::array<char*, 4>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(ArgDumpCwd)),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(std::data(args), nullptr, nullptr, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error);

    waitForFileToExist(result_path);
    auto const fd = tr_sys_file_open(std::data(result_path), TR_SYS_FILE_READ, 0, nullptr);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);

    auto buffer = std::array<char, 1024>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));
    EXPECT_EQ(expected_cwd, tr_sys_path_native_separators(std::data(buffer)));
    EXPECT_FALSE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), nullptr));

    tr_sys_file_close(fd, nullptr);
}

TEST_P(SubprocessTest, SpawnAsyncCwdMissing)
{
    auto const result_path = buildSandboxPath("result.txt");

    auto args = std::array<char*, 4>{
        //  FIXME(ckerr): remove tr_strdup()s after https://github.com/transmission/transmission/issues/1384
        tr_strdup(self_path_.c_str()),
        tr_strdup(std::data(result_path)),
        tr_strdup(std::data(ArgDumpCwd)),
        nullptr
    };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(std::data(args), nullptr, TR_IF_WIN32("C:\\", "/") "tr-missing-test-work-dir", &error);
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
