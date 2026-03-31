// This file Copyright (C) 2026 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <gtest/gtest.h>
#include <libtransmission/resume.h>
#include <libtransmission/torrent.h>
#include <libtransmission/transmission.h>

#include "test-fixtures.h"

using namespace tr::test;

using ResumeTest = SessionTest;

TEST_F(ResumeTest, doneScriptCalledPersistsAcrossResume)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);

    EXPECT_FALSE(tor->done_script_called());

    tor->set_done_script_called(true);
    EXPECT_TRUE(tor->done_script_called());

    auto resume_helper = tr_torrent::ResumeHelper{ *tor };
    tr_resume::save(tor, resume_helper);

    tor->set_done_script_called(false);
    EXPECT_FALSE(tor->done_script_called());

    auto* const ctor = tr_ctorNew(session_);
    auto const loaded = tr_resume::load(tor, resume_helper, tr_resume::DoneScriptCalled, *ctor);
    tr_ctorFree(ctor);

    EXPECT_NE(tr_resume::fields_t{ 0 }, (loaded & tr_resume::DoneScriptCalled));
    EXPECT_TRUE(tor->done_script_called());
}
