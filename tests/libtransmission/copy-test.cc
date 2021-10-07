/*
 * This file copyright Transmission authors and contributors
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>

#include "transmission.h"
#include "error.h"
#include "file.h"

#include "test-fixtures.h"

namespace libtransmission
{

namespace test
{

class CopyTest : public SandboxedTest
{
protected:
    void testImpl(char const* filename1, char const* filename2, size_t const file_length)
    {
        auto const path1 = tr_buildPath(sandboxDir().data(), filename1, nullptr);

        /* Create a file. */
        char* file_content = static_cast<char*>(tr_malloc(file_length));
        tr_rand_buffer(file_content, file_length);
        createFileWithContents(path1, file_content, file_length);
        tr_free(file_content);

        auto const path2 = tr_buildPath(sandboxDir().data(), filename2, nullptr);

        tr_error* err = nullptr;
        /* Copy it. */
        EXPECT_TRUE(tr_sys_path_copy(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        tr_error_clear(&err);

        EXPECT_TRUE(filesAreIdentical(path1, path2));

        /* Dispose of those files that we created. */
        tr_sys_path_remove(path1, nullptr);
        tr_free(path1);

        tr_sys_path_remove(path2, nullptr);
        tr_free(path2);
    }

private:
    uint64_t fillBufferFromFd(tr_sys_file_t fd, uint64_t bytes_remaining, char* buf, size_t buf_len)
    {
        memset(buf, 0, buf_len);

        size_t buf_pos = 0;
        while (buf_pos < buf_len && bytes_remaining > 0)
        {
            uint64_t const chunk_size = std::min(uint64_t{ buf_len - buf_pos }, bytes_remaining);
            uint64_t bytes_read = 0;

            tr_sys_file_read(fd, buf + buf_pos, chunk_size, &bytes_read, nullptr);

            EXPECT_LE(buf_pos + bytes_read, buf_len);
            EXPECT_LE(bytes_read, bytes_remaining);
            buf_pos += bytes_read;
            bytes_remaining -= bytes_read;
        }

        return bytes_remaining;
    }

    bool filesAreIdentical(char const* fn1, char const* fn2)
    {
        bool identical = true;

        tr_sys_file_t fd1 = tr_sys_file_open(fn1, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, nullptr);
        tr_sys_file_t fd2 = tr_sys_file_open(fn2, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0, nullptr);
        EXPECT_NE(fd1, TR_BAD_SYS_FILE);
        EXPECT_NE(fd2, TR_BAD_SYS_FILE);

        tr_sys_path_info info1;
        tr_sys_path_info info2;
        tr_sys_file_get_info(fd1, &info1, nullptr);
        tr_sys_file_get_info(fd2, &info2, nullptr);
        EXPECT_EQ(info1.size, info2.size);

        uint64_t bytes_left1 = info1.size;
        uint64_t bytes_left2 = info2.size;

        size_t const buflen = 2 * 1024 * 1024; /* 2 MiB buffer */
        char* readbuf1 = static_cast<char*>(tr_malloc(buflen));
        char* readbuf2 = static_cast<char*>(tr_malloc(buflen));

        while (bytes_left1 > 0 || bytes_left2 > 0)
        {
            bytes_left1 = fillBufferFromFd(fd1, bytes_left1, readbuf1, buflen);
            bytes_left2 = fillBufferFromFd(fd2, bytes_left2, readbuf2, buflen);

            if (bytes_left1 != bytes_left2)
            {
                identical = false;
                break;
            }

            if (memcmp(readbuf1, readbuf2, buflen) != 0)
            {
                identical = false;
                break;
            }
        }

        tr_free(readbuf1);
        tr_free(readbuf2);
        tr_sys_file_close(fd1, nullptr);
        tr_sys_file_close(fd2, nullptr);

        return identical;
    }
};

TEST_F(CopyTest, copy)
{
    char const* filename1 = "orig-blob.txt";
    char const* filename2 = "copy-blob.txt";
    auto const random_file_length = 1024 * 1024 * 10;

    testImpl(filename1, filename2, random_file_length);
}

} // namespace test

} // namespace libtransmission
