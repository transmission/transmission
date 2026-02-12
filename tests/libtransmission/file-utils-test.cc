// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "lib/base/error.h"

#include "libtransmission/file-utils.h"
#include "libtransmission/file.h"
#include "libtransmission/tr-strbuf.h"

#include "test-fixtures.h"

using UtilsTest = ::tr::test::TransmissionTest;
using namespace std::literals;

TEST_F(UtilsTest, saveFile)
{
    auto filename = tr_pathbuf{};

    // save a file to GoogleTest's temp dir
    auto const sandbox = tr::test::Sandbox::createSandbox(::testing::TempDir(), "transmission-test-XXXXXX");
    filename.assign(sandbox, "filename.txt"sv);
    auto contents = "these are the contents"sv;
    auto error = tr_error{};
    EXPECT_TRUE(tr_file_save(filename.sv(), contents, &error));
    EXPECT_FALSE(error) << error;

    // now read the file back in and confirm the contents are the same
    auto buf = std::vector<char>{};
    EXPECT_TRUE(tr_file_read(filename.sv(), buf, &error));
    EXPECT_FALSE(error) << error;
    auto sv = std::string_view{ std::data(buf), std::size(buf) };
    EXPECT_EQ(contents, sv);

    // remove the tempfile
    EXPECT_TRUE(tr_sys_path_remove(filename, &error));
    EXPECT_FALSE(error) << error;

    // try saving a file to a path that doesn't exist
    filename = "/this/path/does/not/exist/foo.txt";
    EXPECT_FALSE(tr_file_save(filename.sv(), contents, &error));
    ASSERT_TRUE(error);
    EXPECT_NE(0, error.code());
}
