// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdlib> // setenv(), unsetenv()

#ifdef _WIN32
#include <windows.h>
#define setenv(key, value, unused) SetEnvironmentVariableA(key, value)
#define unsetenv(key) SetEnvironmentVariableA(key, nullptr)
#endif

#include "tr/base/env.h"

#include "test-fixtures.h"

using EnvTest = ::tr::test::TransmissionTest;

TEST_F(EnvTest, env)
{
    char const* test_key = "TR_TEST_ENV";

    unsetenv(test_key);

    EXPECT_FALSE(tr_env_key_exists(test_key));
    EXPECT_EQ(""sv, tr_env_get_string(test_key));
    EXPECT_EQ("a"sv, tr_env_get_string(test_key, "a"sv));

    setenv(test_key, "", 1);

    EXPECT_TRUE(tr_env_key_exists(test_key));
    EXPECT_EQ("", tr_env_get_string(test_key, ""));
    EXPECT_EQ("", tr_env_get_string(test_key, "b"));

    setenv(test_key, "135", 1);

    EXPECT_TRUE(tr_env_key_exists(test_key));
    EXPECT_EQ("135", tr_env_get_string(test_key, ""));
    EXPECT_EQ("135", tr_env_get_string(test_key, "c"));
}
