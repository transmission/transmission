// This file copyright Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>

#include "transmission.h"

#include "error.h"
#include "file.h"

#include "test-fixtures.h"

using OpenFilesTest = libtransmission::test::SandboxedTest;

TEST_F(OpenFilesTest, getCachedFailsIfNotCached)
{
}

TEST_F(OpenFilesTest, getCachedFailsIfWrongPermissions)
{
}

TEST_F(OpenFilesTest, getCacheSucceedsIfCached)
{
}

TEST_F(OpenFilesTest, getCachedReturnsTheSameFd)
{
}

TEST_F(OpenFilesTest, getOpensIfNotCached)
{
}

TEST_F(OpenFilesTest, opensInReadOnlyUnlessWritableIsRequested)
{
}

TEST_F(OpenFilesTest, createsMissingFileIfWriteRequested)
{
}

TEST_F(OpenFilesTest, closesLeastRecentlyUsedFile)
{
}

TEST_F(OpenFilesTest, closeFile)
{
}

TEST_F(OpenFilesTest, closeTorrent)
{
}
