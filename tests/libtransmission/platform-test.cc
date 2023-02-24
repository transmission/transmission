// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdlib>
#include <string_view>

#include <libtransmission/transmission.h>

#include <libtransmission/file.h>
#include <libtransmission/platform.h>
#include <libtransmission/tr-strbuf.h>

#include "test-fixtures.h"

using namespace std::literals;
using PlatformTest = ::libtransmission::test::SessionTest;

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

TEST_F(PlatformTest, defaultDownloadDirXdg)
{
    setenv("HOME", sandboxDir().c_str(), 1);
    setenv("XDG_CONFIG_HOME", LIBTRANSMISSION_TEST_ASSETS_DIR, 1);

    auto const expected = fmt::format("{:s}/UserDirsDownloads"sv, sandboxDir());
    auto const actual = tr_getDefaultDownloadDir();
    EXPECT_EQ(expected, actual);

    unsetenv("XDG_CONFIG_HOME");
    unsetenv("HOME");
}

#if !defined(_WIN32) && !defined(__HAIKU__)
TEST_F(PlatformTest, defaultDownloadDir)
{
    setenv("HOME", sandboxDir().c_str(), 1);

    auto const expected = fmt::format("{:s}/Downloads"sv, sandboxDir());
    auto const actual = tr_getDefaultDownloadDir();
    EXPECT_EQ(expected, actual);

    unsetenv("HOME");
}
#endif

TEST_F(PlatformTest, defaultConfigDirEnv)
{
    setenv("TRANSMISSION_HOME", sandboxDir().c_str(), 1);

    auto const expected = sandboxDir();
    auto const actual = tr_getDefaultConfigDir("appname");
    EXPECT_EQ(expected, actual);

    unsetenv("TRANSMISSION_HOME");
}

#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__HAIKU__)

TEST_F(PlatformTest, defaultConfigDirXdgConfig)
{
    setenv("XDG_CONFIG_HOME", sandboxDir().c_str(), 1);

    auto const expected = fmt::format("{:s}/appname", sandboxDir());
    auto const actual = tr_getDefaultConfigDir("appname");
    EXPECT_EQ(expected, actual);

    unsetenv("XDG_CONFIG_HOME");
}

TEST_F(PlatformTest, defaultConfigDirXdgConfigHome)
{
    unsetenv("TRANSMISSION_HOME");
    unsetenv("XDG_CONFIG_HOME");
    auto const home = tr_pathbuf{ sandboxDir(), "/home/user" };
    setenv("HOME", home, 1);

    auto const expected = fmt::format("{:s}/.config/appname", home.sv());
    auto const actual = tr_getDefaultConfigDir("appname");
    EXPECT_EQ(expected, actual);

    unsetenv("HOME");
}

#endif

TEST_F(PlatformTest, webClientDirEnvClutch)
{
    setenv("CLUTCH_HOME", sandboxDir().c_str(), 1);

    EXPECT_EQ(sandboxDir(), tr_getWebClientDir(session_));

    unsetenv("CLUTCH_HOME");
}

TEST_F(PlatformTest, webClientDirEnvTr)
{
    setenv("TRANSMISSION_WEB_HOME", sandboxDir().c_str(), 1);

    EXPECT_EQ(sandboxDir(), tr_getWebClientDir(session_));

    unsetenv("TRANSMISSION_WEB_HOME");
}

#if !defined(BUILD_MAC_CLIENT) && !defined(_WIN32)
TEST_F(PlatformTest, webClientDirXdgDataHome)
{
    setenv("XDG_DATA_HOME", sandboxDir().c_str(), 1);

    auto const expected = tr_pathbuf{ sandboxDir(), "/transmission/public_html"sv };
    auto const index_html = tr_pathbuf{ expected, "/index.html"sv };
    EXPECT_TRUE(tr_sys_dir_create(expected, TR_SYS_DIR_CREATE_PARENTS, 0777));
    EXPECT_TRUE(tr_saveFile(index_html, "<html></html>"sv));

    EXPECT_EQ(expected, tr_getWebClientDir(session_));

    unsetenv("XDG_DATA_HOME");
}
#endif
