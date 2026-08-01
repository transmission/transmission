// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <libtransmission/config-dir-lock.h>
#include <libtransmission/file.h>

#include "test-fixtures.h"

namespace tr::test
{

using ConfigDirLockTest = SandboxedTest;

TEST_F(ConfigDirLockTest, locksAConfigDirNoOneElseIsUsing)
{
    EXPECT_TRUE(tr_config_dir_lock::create(sandboxDir()).is_held());
}

TEST_F(ConfigDirLockTest, refusesAConfigDirAnotherLockHolds)
{
    auto const held = tr_config_dir_lock::create(sandboxDir());
    ASSERT_TRUE(held.is_held());

    EXPECT_FALSE(tr_config_dir_lock::create(sandboxDir()).is_held());

    // A failed attempt closes the descriptor it opened. Ask a second time to
    // confirm that closing it left the lock `held` owns in place.
    EXPECT_FALSE(tr_config_dir_lock::create(sandboxDir()).is_held());
}

TEST_F(ConfigDirLockTest, releasesTheLockWhenDestroyed)
{
    {
        auto const held = tr_config_dir_lock::create(sandboxDir());
        ASSERT_TRUE(held.is_held());
    }

    EXPECT_TRUE(tr_config_dir_lock::create(sandboxDir()).is_held());
}

TEST_F(ConfigDirLockTest, movingKeepsTheLockHeld)
{
    auto held = tr_config_dir_lock{};

    {
        auto moved_from = tr_config_dir_lock::create(sandboxDir());
        ASSERT_TRUE(moved_from.is_held());
        held = std::move(moved_from);
    }

    EXPECT_TRUE(held.is_held());

    // Destroying the moved-from lock must not have released the config dir.
    EXPECT_FALSE(tr_config_dir_lock::create(sandboxDir()).is_held());
}

TEST_F(ConfigDirLockTest, oneLockedConfigDirDoesNotLockAnother)
{
    auto const other_dir = sandboxDir() + "/other";
    ASSERT_TRUE(tr_sys_dir_create(other_dir, TR_SYS_DIR_CREATE_PARENTS, 0700));

    auto const held = tr_config_dir_lock::create(sandboxDir());
    ASSERT_TRUE(held.is_held());

    EXPECT_TRUE(tr_config_dir_lock::create(other_dir).is_held());
}

TEST_F(ConfigDirLockTest, failsWhenTheConfigDirIsNotThere)
{
    auto const missing_dir = sandboxDir() + "/missing";

    EXPECT_FALSE(tr_config_dir_lock::create(missing_dir).is_held());
}

} // namespace tr::test
