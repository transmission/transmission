// This file Copyright (C) 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/platform.h>
#include <libtransmission/subprocess.h>

#include "gtest/internal/gtest-port.h" // GetArgvs()

#include "test-fixtures.h"

#include <array>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#endif

namespace libtransmission::test
{

std::string getTestProgramPath(std::string const& filename)
{
    auto const exe_path = tr_sys_path_resolve(testing::internal::GetArgvs().front().data());
    auto const exe_dir = tr_sys_path_dirname(exe_path);
    return std::string{ exe_dir } + TR_PATH_DELIMITER + filename;
}

class SubprocessTest
    : public ::testing::Test
    , public testing::WithParamInterface<std::string>
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
        auto path = tr_sys_dir_get_current();
        tr_sys_path_native_separators(path.data());
        return path;
    }

    std::string const arg_dump_args_{ "--dump-args" };
    std::string const arg_dump_env_{ "--dump-env" };
    std::string const arg_dump_cwd_{ "--dump-cwd" };

    std::string self_path_;

    static void waitForFileToBeReadable(std::string const& path)
    {
        auto const test = [&path]()
        {
            return std::ifstream{ path, std::ios_base::in }.is_open();
        };
        EXPECT_TRUE(waitFor(test, 30000));
    }

    void SetUp() override
    {
        self_path_ = GetParam();
    }
};

TEST_P(SubprocessTest, SpawnAsyncMissingExec)
{
    auto const missing_exe_path = std::string{ TR_IF_WIN32("C:\\", "/") "tr-missing-test-exe" TR_IF_WIN32(".exe", "") };

    auto args = std::array<char const*, 2>{ missing_exe_path.data(), nullptr };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(std::data(args), {}, {}, &error);
    EXPECT_FALSE(ret);
    EXPECT_NE(nullptr, error);
    EXPECT_NE(0, error->code);
    EXPECT_NE(nullptr, error->message);

    tr_error_clear(&error);
}

TEST_P(SubprocessTest, SpawnAsyncArgs)
{
    auto const result_path = buildSandboxPath("result.txt");
    bool const allow_batch_metachars = TR_IF_WIN32(false, true) || !tr_strvEndsWith(tr_strlower(self_path_), ".cmd"sv);

    auto const test_arg1 = std::string{ "arg1 " };
    auto const test_arg2 = std::string{ " arg2" };
    auto const test_arg3 = std::string{};
    auto const test_arg4 = std::string{ "\"arg3'^! $PATH %PATH% \\" };

    auto const args = std::array<char const*, 8>{ self_path_.c_str(),
                                                  result_path.data(),
                                                  arg_dump_args_.data(),
                                                  test_arg1.data(),
                                                  test_arg2.data(),
                                                  test_arg3.data(),
                                                  allow_batch_metachars ? test_arg4.data() : nullptr,
                                                  nullptr };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(std::data(args), {}, {}, &error);
    EXPECT_TRUE(ret) << args[0] << ' ' << args[1];
    EXPECT_EQ(nullptr, error) << *error;

    waitForFileToBeReadable(result_path);

    auto in = std::ifstream{ result_path, std::ios_base::in };
    EXPECT_TRUE(in.is_open()) << strerror(errno);

    auto line = std::string{};
    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_arg1, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_arg2, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_arg3, line);

    if (allow_batch_metachars)
    {
        EXPECT_TRUE(std::getline(in, line));
        EXPECT_EQ(test_arg4, line);
    }

    EXPECT_FALSE(std::getline(in, line));
}

