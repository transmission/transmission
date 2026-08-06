// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string>

#include <gtest/gtest.h>

#include <libtransmission/config-dir-lock.h>
#include <libtransmission/file.h>
#include <libtransmission/transmission.h>

#include "test-fixtures.h"

namespace tr::test
{

using ConfigDirLockTest = libtransmission::test::SandboxedTest;

TEST_F(ConfigDirLockTest, locksAConfigDirNoOneElseIsUsing)
{
    EXPECT_TRUE(tr_config_dir_lock{ sandboxDir() }.is_held());
}

TEST_F(ConfigDirLockTest, doesNotLockAConfigDirAnotherLockHolds)
{
    auto const held = tr_config_dir_lock{ sandboxDir() };
    ASSERT_TRUE(held.is_held());

    EXPECT_EQ(tr_config_dir_lock::Status::Contended, tr_config_dir_lock{ sandboxDir() }.status());

    // A failed attempt closes the descriptor it opened.
    // Ask a second time to confirm that closing it left the lock `held` owns in place.
    EXPECT_FALSE(tr_config_dir_lock{ sandboxDir() }.is_held());
}

TEST_F(ConfigDirLockTest, doesNotLockAConfigDirItCannotOpen)
{
    EXPECT_EQ(tr_config_dir_lock::Status::Unavailable, tr_config_dir_lock{ sandboxDir() + "/missing" }.status());
}

TEST_F(ConfigDirLockTest, releasesTheLockWhenDestroyed)
{
    {
        auto const held = tr_config_dir_lock{ sandboxDir() };
        ASSERT_TRUE(held.is_held());
    }

    EXPECT_TRUE(tr_config_dir_lock{ sandboxDir() }.is_held());
}

TEST_F(ConfigDirLockTest, reportsWhetherAnotherProcessHoldsTheDir)
{
    EXPECT_FALSE(tr_configDirIsContended(sandboxDir()));

    auto const held = tr_config_dir_lock{ sandboxDir() };
    ASSERT_TRUE(held.is_held());

    EXPECT_TRUE(tr_configDirIsContended(sandboxDir()));
}

// The probe takes the lock to ask and gives it straight back. Asking twice in a row
// therefore answers the same both times, and leaves the dir free for the next session.
TEST_F(ConfigDirLockTest, probingDoesNotKeepTheDir)
{
    EXPECT_FALSE(tr_configDirIsContended(sandboxDir()));
    EXPECT_FALSE(tr_configDirIsContended(sandboxDir()));

    EXPECT_TRUE(tr_config_dir_lock{ sandboxDir() }.is_held());
}

TEST_F(ConfigDirLockTest, oneLockedConfigDirDoesNotLockAnother)
{
    auto const other_dir = sandboxDir() + "/other";
    ASSERT_TRUE(tr_sys_dir_create(other_dir, TR_SYS_DIR_CREATE_PARENTS, 0700));

    auto const held = tr_config_dir_lock{ sandboxDir() };
    ASSERT_TRUE(held.is_held());

    EXPECT_TRUE(tr_config_dir_lock{ other_dir }.is_held());
}

} // namespace tr::test
