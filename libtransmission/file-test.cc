/*
 * This file Copyright (C) 2013-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "transmission.h"
#include "error.h"
#include "file.h"

#include "test-fixtures.h"

#if !defined(__OpenBSD__)
#define HAVE_UNIFIED_BUFFER_CACHE
#endif

#ifndef _WIN32
#define NATIVE_PATH_SEP "/"
#else
#define NATIVE_PATH_SEP "\\"
#endif

class FileTest: public SessionTest
{
protected:

  // static tr_session* session;

    char* create_test_dir(char const* name)
    {
        char* const test_dir = tr_buildPath(tr_sessionGetConfigDir(session_), name, nullptr);
        tr_sys_dir_create(test_dir, 0, 0777, nullptr);
        return test_dir;
    }

    bool create_symlink(char const* dst_path, char const* src_path, bool dst_is_dir)
    {
#ifndef _WIN32

        (void)dst_is_dir;

        return symlink(src_path, dst_path) != -1;

#else

        wchar_t* wide_src_path;
        wchar_t* wide_dst_path;
        bool ret = false;

        wide_src_path = tr_win32_utf8_to_native(src_path, -1);
        wide_dst_path = tr_win32_utf8_to_native(dst_path, -1);

        ret = CreateSymbolicLinkW(wide_dst_path, wide_src_path, dst_is_dir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);

        tr_free(wide_dst_path);
        tr_free(wide_src_path);

        return ret;

#endif
    }

    bool create_hardlink(char const* dst_path, char const* src_path)
    {
#ifndef _WIN32

        return link(src_path, dst_path) != -1;

#else

        wchar_t* wide_src_path = tr_win32_utf8_to_native(src_path, -1);
        wchar_t* wide_dst_path = tr_win32_utf8_to_native(dst_path, -1);

        bool ret = CreateHardLinkW(wide_dst_path, wide_src_path, nullptr);

        tr_free(wide_dst_path);
        tr_free(wide_src_path);

        return ret;

#endif
    }

    void clear_path_info(tr_sys_path_info* info)
    {
        info->type = (tr_sys_path_type_t)-1;
        info->size = (uint64_t)-1;
        info->last_modified_at = (time_t)-1;
    }

    bool path_contains_no_symlinks(char const* path)
    {
        char const* p = path;

        while (*p != '\0')
        {
            tr_sys_path_info info;
            char* pathPart;
            char const* slashPos = strchr(p, '/');

#ifdef _WIN32

            char const* backslashPos = strchr(p, '\\');

            if (slashPos == nullptr || (backslashPos != nullptr && backslashPos < slashPos))
            {
                slashPos = backslashPos;
            }

#endif

            if (slashPos == nullptr)
            {
                slashPos = p + strlen(p) - 1;
            }

            pathPart = tr_strndup(path, (size_t)(slashPos - path + 1));

            if (!tr_sys_path_get_info(pathPart, TR_SYS_PATH_NO_FOLLOW, &info, nullptr) ||
                (info.type != TR_SYS_PATH_IS_FILE && info.type != TR_SYS_PATH_IS_DIRECTORY))
            {
                tr_free(pathPart);
                return false;
            }

            tr_free(pathPart);

            p = slashPos + 1;
        }

        return true;
    }

    bool validate_permissions(char const* path, unsigned int permissions)
    {
#ifndef _WIN32

        struct stat sb;
        return stat(path, &sb) != -1 && (sb.st_mode & 0777) == permissions;

#else

        (void)path;
        (void)permissions;

        /* No UNIX permissions on Windows */
        return true;

#endif
    }

    struct xname_test_data
    {
        char const* input;
        char const* output;
    };

    void test_path_xname(struct xname_test_data const* data, size_t data_size, char* (*func)(char const*, tr_error**))
    {
        for (size_t i = 0; i < data_size; ++i)
        {
            tr_error* err = nullptr;
            char* name = func(data[i].input, &err);

            if (data[i].output != nullptr)
            {
                EXPECT_NE(nullptr, name);
                EXPECT_EQ(nullptr, err);
                EXPECT_STREQ(data[i].output, name);
                tr_free(name);
            }
            else
            {
                EXPECT_EQ(nullptr, name);
                EXPECT_NE(nullptr, err);
                tr_error_clear(&err);
            }
        }
    }

    static void test_dir_read_impl(char const* path, bool* have1, bool* have2)
    {
        *have1 = *have2 = false;

        tr_error* err = nullptr;
        auto dd = tr_sys_dir_open(path, &err);
        EXPECT_NE(TR_BAD_SYS_DIR, dd);
        EXPECT_EQ(nullptr, err);

        char const* name;
        while ((name = tr_sys_dir_read_name(dd, &err)) != nullptr)
        {
            EXPECT_EQ(nullptr, err);

            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            {
                continue;
            }

            if (strcmp(name, "a") == 0)
            {
                *have1 = true;
            }
            else if (strcmp(name, "b") == 0)
            {
                *have2 = true;
            }
            else
            {
                FAIL();
            }
        }

        EXPECT_EQ(nullptr, err);

        EXPECT_TRUE(tr_sys_dir_close(dd, &err));
        EXPECT_EQ(nullptr, err);
    }
};