TEST_P(SubprocessTest, SpawnAsyncEnv)
{
    auto const result_path = buildSandboxPath("result.txt");

    auto const test_env_key1 = std::string{ "VAR1" };
    auto const test_env_key2 = std::string{ "_VAR_2_" };
    auto const test_env_key3 = std::string{ "vAr#" };
    auto const test_env_key4 = std::string{ "FOO" };
    auto const test_env_key5 = std::string{ "ZOO" };
    auto const test_env_key6 = std::string{ "TR_MISSING_TEST_ENV_KEY" };

    auto const test_env_value1 = std::string{ "value1 " };
    auto const test_env_value2 = std::string{ " value2" };
    auto const test_env_value3 = std::string{ " \"value3'^! $PATH %PATH% " };
    auto const test_env_value4 = std::string{ "bar" };
    auto const test_env_value5 = std::string{ "jar" };

    auto args = std::array<char const*, 10>{
        self_path_.c_str(), //
        result_path.data(), //
        arg_dump_env_.data(), //
        test_env_key1.data(), //
        test_env_key2.data(), //
        test_env_key3.data(), //
        test_env_key4.data(), //
        test_env_key5.data(), //
        test_env_key6.data(), //
        nullptr, //
    };

    auto const env = std::map<std::string_view, std::string_view>{
        { test_env_key1, test_env_value1 },
        { test_env_key2, test_env_value2 },
        { test_env_key3, test_env_value3 },
        { test_env_key5, test_env_value5 },
    };

    setenv("FOO", "bar", 1 /*true*/); // inherited
    setenv("ZOO", "tar", 1 /*true*/); // overridden

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(std::data(args), env, {}, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error) << *error;

    waitForFileToBeReadable(result_path);

    auto in = std::ifstream{ result_path, std::ios_base::in };
    EXPECT_TRUE(in.is_open()) << strerror(errno);

    auto line = std::string{};
    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value1, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value2, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value3, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value4, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ(test_env_value5, line);

    EXPECT_TRUE(std::getline(in, line));
    EXPECT_EQ("<null>"sv, line);

    EXPECT_FALSE(std::getline(in, line));
}

TEST_P(SubprocessTest, SpawnAsyncCwdExplicit)
{
    auto const test_dir = sandbox_.path();
    auto const result_path = buildSandboxPath("result.txt");

    auto const args = std::array<char const*, 4>{ self_path_.c_str(), result_path.c_str(), arg_dump_cwd_.c_str(), nullptr };

    tr_error* error = nullptr;
    bool const ret = tr_spawn_async(std::data(args), {}, test_dir, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error) << *error;

    waitForFileToBeReadable(result_path);

    auto in = std::ifstream{ result_path, std::ios_base::in };
    EXPECT_TRUE(in.is_open()) << strerror(errno);

    auto line = std::string{};
    EXPECT_TRUE(std::getline(in, line));
    auto expected = std::string{ test_dir };
    tr_sys_path_native_separators(std::data(expected));
    auto actual = line;
    tr_sys_path_native_separators(std::data(actual));
    EXPECT_EQ(expected, actual);

    EXPECT_FALSE(std::getline(in, line));
}

TEST_P(SubprocessTest, SpawnAsyncCwdInherit)
{
    auto const result_path = buildSandboxPath("result.txt");
    auto const expected_cwd = nativeCwd();

    auto const args = std::array<char const*, 4>{ self_path_.c_str(), result_path.data(), arg_dump_cwd_.data(), nullptr };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(std::data(args), {}, {}, &error);
    EXPECT_TRUE(ret);
    EXPECT_EQ(nullptr, error) << *error;

    waitForFileToBeReadable(result_path);

    auto in = std::ifstream{ result_path, std::ios_base::in };
    EXPECT_TRUE(in.is_open()) << strerror(errno);

    auto line = std::string{};
    EXPECT_TRUE(std::getline(in, line));
    auto actual = line;
    tr_sys_path_native_separators(std::data(actual));
    EXPECT_EQ(expected_cwd, actual);

    EXPECT_FALSE(std::getline(in, line));
}

TEST_P(SubprocessTest, SpawnAsyncCwdMissing)
{
    auto const result_path = buildSandboxPath("result.txt");

    auto const args = std::array<char const*, 4>{ self_path_.c_str(), result_path.data(), arg_dump_cwd_.data(), nullptr };

    tr_error* error = nullptr;
    auto const ret = tr_spawn_async(std::data(args), {}, TR_IF_WIN32("C:\\", "/") "tr-missing-test-work-dir", &error);
    EXPECT_FALSE(ret);
    EXPECT_NE(nullptr, error);
    EXPECT_NE(0, error->code);
    EXPECT_NE(nullptr, error->message);
    tr_error_clear(&error);
}

INSTANTIATE_TEST_SUITE_P(
    Subprocess,
    SubprocessTest,
    TR_IF_WIN32(
        ::testing::Values( //
            getTestProgramPath("subprocess-test.exe"),
            getTestProgramPath("subprocess-test.cmd")),
        ::testing::Values( //
            getTestProgramPath("subprocess-test"))));

} // namespace libtransmission::test
