// This file Copyright (C) 2026 Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/session-settings.h>

#include <gtest/gtest.h>

TEST(SessionSettingsIncludeTest, publicHeaderIsUsable)
{
    static_assert(sizeof(tr::SessionSettingsSnapshot) > 0U);

    EXPECT_TRUE(tr::SessionSettingsSnapshot::has_key(TR_KEY_download_dir));
    EXPECT_FALSE(tr::SessionSettingsSnapshot::has_key(TR_KEY_session_id));
}