TEST_F(FileTest, get_info)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_sys_path_info info;

    char* path1 = tr_buildPath(test_dir, "a", nullptr);
    char* path2 = tr_buildPath(test_dir, "b", nullptr);

    // Can't get info of non-existent file/directory
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_get_info(path1, 0, &info, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    auto t = time(nullptr);
    create_file_with_string_contents(path1, "test");

    // Good file info
    clear_path_info(&info);
    EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
    EXPECT_EQ(4, info.size);
    EXPECT_GT(info.last_modified_at, t-1);
    EXPECT_LE(info.last_modified_at, time(nullptr)+1);

    // Good file info (by handle)
    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, nullptr);
    clear_path_info(&info);
    EXPECT_TRUE(tr_sys_file_get_info(fd, &info, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
    EXPECT_EQ(4, info.size);
    EXPECT_GT(info.last_modified_at, t-1);
    EXPECT_LE(info.last_modified_at, time(nullptr)+1);
    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    // Good directory info
    t = time(nullptr);
    tr_sys_dir_create(path1, 0, 0777, nullptr);
    clear_path_info(&info);
    EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(TR_SYS_PATH_IS_DIRECTORY, info.type);
    EXPECT_NE(uint64_t(-1), info.size);
    EXPECT_GT(info.last_modified_at, t-1);
    EXPECT_LE(info.last_modified_at, time(nullptr)+1);
    tr_sys_path_remove(path1, nullptr);

    if (create_symlink(path1, path2, false))
    {
        // Can't get info of non-existent file/directory
        EXPECT_FALSE(tr_sys_path_get_info(path1, 0, &info, &err));
        EXPECT_NE(nullptr, err);
        tr_error_clear(&err);

        t = time(nullptr);
        create_file_with_string_contents(path2, "test");

        // Good file info
        clear_path_info(&info);
        EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
        EXPECT_EQ(4, info.size);
        EXPECT_GT(info.last_modified_at, t-1);
        EXPECT_LE(info.last_modified_at, time(nullptr)+1);

        // Good file info (by handle)
        fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, nullptr);
        clear_path_info(&info);
        EXPECT_TRUE(tr_sys_file_get_info(fd, &info, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_EQ(TR_SYS_PATH_IS_FILE, info.type);
        EXPECT_EQ(4, info.size);
        EXPECT_GT(info.last_modified_at, t-1);
        EXPECT_LE(info.last_modified_at, time(nullptr)+1);
        tr_sys_file_close(fd, nullptr);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);

        // Good directory info
        t = time(nullptr);
        tr_sys_dir_create(path2, 0, 0777, nullptr);
        EXPECT_TRUE(create_symlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        clear_path_info(&info);
        EXPECT_TRUE(tr_sys_path_get_info(path1, 0, &info, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_EQ(TR_SYS_PATH_IS_DIRECTORY, info.type);
        EXPECT_NE(uint64_t(-1), info.size);
        EXPECT_GT(info.last_modified_at, t-1);
        EXPECT_LE(info.last_modified_at, time(nullptr)+1);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, path_exists)
{
    char* const test_dir = create_test_dir(__FUNCTION__);

    auto* path1 = tr_buildPath(test_dir, "a", nullptr);
    auto* path2 = tr_buildPath(test_dir, "b", nullptr);

    // Non-existent file does not exist
    tr_error* err = nullptr;
    EXPECT_FALSE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err);

    // Create file and see that it exists
    create_file_with_string_contents(path1, "test");
    EXPECT_TRUE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path1, nullptr);

    // Create directory and see that it exists
    tr_sys_dir_create(path1, 0, 0777, nullptr);
    EXPECT_TRUE(tr_sys_path_exists(path1, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path1, nullptr);

    if (create_symlink(path1, path2, false))
    {
        // Non-existent file does not exist (via symlink)
        EXPECT_FALSE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err);

        // Create file and see that it exists (via symlink)
        create_file_with_string_contents(path2, "test");
        EXPECT_TRUE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);

        /* Create directory and see that it exists (via symlink) */
        tr_sys_dir_create(path2, 0, 0777, nullptr);
        EXPECT_TRUE(create_symlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        EXPECT_TRUE(tr_sys_path_exists(path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, path_is_relative)
{
    EXPECT_TRUE(tr_sys_path_is_relative(""));
    EXPECT_TRUE(tr_sys_path_is_relative("."));
    EXPECT_TRUE(tr_sys_path_is_relative(".."));
    EXPECT_TRUE(tr_sys_path_is_relative("x"));
    EXPECT_TRUE(tr_sys_path_is_relative("\\"));
    EXPECT_TRUE(tr_sys_path_is_relative(":"));

#ifdef _WIN32

    EXPECT_TRUE(tr_sys_path_is_relative("/"));
    EXPECT_TRUE(tr_sys_path_is_relative("\\x"));
    EXPECT_TRUE(tr_sys_path_is_relative("/x"));
    EXPECT_TRUE(tr_sys_path_is_relative("\\x\\y"));
    EXPECT_TRUE(tr_sys_path_is_relative("/x/y"));
    EXPECT_TRUE(tr_sys_path_is_relative("C:x"));
    EXPECT_TRUE(tr_sys_path_is_relative("C:x\\y"));
    EXPECT_TRUE(tr_sys_path_is_relative("C:x/y"));

    EXPECT_FALSE(tr_sys_path_is_relative("\\\\"));
    EXPECT_FALSE(tr_sys_path_is_relative("//"));
    EXPECT_FALSE(tr_sys_path_is_relative("\\\\x"));
    EXPECT_FALSE(tr_sys_path_is_relative("//x"));
    EXPECT_FALSE(tr_sys_path_is_relative("\\\\x\\y"));
    EXPECT_FALSE(tr_sys_path_is_relative("//x/y"));
    EXPECT_FALSE(tr_sys_path_is_relative("\\\\.\\x"));
    EXPECT_FALSE(tr_sys_path_is_relative("//./x"));

    EXPECT_FALSE(tr_sys_path_is_relative("a:"));
    EXPECT_FALSE(tr_sys_path_is_relative("a:\\"));
    EXPECT_FALSE(tr_sys_path_is_relative("a:/"));
    EXPECT_FALSE(tr_sys_path_is_relative("Z:"));
    EXPECT_FALSE(tr_sys_path_is_relative("Z:\\"));
    EXPECT_FALSE(tr_sys_path_is_relative("Z:/"));

#else /* _WIN32 */

    EXPECT_FALSE(tr_sys_path_is_relative("/"));
    EXPECT_FALSE(tr_sys_path_is_relative("/x"));
    EXPECT_FALSE(tr_sys_path_is_relative("/x/y"));
    EXPECT_FALSE(tr_sys_path_is_relative("//x"));

#endif /* _WIN32 */
}

TEST_F(FileTest, path_is_same)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = nullptr;
    char* path1;
    char* path2;
    char* path3;

    path1 = tr_buildPath(test_dir, "a", nullptr);
    path2 = tr_buildPath(test_dir, "b", nullptr);
    path3 = tr_buildPath(path2, "c", nullptr);

    /* Two non-existent files are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);

    /* Two same files are the same */
    create_file_with_string_contents(path1, "test");
    EXPECT_TRUE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err);

    /* Existent and non-existent files are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
    EXPECT_EQ(nullptr, err);

    /* Two separate files (even with same content) are not the same */
    create_file_with_string_contents(path2, "test");
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path1, nullptr);

    /* Two same directories are the same */
    tr_sys_dir_create(path1, 0, 0777, nullptr);
    EXPECT_TRUE(tr_sys_path_is_same(path1, path1, &err));
    EXPECT_EQ(nullptr, err);

    /* File and directory are not the same */
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path2, nullptr);

    /* Two separate directories are not the same */
    tr_sys_dir_create(path2, 0, 0777, nullptr);
    EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path1, nullptr);
    tr_sys_path_remove(path2, nullptr);

    if (create_symlink(path1, ".", true))
    {
        /* Directory and symlink pointing to it are the same */
        EXPECT_TRUE(tr_sys_path_is_same(path1, test_dir, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(tr_sys_path_is_same(test_dir, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Non-existent file and symlink are not the same */
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Symlinks pointing to different directories are not the same */
        create_symlink(path2, "..", true);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);

        /* Symlinks pointing to same directory are the same */
        create_symlink(path2, ".", true);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);

        /* Directory and symlink pointing to another directory are not the same */
        tr_sys_dir_create(path2, 0, 0777, nullptr);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Symlinks pointing to same directory are the same */
        create_symlink(path3, "..", true);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path1, nullptr);

        /* File and symlink pointing to directory are not the same */
        create_file_with_string_contents(path1, "test");
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path3, nullptr);

        /* File and symlink pointing to same file are the same */
        create_symlink(path3, path1, false);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Symlinks pointing to non-existent files are not the same */
        tr_sys_path_remove(path1, nullptr);
        create_symlink(path1, "missing", false);
        tr_sys_path_remove(path3, nullptr);
        create_symlink(path3, "missing", false);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path3, nullptr);

        /* Symlinks pointing to same non-existent file are not the same */
        create_symlink(path3, ".." NATIVE_PATH_SEP "missing", false);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        /* Non-existent file and symlink pointing to non-existent file are not the same */
        tr_sys_path_remove(path3, nullptr);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path3, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path3);
    path3 = tr_buildPath(test_dir, "c", nullptr);

    create_file_with_string_contents(path1, "test");

    if (create_hardlink(path2, path1))
    {
        /* File and hardlink to it are the same */
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);

        /* Two hardlinks to the same file are the same */
        create_hardlink(path3, path2);
        EXPECT_TRUE(tr_sys_path_is_same(path2, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path2, nullptr);

        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path3, nullptr);

        /* File and hardlink to another file are not the same */
        create_file_with_string_contents(path3, "test");
        create_hardlink(path2, path3);
        EXPECT_FALSE(tr_sys_path_is_same(path1, path2, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_is_same(path2, path1, &err));
        EXPECT_EQ(nullptr, err);

        tr_sys_path_remove(path3, nullptr);
        tr_sys_path_remove(path2, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

    if (create_symlink(path2, path1, false) && create_hardlink(path3, path1))
    {
        EXPECT_TRUE(tr_sys_path_is_same(path2, path3, &err));
        EXPECT_EQ(nullptr, err);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run combined symlink and hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path3, nullptr);
    tr_sys_path_remove(path2, nullptr);
    tr_sys_path_remove(path1, nullptr);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, path_resolve)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = nullptr;
    char* path1;
    char* path2;

    path1 = tr_buildPath(test_dir, "a", nullptr);
    path2 = tr_buildPath(test_dir, "b", nullptr);

    create_file_with_string_contents(path1, "test");

    if (create_symlink(path2, path1, false))
    {
        auto* tmp = tr_sys_path_resolve(path2, &err);
        EXPECT_NE(nullptr, tmp);
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(path_contains_no_symlinks(tmp));
        tr_free(tmp);

        tr_sys_path_remove(path2, nullptr);
        tr_sys_path_remove(path1, nullptr);

        tr_sys_dir_create(path1, 0, 0755, nullptr);
        EXPECT_TRUE(create_symlink(path2, path1, true)); /* Win32: directory and file symlinks differ :( */
        tmp = tr_sys_path_resolve(path2, &err);
        EXPECT_NE(nullptr, tmp);
        EXPECT_EQ(nullptr, err);
        EXPECT_TRUE(path_contains_no_symlinks(tmp));
        tr_free(tmp);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path2, nullptr);
    tr_sys_path_remove(path1, nullptr);

    tr_free(path2);
    tr_free(path1);

#ifdef _WIN32

    {
        char* tmp;

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\NonExistent", &err);
        EXPECT_EQ(nullptr, tmp);
        EXPECT_NE(nullptr, err);
        tr_error_clear(&err);

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\ADMIN$\\NonExistent", &err);
        EXPECT_EQ(nullptr, tmp);
        EXPECT_NE(nullptr, err);
        tr_error_clear(&err);

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\ADMIN$\\System32", &err);
        EXPECT_STEQ("\\\\127.0.0.1\\ADMIN$\\System32", tmp);
        EXPECT_EQ(nullptr, err);
        tr_free(tmp);
    }

#endif

    tr_free(test_dir);
}

TEST_F(FileTest, path_basename_dirname)
{
    struct xname_test_data const common_xname_tests[] =
    {
        { "/", "/" },
        { "", "." },
#ifdef _WIN32
        { "\\", "/" },
        /* Invalid paths */
        { "\\\\\\", nullptr },
        { "123:", nullptr },
        /* Reserved characters */
        { "<", nullptr },
        { ">", nullptr },
        { ":", nullptr },
        { "\"", nullptr },
        { "|", nullptr },
        { "?", nullptr },
        { "*", nullptr },
        { "a\\<", nullptr },
        { "a\\>", nullptr },
        { "a\\:", nullptr },
        { "a\\\"", nullptr },
        { "a\\|", nullptr },
        { "a\\?", nullptr },
        { "a\\*", nullptr },
        { "c:\\a\\b<c\\d", nullptr },
        { "c:\\a\\b>c\\d", nullptr },
        { "c:\\a\\b:c\\d", nullptr },
        { "c:\\a\\b\"c\\d", nullptr },
        { "c:\\a\\b|c\\d", nullptr },
        { "c:\\a\\b?c\\d", nullptr },
        { "c:\\a\\b*c\\d", nullptr }
#else
        { "////", "/" }
#endif
    };

    test_path_xname(common_xname_tests, TR_N_ELEMENTS(common_xname_tests), tr_sys_path_basename);
    test_path_xname(common_xname_tests, TR_N_ELEMENTS(common_xname_tests), tr_sys_path_dirname);

    struct xname_test_data const basename_tests[] =
    {
        { "a", "a" },
        { "aa", "aa" },
        { "/aa", "aa" },
        { "/a/b/c", "c" },
        { "/a/b/c/", "c" },
#ifdef _WIN32
        { "c:\\a\\b\\c", "c" },
        { "c:", "/" },
        { "c:/", "/" },
        { "c:\\", "/" },
        { "c:a/b", "b" },
        { "c:a", "a" },
        { "\\\\a\\b\\c", "c" },
        { "//a/b", "b" },
        { "//1.2.3.4/b", "b" },
        { "\\\\a", "a" },
        { "\\\\1.2.3.4", "1.2.3.4" },
        { "\\", "/" },
        { "\\a", "a" }
#endif
    };

    test_path_xname(basename_tests, TR_N_ELEMENTS(basename_tests), tr_sys_path_basename);

    struct xname_test_data const dirname_tests[] =
    {
        { "/a/b/c", "/a/b" },
        { "a/b/c", "a/b" },
        { "a/b/c/", "a/b" },
        { "a", "." },
        { "a/", "." },
#ifdef _WIN32
        { "C:\\a/b\\c", "C:\\a/b" },
        { "C:\\a/b\\c\\", "C:\\a/b" },
        { "C:\\a/b", "C:\\a" },
        { "C:/a", "C:" },
        { "C:", "C:" },
        { "C:/", "C:" },
        { "C:\\", "C:" },
        { "c:a/b", "c:a" },
        { "c:a", "c:." },
        { "c:.", "c:." },
        { "\\\\a\\b\\c", "\\\\a\\b" },
        { "\\\\a\\b\\c/", "\\\\a\\b" },
        { "//a/b", "//a" },
        { "//1.2.3.4/b", "//1.2.3.4" },
        { "\\\\a", "\\\\" },
        { "\\\\1.2.3.4", "\\\\" },
        { "\\\\", "\\\\" },
        { "a/b\\c", "a/b" }
#endif
    };

    test_path_xname(dirname_tests, TR_N_ELEMENTS(dirname_tests), tr_sys_path_dirname);

    /* TODO: is_same(dirname(x) + '/' + basename(x), x) */
}

TEST_F(FileTest, path_rename)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = nullptr;

    auto* path1 = tr_buildPath(test_dir, "a", nullptr);
    auto* path2 = tr_buildPath(test_dir, "b", nullptr);
    auto* path3 = tr_buildPath(path2, "c", nullptr);

    create_file_with_string_contents(path1, "test");

    /* Preconditions */
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));

    /* Forward rename works */
    EXPECT_TRUE(tr_sys_path_rename(path1, path2, &err));
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
    EXPECT_EQ(nullptr, err);

    /* Backward rename works */
    EXPECT_TRUE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));
    EXPECT_EQ(nullptr, err);

    /* Another backward rename [of non-existent file] does not work */
    EXPECT_FALSE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    /* Rename to file which couldn't be created does not work */
    EXPECT_FALSE(tr_sys_path_rename(path1, path3, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    /* Rename of non-existent file does not work */
    EXPECT_FALSE(tr_sys_path_rename(path3, path2, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    create_file_with_string_contents(path2, "test");

    /* Renaming file does overwrite existing file */
    EXPECT_TRUE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_dir_create(path2, 0, 0777, nullptr);

    /* Renaming file does not overwrite existing directory, and vice versa */
    EXPECT_FALSE(tr_sys_path_rename(path1, path2, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    EXPECT_FALSE(tr_sys_path_rename(path2, path1, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path2, nullptr);

    tr_free(path3);
    path3 = tr_buildPath(test_dir, "c", nullptr);

    if (create_symlink(path2, path1, false))
    {
        /* Preconditions */
        EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
        EXPECT_FALSE(tr_sys_path_exists(path3, nullptr));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, nullptr));

        /* Rename of symlink works, files stay the same */
        EXPECT_TRUE(tr_sys_path_rename(path2, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));
        EXPECT_TRUE(tr_sys_path_exists(path3, nullptr));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, nullptr));

        tr_sys_path_remove(path3, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    if (create_hardlink(path2, path1))
    {
        /* Preconditions */
        EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
        EXPECT_FALSE(tr_sys_path_exists(path3, nullptr));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path2, nullptr));

        /* Rename of hardlink works, files stay the same */
        EXPECT_TRUE(tr_sys_path_rename(path2, path3, &err));
        EXPECT_EQ(nullptr, err);
        EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));
        EXPECT_TRUE(tr_sys_path_exists(path3, nullptr));
        EXPECT_TRUE(tr_sys_path_is_same(path1, path3, nullptr));

        tr_sys_path_remove(path3, nullptr);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path1, nullptr);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, path_remove)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = nullptr;

    auto* path1 = tr_buildPath(test_dir, "a", nullptr);
    auto* path2 = tr_buildPath(test_dir, "b", nullptr);
    auto* path3 = tr_buildPath(path2, "c", nullptr);

    /* Can't remove non-existent file/directory */
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    EXPECT_FALSE(tr_sys_path_remove(path1, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    tr_error_clear(&err);

    /* Removing file works */
    create_file_with_string_contents(path1, "test");
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_path_remove(path1, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));

    /* Removing empty directory works */
    tr_sys_dir_create(path1, 0, 0777, nullptr);
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_path_remove(path1, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));

    /* Removing non-empty directory fails */
    tr_sys_dir_create(path2, 0, 0777, nullptr);
    create_file_with_string_contents(path3, "test");
    EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
    EXPECT_TRUE(tr_sys_path_exists(path3, nullptr));
    EXPECT_FALSE(tr_sys_path_remove(path2, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
    EXPECT_TRUE(tr_sys_path_exists(path3, nullptr));
    tr_error_clear(&err);

    tr_sys_path_remove(path3, nullptr);
    tr_sys_path_remove(path2, nullptr);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, path_native_separators)
{
    EXPECT_EQ(nullptr, tr_sys_path_native_separators(nullptr));

    char path1[] = "";
    char path2[] = "a";
    char path3[] = "/";
    char path4[] = "/a/b/c";
    char path5[] = "C:\\a/b\\c";

    struct
    {
        char* input;
        char const* output;
    }
    test_data[] =
    {
        { path1, "" },
        { path2, TR_IF_WIN32("a", "a") },
        { path3, TR_IF_WIN32("\\", "/") },
        { path4, TR_IF_WIN32("\\a\\b\\c", "/a/b/c") },
        { path5, TR_IF_WIN32("C:\\a\\b\\c", "C:\\a/b\\c") },
    };

    for (size_t i = 0; i < TR_N_ELEMENTS(test_data); ++i)
    {
        char* const output = tr_sys_path_native_separators(test_data[i].input);

        EXPECT_STREQ(test_data[i].output, output);
        EXPECT_STREQ(test_data[i].output, test_data[i].input);
        EXPECT_EQ(test_data[i].input, output);
    }
}

TEST_F(FileTest, file_open)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = nullptr;
    char* path1;
    tr_sys_file_t fd;
    uint64_t n;
    tr_sys_path_info info;

    path1 = tr_buildPath(test_dir, "a", nullptr);

    /* Can't open non-existent file */
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err) == TR_BAD_SYS_FILE);
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    tr_error_clear(&err);
    EXPECT_TRUE(tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err) == TR_BAD_SYS_FILE);
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path1, nullptr));
    tr_error_clear(&err);

    /* Can't open directory */
    tr_sys_dir_create(path1, 0, 0777, nullptr);
