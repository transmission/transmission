// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "torrent-files.h"
#include "file.h"

#include "test-fixtures.h"

using namespace std::literals;
using RemoveTest = libtransmission::test::SandboxedTest;

TEST_F(RemoveTest, RemovesSingleFile)
{
}

TEST_F(RemoveTest, RemovesSubtree)
{
}

TEST_F(RemoveTest, RemovesSubtreeIfPossible)
{
}

TEST_F(RemoveTest, RemovesFilesIfUnableToRemoveSubtree)
{
}

TEST_F(RemoveTest, RemovesLeftoverJunk)
{
}

TEST_F(RemoveTest, CleansUpTmpdirWhenDone)
{
}

TEST_F(RemoveTest, DoesNotRemoveOtherFilesInSubtree)
{
}

TEST_F(RemoveTest, DoesNotRemoveSiblingFiles)
{
}
