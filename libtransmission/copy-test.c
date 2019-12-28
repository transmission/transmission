/*
 * This file is copyright Dr Robert Harvey Crowston, 2019.
 * <crowston@protonmail.com>
 *
 * This file may be used and distributed under the GNU GPL versions 2 or 3 or
 * any future version of the GPL.
 *
 */

#include <string.h> /* memcmp() */

#include "transmission.h"
#include "crypto-utils.h"
#include "file.h"
#include "tr-assert.h"

#include "libtransmission-test.h"

/***
****
***/

#define RANDOM_FILE_LENGTH  (1024*1024*128) /* 128 MiB */

char* sandbox_path;
char* reference_file;

/***
****
***/

static uint64_t fill_buffer_from_fd(tr_sys_file_t fd, uint64_t bytes_remaining, char* buf, size_t buf_len)
{
    memset(buf, 0, buf_len);

    size_t buf_pos = 0;
    while (buf_pos < buf_len && bytes_remaining > 0)
    {
        uint64_t const chunk_size = MIN(buf_len - buf_pos, bytes_remaining);
        uint64_t bytes_read = 0;

        tr_sys_file_read(fd, buf + buf_pos, chunk_size, &bytes_read, NULL);

        TR_ASSERT(buf_pos + bytes_read <= buf_len);
        TR_ASSERT(bytes_read <= bytes_remaining);
        buf_pos += bytes_read;
        bytes_remaining -= bytes_read;
    }

    return bytes_remaining;
}

static bool files_are_identical(char const* fn1, char const* fn2)
{
    tr_sys_file_t fd1, fd2;
    tr_sys_path_info info1, info2;
    char* readbuf1 = NULL;
    char* readbuf2 = NULL;
    bool result;
    size_t const buflen = 2 * 1024 * 1024; /* 2 MiB buffer */

    fd1 = tr_sys_file_open(fn1, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0,
        NULL);
    check_bool(fd1 != TR_BAD_SYS_FILE, ==, true);

    fd2 = tr_sys_file_open(fn2, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0,
        NULL);
    check_bool(fd2 != TR_BAD_SYS_FILE, ==, true);

    tr_sys_file_get_info(fd1, &info1, NULL);
    tr_sys_file_get_info(fd2, &info2, NULL);

    if (info1.size != info2.size)
    {
        result = false;
        goto out;
    }

    readbuf1 = tr_valloc(buflen);
    readbuf2 = tr_valloc(buflen);
    TR_ASSERT(readbuf1);
    TR_ASSERT(readbuf2);

    uint64_t bytes_left1 = info1.size;
    uint64_t bytes_left2 = info2.size;
    while (bytes_left1 || bytes_left2)
    {
        bytes_left1 = fill_buffer_from_fd(fd1, bytes_left1, readbuf1, buflen);
        bytes_left2 = fill_buffer_from_fd(fd2, bytes_left2, readbuf2, buflen);
        TR_ASSERT(bytes_left1 == bytes_left2);

        if (memcmp(readbuf1, readbuf2, buflen))
        {
            result = false;
            goto out;
        }
    }

    result = true;

out:
    tr_free(readbuf1);
    tr_free(readbuf2);
    tr_sys_file_close(fd1, NULL);
    tr_sys_file_close(fd2, NULL);

    return result;
}

static void get_random_string(char* buffer, size_t length)
{
    TR_ASSERT(length > 1);

    /* Obtain a stream of random bytes. */
    tr_rand_buffer(buffer, length - 1);

    /* Add a terminating null. */
    buffer[length - 1] = '\0';

    /* Replace embedded nulls, except the terminating one. */
    char* end = buffer + length - 1;
    while (end > buffer)
    {
        buffer = memchr(buffer, '\0', end - buffer);
        if (buffer == NULL)
        {
            break;
        }
        buffer[0] = '\1'; /* Biases the samples towards 0x1 but whiteness is not important for this application. */
        ++buffer;
    }
}

static int test_copy_file(void)
{
    char const* filename1 = "orig-blob.txt";
    char const* filename2 = "copy-blob.txt";

    char* path1, * path2;
    if (!reference_file)
    {
        path1 = tr_buildPath(sandbox_path, filename1, NULL);

        /* Create a file. */
        char* file_content = tr_valloc(RANDOM_FILE_LENGTH);
        get_random_string(file_content, RANDOM_FILE_LENGTH);
        libtest_create_file_with_string_contents(path1, file_content);
        tr_free(file_content);
    }
    else
    {
        /* Use the existing file. */
        path1 = reference_file;
    }

    path2 = tr_buildPath(sandbox_path, filename2, NULL);

    /* Copy it. */
    tr_sys_path_info info1;
    check_bool(tr_sys_path_get_info(path1, 0, &info1, NULL), ==, true);
    check_bool(tr_sys_path_copy(path1, path2, info1.size, NULL), ==, true);

    /* Verify the files are identical. */
    check_bool(files_are_identical(path1, path2), ==, true);

    /* Dispose of those files that we created. */
    if (!reference_file)
    {
        tr_sys_path_remove(path1, NULL);
        tr_free(path1);
    }

    tr_sys_path_remove(path2, NULL);
    tr_free(path2);

    return 0;
}

/***
****
***/

int main(int argc, char* argv[])
{
    if (argc == 2)
    {
        reference_file = argv[1];
    }
    else
    {
        reference_file = NULL;
    }

    testFunc const tests[] =
    {
        test_copy_file
    };

    sandbox_path = libtest_sandbox_create();

    int const result = runTests(tests, NUM_TESTS(tests));

    libtest_sandbox_destroy(sandbox_path);

    return result;
}