#ifdef _WIN32
    /* This works on *NIX */
    EXPECT_TRUE(tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err) == TR_BAD_SYS_FILE);
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
#endif
    EXPECT_TRUE(tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err) == TR_BAD_SYS_FILE);
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path1, nullptr);

    /* Can create non-existent file */
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0640, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_close(fd, nullptr);
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(validate_permissions(path1, 0640));

    /* Can open existing file */
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_close(fd, nullptr);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);
    create_file_with_string_contents(path1, "test");

    /* Can't create new file if it already exists */
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE_NEW, 0640, &err);
    EXPECT_EQ(TR_BAD_SYS_FILE, fd);
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, nullptr);
    EXPECT_EQ(4, info.size);

    /* Pointer is at the end of file */
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, nullptr);
    EXPECT_EQ(4, info.size);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_APPEND, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_write(fd, "s", 1, nullptr, nullptr); /* On *NIX, pointer is positioned on each write but not initially */
    tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, nullptr);
    EXPECT_EQ(5, n);
    tr_sys_file_close(fd, nullptr);

    /* File gets truncated */
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, nullptr);
    EXPECT_EQ(5, info.size);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0600, &err);
    EXPECT_NE(TR_BAD_SYS_FILE, fd);
    EXPECT_EQ(nullptr, err);
    tr_sys_file_get_info(fd, &info, nullptr);
    EXPECT_EQ(0, info.size);
    tr_sys_file_close(fd, nullptr);
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, nullptr);
    EXPECT_EQ(0, info.size);

    /* TODO: symlink and hardlink tests */

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, file_read_write_seek)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = nullptr;
    char* path1;
    tr_sys_file_t fd;
    uint64_t n;
    char buf[100];

    path1 = tr_buildPath(test_dir, "a", nullptr);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_write(fd, "test", 4, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_SET, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_read(fd, buf, sizeof(buf), &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_EQ(0, memcmp(buf, "test", 4));

    EXPECT_TRUE(tr_sys_file_seek(fd, -3, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_write(fd, "E", 1, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, -2, TR_SEEK_CUR, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_read(fd, buf, sizeof(buf), &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_EQ(0, memcmp("tEst", buf, 4));

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_END, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(4, n);

    EXPECT_TRUE(tr_sys_file_write(fd, " ok", 3, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(3, n);

    EXPECT_TRUE(tr_sys_file_seek(fd, 0, TR_SEEK_SET, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(0, n);

    EXPECT_TRUE(tr_sys_file_read(fd, buf, sizeof(buf), &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(7, n);

    EXPECT_EQ(0, memcmp("tEst ok", buf, 7));

    EXPECT_TRUE(tr_sys_file_write_at(fd, "-", 1, 4, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(1, n);

    EXPECT_TRUE(tr_sys_file_read_at(fd, buf, 5, 2, &n, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(5, n);

    EXPECT_EQ(0, memcmp("st-ok", buf, 5));

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, file_truncate)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = nullptr;
    tr_sys_path_info info;

    auto* path1 = tr_buildPath(test_dir, "a", nullptr);

    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 10, &err));
    EXPECT_EQ(nullptr, err);
    tr_sys_file_get_info(fd, &info, nullptr);
    EXPECT_EQ(10, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 20, &err));
    EXPECT_EQ(nullptr, err);
    tr_sys_file_get_info(fd, &info, nullptr);
    EXPECT_EQ(20, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 0, &err));
    EXPECT_EQ(nullptr, err);
    tr_sys_file_get_info(fd, &info, nullptr);
    EXPECT_EQ(0, info.size);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 50, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_get_info(path1, 0, &info, nullptr);
    EXPECT_EQ(50, info.size);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    EXPECT_TRUE(tr_sys_file_truncate(fd, 25, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_get_info(path1, 0, &info, nullptr);
    EXPECT_EQ(25, info.size);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, file_preallocate)
{
    char* const test_dir = create_test_dir(__FUNCTION__);

    auto* path1 = tr_buildPath(test_dir, "a", nullptr);

    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    tr_error* err = nullptr;
    auto prealloc_size = size_t { 50 };
    if (tr_sys_file_preallocate(fd, prealloc_size, 0, &err))
    {
        EXPECT_EQ(nullptr, err);
        tr_sys_path_info info;
        tr_sys_file_get_info(fd, &info, nullptr);
        EXPECT_EQ(prealloc_size, info.size);
    }
    else
    {
        EXPECT_NE(nullptr, err);
        fprintf(stderr, "WARNING: [%s] unable to preallocate file (full): %s (%d)\n", __FUNCTION__, err->message, err->code);
        tr_error_clear(&err);
    }

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, nullptr);

    prealloc_size = 500 * 1024 * 1024;
    if (tr_sys_file_preallocate(fd, prealloc_size, TR_SYS_FILE_PREALLOC_SPARSE, &err))
    {
        EXPECT_EQ(nullptr, err);
        tr_sys_path_info info;
        tr_sys_file_get_info(fd, &info, nullptr);
        EXPECT_EQ(prealloc_size, info.size);
    }
    else
    {
        EXPECT_NE(nullptr, err);
        fprintf(stderr, "WARNING: [%s] unable to preallocate file (sparse): %s (%d)\n", __FUNCTION__, err->message, err->code);
        tr_error_clear(&err);
    }

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, map)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    auto* path1 = tr_buildPath(test_dir, "a", nullptr);

    auto constexpr contents = std::string_view { "test" };
    create_file_with_string_contents(path1, std::data(contents));

    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE, 0600, nullptr);

    tr_error* err = nullptr;
    auto map_len = std::size(contents);
    auto* view = static_cast<char*>(tr_sys_file_map_for_reading(fd, 0, map_len, &err));
    EXPECT_NE(nullptr, view);
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(contents, std::string_view(view, map_len));

#ifdef HAVE_UNIFIED_BUFFER_CACHE

    auto constexpr contents2 = std::string_view { "more" };
    auto n_written = uint64_t { };
    tr_sys_file_write_at(fd, std::data(contents2), std::size(contents2), 0, &n_written, &err);
    EXPECT_EQ(map_len, std::size(contents2));
    EXPECT_EQ(map_len, n_written);
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(contents2, std::string_view(view, map_len));

#endif

    EXPECT_TRUE(tr_sys_file_unmap(view, map_len, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, file_utilities)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    auto* path1 = tr_buildPath(test_dir, "a", nullptr);
    auto constexpr contents = std::string_view { "a\nbc\r\ndef\nghij\r\n\n\nklmno\r" };
    create_file_with_string_contents(path1, std::data(contents));

    auto fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, nullptr);

    tr_error* err = nullptr;
    auto buffer = std::array<char, 16>{};
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("a", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("bc", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("def", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("ghij", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), 4, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("klmn", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("o", std::data(buffer));
    EXPECT_FALSE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("o", std::data(buffer)); // on EOF, buffer stays unchanged

    tr_sys_file_close(fd, nullptr);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0, nullptr);

    EXPECT_TRUE(tr_sys_file_write_line(fd, "p", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_line(fd, "qr", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_fmt(fd, "s%cu\r\n", &err, 't'));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_line(fd, "", &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_file_write_fmt(fd, "v%sy%d", &err, "wx", 2));
    EXPECT_EQ(nullptr, err);

    tr_sys_file_seek(fd, 0, TR_SEEK_SET, nullptr, nullptr);

    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("p", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("qr", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("stu", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("", std::data(buffer));
    EXPECT_TRUE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("vwxy2", std::data(buffer));
    EXPECT_FALSE(tr_sys_file_read_line(fd, std::data(buffer), std::size(buffer), &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_STREQ("vwxy2", std::data(buffer)); // on EOF, buffer stays unchanged

    tr_sys_file_close(fd, nullptr);

    tr_sys_path_remove(path1, nullptr);

    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, dir_create)
{
    char* const test_dir = create_test_dir(__FUNCTION__);

    auto* path1 = tr_buildPath(test_dir, "a", nullptr);
    auto* path2 = tr_buildPath(path1, "b", nullptr);

    // Can create directory which has parent
    tr_error* err = nullptr;
    EXPECT_TRUE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(validate_permissions(path1, 0700));

    tr_sys_path_remove(path1, nullptr);
    create_file_with_string_contents(path1, "test");

    // Can't create directory where file already exists
    EXPECT_FALSE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);
    EXPECT_FALSE(tr_sys_dir_create(path1, TR_SYS_DIR_CREATE_PARENTS, 0700, &err));
    EXPECT_NE(nullptr, err);
    tr_error_clear(&err);

    tr_sys_path_remove(path1, nullptr);

    // Can't create directory which has no parent
    EXPECT_FALSE(tr_sys_dir_create(path2, 0, 0700, &err));
    EXPECT_NE(nullptr, err);
    EXPECT_FALSE(tr_sys_path_exists(path2, nullptr));
    tr_error_clear(&err);

    // Can create directory with parent directories
    EXPECT_TRUE(tr_sys_dir_create(path2, TR_SYS_DIR_CREATE_PARENTS, 0751, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_path_exists(path1, nullptr));
    EXPECT_TRUE(tr_sys_path_exists(path2, nullptr));
    EXPECT_TRUE(validate_permissions(path1, 0751));
    EXPECT_TRUE(validate_permissions(path2, 0751));

    // Can create existing directory (no-op)
    EXPECT_TRUE(tr_sys_dir_create(path1, 0, 0700, &err));
    EXPECT_EQ(nullptr, err);
    EXPECT_TRUE(tr_sys_dir_create(path1, TR_SYS_DIR_CREATE_PARENTS, 0700, &err));
    EXPECT_EQ(nullptr, err);

    tr_sys_path_remove(path2, nullptr);
    tr_sys_path_remove(path1, nullptr);

    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
}

TEST_F(FileTest, dir_read)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    auto* path1 = tr_buildPath(test_dir, "a", nullptr);
    auto* path2 = tr_buildPath(test_dir, "b", nullptr);

    bool have1;
    bool have2;
    test_dir_read_impl(test_dir, &have1, &have2);
    EXPECT_FALSE(have1);
    EXPECT_FALSE(have2);

    create_file_with_string_contents(path1, "test");
    test_dir_read_impl(test_dir, &have1, &have2);
    EXPECT_TRUE(have1);
    EXPECT_FALSE(have2);

    create_file_with_string_contents(path2, "test");
    test_dir_read_impl(test_dir, &have1, &have2);
    EXPECT_TRUE(have1);
    EXPECT_TRUE(have2);

    tr_sys_path_remove(path1, nullptr);
    test_dir_read_impl(test_dir, &have1, &have2);
    EXPECT_FALSE(have1);
    EXPECT_TRUE(have2);

    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
}
