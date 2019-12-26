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
#include "file.h"
#include "tr-assert.h"

#include "libtransmission-test.h"

/***
****
***/

char* sandbox;

static char const file_content[] = "mZJ0sMGOB9eHtni1DaBdjWYHINl6todyJi2zrUYrkhp8ALva48aP6XJybsPYKOxtO9n49EdqoMf7W5dBOYct7RKJNi4AP2yvmpU9TgQXXeffdNIE8M4wPD9Vp7H2AbwsJXhVz51sxjmC4mCwmAsE531INqtW14w7HYAkPNvccXtiYxa97bSM7lKINA9V0D8o4NknP3HKLTsfDTnnJ0AgxrSZv4R5XjonswreBWwW3NVaEfWVUhnJpMh0B0hJ0QiFX3uBEuka9DKt0GIWNfa0cBSJteUXNnslb6GLOgwXPFLoyfK3G0HfDQ6ofG9aiXcBARCKMPRa9DX5uVKiyYwVD3FuUkbBig7FW7n9e8XqZGpE3sXTM4hVG335L1Tx7eRH2b3KZBQsFEjn9dNzEF1seHRlB4x1uw2Iiebkmgn1UfLpLiUdDzP2QmBQnhWe2NP4eC5Dhd25hhlYJgStjSBcKLJOYDgCy1lmypVXULUZPCOm7hNg4M19qirBp8m5GTScwFvhEntMnxaIrLbWq7SQeKPaMJnlVdlSxOMJtzhNsEHaFRLAf90Zr6dNR9yCdydeNQ0qCNI17SXOXDSEYUGlInZioxilxI2V4Ewd6zhMGNjikMm9jj51lGDHyQnPq7W9oRS0kGWjUwFT8oASGjpWlexo6BMTlG4BDnGLKLYqniV4jCyRpF7UWqmCyMh3H8q7e6JqR0dIZc11OU9VCI9GIfKb0KroE9wnLii7CKLlVJZXtIrxILt13NjPd8if5HfLOyuQVp52jfdjVgTkPVPONBzRieEYYAQwRlmUdJkYoVuP5sxzwH7p6rl74Gl1ApKxFsYCj5dZdxkWe9M2ToUi1qp8ACOK2YtYyxDqG93eDfxQPCdNEL7dZvK8LaBG0h5r96gZ9GwMXM7VBirpkV3HXkxxjK53WDYGMvtbZ5NXi2NTqmXTbvqZHbyGPFXfNSbMUqPToC2INdF0oIQ3Fho22LvNXUWD73s61RPOgHB3%";

/***
****
***/

static uint64_t fill_buffer_from_fd(tr_sys_file_t fd, uint64_t bytes_remaining,
    char* buf, size_t buf_len)
{
    memset(buf, 0, buf_len);

    size_t buf_pos = 0;
    while (buf_pos < buf_len && bytes_remaining > 0)
    {
        uint64_t const chunk_size = MIN( buf_len-buf_pos, bytes_remaining );
        uint64_t bytes_read = 0;

        TR_ASSERT(tr_sys_file_read(fd, buf + buf_pos, chunk_size, &bytes_read,
            NULL));

        TR_ASSERT( buf_pos + bytes_read <= buf_len );
        TR_ASSERT( bytes_read <= bytes_remaining );
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
    TR_ASSERT(fd1 != TR_BAD_SYS_FILE);

    fd2 = tr_sys_file_open(fn2, TR_SYS_FILE_READ | TR_SYS_FILE_SEQUENTIAL, 0,
        NULL);
    TR_ASSERT(fd2 != TR_BAD_SYS_FILE);

    TR_ASSERT(tr_sys_file_get_info(fd1, &info1, NULL));
    TR_ASSERT(tr_sys_file_get_info(fd2, &info2, NULL));

    if(info1.size != info2.size)
    {
        result = false;
        goto out;
    }

    readbuf1 = tr_valloc(buflen);
    readbuf2 = tr_valloc(buflen);
    TR_ASSERT(readbuf1); TR_ASSERT(readbuf2);

    uint64_t bytes_left1 = info1.size;
    uint64_t bytes_left2 = info2.size;
    while(bytes_left1 || bytes_left2)
    {
        bytes_left1 = fill_buffer_from_fd(fd1, bytes_left1, readbuf1, buflen);
        bytes_left2 = fill_buffer_from_fd(fd2, bytes_left2, readbuf2, buflen);
        TR_ASSERT( bytes_left1 == bytes_left2 );

        if( memcmp(readbuf1, readbuf2, buflen) )
        {
            result = false;
            goto out;
        }
    }

    result = true;

out:
    tr_free(readbuf1);
    tr_free(readbuf2);
    TR_ASSERT(tr_sys_file_close(fd1, NULL));
    TR_ASSERT(tr_sys_file_close(fd2, NULL));

    return result;
}

static int test_copy_file(void)
{
    char const* filename1 = "orig-blob.txt";
    char const* filename2 = "copy-blob.txt";

    char* path1 = tr_buildPath(sandbox, filename1, NULL);
    char* path2 = tr_buildPath(sandbox, filename2, NULL);

    /* Create a file. */
    libtest_create_file_with_string_contents(path1, file_content);

    /* Copy it. */
    check_bool(tr_sys_path_copy(path1, path2, sizeof(file_content), NULL), ==,
        true);

    /* Verify the files are identical. */
    check_bool( files_are_identical(path1, path2), ==, true );

    /* Dispose of the files. */
    tr_sys_path_remove(path1, NULL);
    tr_sys_path_remove(path2, NULL);

    tr_free(path1);
    tr_free(path2);

    return 0;
}

/***
****
***/

int main(void)
{
    testFunc const tests[] =
    {
        test_copy_file
    };

    sandbox = libtest_sandbox_create();

    int const result = runTests(tests, NUM_TESTS(tests));

    libtest_sandbox_destroy(sandbox);

    return result;
}

