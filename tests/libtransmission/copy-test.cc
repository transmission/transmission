// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstring>
#include <vector>

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>

#include "test-fixtures.h"

namespace libtransmission::test
{

class CopyTest : public SandboxedTest
{
protected:
    void testImpl(char const* filename1, char const* filename2, size_t const file_length)
    {
        auto const path1 = tr_pathbuf{ sandboxDir(), '/', filename1 };

        /* Create a file. */
        auto contents = std::vector<char>{};
        contents.resize(file_length);
        tr_rand_buffer(std::data(contents), std::size(contents));
        createFileWithContents(path1, std::data(contents), std::size(contents));

        auto const path2 = tr_pathbuf{ sandboxDir(), '/', filename2 };

        tr_error* err = nullptr;
        /* Copy it. */
        EXPECT_TRUE(tr_sys_path_copy(path1, path2, &err));
        EXPECT_EQ(nullptr, err) << ' ' << *err;
        tr_error_clear(&err);

        EXPECT_TRUE(filesAreIdentical(path1, path2));

        /* Dispose of those files that we created. */
        tr_sys_path_remove(path1);
        tr_sys_path_remove(path2);
    }

private:
    static uint64_t fillBufferFromFd(tr_sys_file_t fd, uint64_t bytes_remaining, char* buf, size_t buf_len)
    {
        memset(buf, 0, buf_len);

        size_t buf_pos = 0;
        while (buf_pos < buf_len && bytes_remaining > 0)
        {
            uint64_t const chunk_size = std::min(uint64_t{ buf_len - buf_pos }, bytes_remaining);
            uint64_t bytes_read = 0;

            tr_sys_file_read(fd, buf + buf_pos, chunk_size, &bytes_read);

            EXPECT_LE(buf_pos + bytes_read, buf_len);
            EXPECT_LE(bytes_read, bytes_remaining);
            buf_pos += bytes_read;
            bytes_remaining -= bytes_read;
        }

        return bytes_remaining;
    }

    static bool filesAreIdentical(std::string_view filename1, std::string_view filename2)
    {
        auto contents1 = std::vector<char>{};
        auto contents2 = std::vector<char>{};
        return tr_loadFile(filename1, contents1) && tr_loadFile(filename2, contents2) && contents1 == contents2;
    }
};

TEST_F(CopyTest, copy)
{
    char const* filename1 = "orig-blob.txt";
    char const* filename2 = "copy-blob.txt";
    auto const random_file_length = 1024 * 1024 * 10;

    testImpl(filename1, filename2, random_file_length);
}

} // namespace libtransmission::test
