// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdlib>
#include <string_view>

#include "transmission.h"

#include "tr-strbuf.h"

#include "test-fixtures.h"

using namespace std::literals;
using PlatformTest = ::libtransmission::test::SandboxedTest;
using ::libtransmission::test::makeString;

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

TEST_F(PlatformTest, defaultDownloadDirXdg)
{
    setenv("HOME", sandboxDir().c_str(), 1);
    setenv("XDG_CONFIG_HOME", LIBTRANSMISSION_TEST_ASSETS_DIR, 1);

    auto actual = makeString(tr_getDefaultDownloadDir());
    auto expected = fmt::format("{:s}/UserDirsDownloads"sv, sandboxDir());
    EXPECT_EQ(expected, actual);

    unsetenv("XDG_CONFIG_HOME");
    unsetenv("HOME");
}

#if !defined(_WIN32) && !defined(__HAIKU__)
TEST_F(PlatformTest, defaultDownloadDir)
{
    setenv("HOME", sandboxDir().c_str(), 1);

    auto actual = makeString(tr_getDefaultDownloadDir());
    auto expected = fmt::format("{:s}/Downloads"sv, sandboxDir());
    EXPECT_EQ(expected, actual);

    unsetenv("HOME");
}
#endif

TEST_F(PlatformTest, defaultConfigDirEnv)
{
    setenv("TRANSMISSION_HOME", sandboxDir().c_str(), 1);

    auto actual = std::string{ tr_getDefaultConfigDir("appname") };
    auto expected = sandboxDir();
    EXPECT_EQ(expected, actual);

    unsetenv("TRANSMISSION_HOME");
}

// #if !defined(__APPLE__) && !defined(_WIN32) && !defined(__HAIKU__)
