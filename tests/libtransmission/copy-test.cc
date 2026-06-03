// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>

#include "test-fixtures.h"

namespace tr::test
{

class CopyTest : public SandboxedTest
{
protected:
    void testImpl(std::filesystem::path const& filename1, std::filesystem::path const& filename2, size_t const file_length)
        const
    {
        auto const path1 = tr_u8path(sandboxDir()) / filename1;

        /* Create a file. */
        auto contents = std::vector<char>{};
        contents.resize(file_length);
        tr_rand_buffer(std::data(contents), std::size(contents));
        createFileWithContents(path1.string(), std::data(contents), std::size(contents));

        auto const path2 = tr_u8path(sandboxDir()) / filename2;

        /* Copy it. */
        auto error = tr_error{};
        EXPECT_TRUE(tr_sys_path_copy(path1, path2, &error));
        EXPECT_FALSE(error) << error;

        EXPECT_TRUE(filesAreIdentical(path1, path2));

        /* Dispose of those files that we created. */
        tr_sys_path_remove(path1.string());
        tr_sys_path_remove(path2.string());
    }

private:
    static bool filesAreIdentical(std::filesystem::path const& filename1, std::filesystem::path const& filename2)
    {
        auto contents1 = std::vector<char>{};
        auto contents2 = std::vector<char>{};
        return tr_file_read(filename1.string(), contents1) && tr_file_read(filename2.string(), contents2) &&
            contents1 == contents2;
    }
};

TEST_F(CopyTest, copy)
{
    static constexpr auto Filename1 = u8"orig-blob.txt"sv;
    static constexpr auto Filename2 = u8"copy-blob.txt"sv;
    static constexpr auto RandomFileLength = 1024 * 1024 * 10;

    testImpl(Filename1, Filename2, RandomFileLength);
}

} // namespace tr::test
