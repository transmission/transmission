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

#include "libtransmission-test.h"

#ifndef _WIN32
#define NATIVE_PATH_SEP "/"
#else
#define NATIVE_PATH_SEP "\\"
#endif

#if !defined(__OpenBSD__)
#define HAVE_UNIFIED_BUFFER_CACHE
#endif

static tr_session* session;

static char* create_test_dir(char const* name)
{
    char* const test_dir = tr_buildPath(tr_sessionGetConfigDir(session), name, NULL);
    tr_sys_dir_create(test_dir, 0, 0777, NULL);
    return test_dir;
}

static bool create_symlink(char const* dst_path, char const* src_path, bool dst_is_dir)
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

static bool create_hardlink(char const* dst_path, char const* src_path)
{
#ifndef _WIN32

    return link(src_path, dst_path) != -1;

#else

    wchar_t* wide_src_path = tr_win32_utf8_to_native(src_path, -1);
    wchar_t* wide_dst_path = tr_win32_utf8_to_native(dst_path, -1);

    bool ret = CreateHardLinkW(wide_dst_path, wide_src_path, NULL);

    tr_free(wide_dst_path);
    tr_free(wide_src_path);

    return ret;

#endif
}

static void clear_path_info(tr_sys_path_info* info)
{
    info->type = (tr_sys_path_type_t)-1;
    info->size = (uint64_t)-1;
    info->last_modified_at = (time_t)-1;
}

static bool path_contains_no_symlinks(char const* path)
{
    char const* p = path;

    while (*p != '\0')
    {
        tr_sys_path_info info;
        char* pathPart;
        char const* slashPos = strchr(p, '/');

#ifdef _WIN32

        char const* backslashPos = strchr(p, '\\');

        if (slashPos == NULL || (backslashPos != NULL && backslashPos < slashPos))
        {
            slashPos = backslashPos;
        }

#endif

        if (slashPos == NULL)
        {
            slashPos = p + strlen(p) - 1;
        }

        pathPart = tr_strndup(path, (size_t)(slashPos - path + 1));

        if (!tr_sys_path_get_info(pathPart, TR_SYS_PATH_NO_FOLLOW, &info, NULL) ||
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

static bool validate_permissions(char const* path, unsigned int permissions)
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

static int test_get_info(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_sys_path_info info;
    tr_sys_file_t fd;
    tr_error* err = NULL;
    char* path1;
    char* path2;
    time_t t;

    path1 = tr_buildPath(test_dir, "a", NULL);
    path2 = tr_buildPath(test_dir, "b", NULL);

    /* Can't get info of non-existent file/directory */
    check(!tr_sys_path_get_info(path1, 0, &info, &err));
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);

    t = time(NULL);
    libtest_create_file_with_string_contents(path1, "test");

    /* Good file info */
    clear_path_info(&info);
    check(tr_sys_path_get_info(path1, 0, &info, &err));
    check_ptr(err, ==, NULL);
    check_int(info.type, ==, TR_SYS_PATH_IS_FILE);
    check_uint(info.size, ==, 4);
    check_int(info.last_modified_at, >=, t - 1);
    check_int(info.last_modified_at, <=, time(NULL) + 1);

    /* Good file info (by handle) */
    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, NULL);
    clear_path_info(&info);
    check(tr_sys_file_get_info(fd, &info, &err));
    check_ptr(err, ==, NULL);
    check_int(info.type, ==, TR_SYS_PATH_IS_FILE);
    check_uint(info.size, ==, 4);
    check_int(info.last_modified_at, >=, t - 1);
    check_int(info.last_modified_at, <=, time(NULL) + 1);
    tr_sys_file_close(fd, NULL);

    tr_sys_path_remove(path1, NULL);

    /* Good directory info */
    t = time(NULL);
    tr_sys_dir_create(path1, 0, 0777, NULL);
    clear_path_info(&info);
    check(tr_sys_path_get_info(path1, 0, &info, &err));
    check_ptr(err, ==, NULL);
    check_int(info.type, ==, TR_SYS_PATH_IS_DIRECTORY);
    check_uint(info.size, !=, (uint64_t)-1);
    check_int(info.last_modified_at, >=, t - 1);
    check_int(info.last_modified_at, <=, time(NULL) + 1);
    tr_sys_path_remove(path1, NULL);

    if (create_symlink(path1, path2, false))
    {
        /* Can't get info of non-existent file/directory */
        check(!tr_sys_path_get_info(path1, 0, &info, &err));
        check_ptr(err, !=, NULL);
        tr_error_clear(&err);

        t = time(NULL);
        libtest_create_file_with_string_contents(path2, "test");

        /* Good file info */
        clear_path_info(&info);
        check(tr_sys_path_get_info(path1, 0, &info, &err));
        check_ptr(err, ==, NULL);
        check_int(info.type, ==, TR_SYS_PATH_IS_FILE);
        check_uint(info.size, ==, 4);
        check_int(info.last_modified_at, >=, t - 1);
        check_int(info.last_modified_at, <=, time(NULL) + 1);

        /* Good file info (by handle) */
        fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, NULL);
        clear_path_info(&info);
        check(tr_sys_file_get_info(fd, &info, &err));
        check_ptr(err, ==, NULL);
        check_int(info.type, ==, TR_SYS_PATH_IS_FILE);
        check_uint(info.size, ==, 4);
        check_int(info.last_modified_at, >=, t - 1);
        check_int(info.last_modified_at, <=, time(NULL) + 1);
        tr_sys_file_close(fd, NULL);

        tr_sys_path_remove(path2, NULL);
        tr_sys_path_remove(path1, NULL);

        /* Good directory info */
        t = time(NULL);
        tr_sys_dir_create(path2, 0, 0777, NULL);
        check(create_symlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        clear_path_info(&info);
        check(tr_sys_path_get_info(path1, 0, &info, &err));
        check_ptr(err, ==, NULL);
        check_int(info.type, ==, TR_SYS_PATH_IS_DIRECTORY);
        check_uint(info.size, !=, (uint64_t)-1);
        check_int(info.last_modified_at, >=, t - 1);
        check_int(info.last_modified_at, <=, time(NULL) + 1);

        tr_sys_path_remove(path2, NULL);
        tr_sys_path_remove(path1, NULL);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_path_exists(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    char* path2;

    path1 = tr_buildPath(test_dir, "a", NULL);
    path2 = tr_buildPath(test_dir, "b", NULL);

    /* Non-existent file does not exist */
    check(!tr_sys_path_exists(path1, &err));
    check_ptr(err, ==, NULL);

    /* Create file and see that it exists */
    libtest_create_file_with_string_contents(path1, "test");
    check(tr_sys_path_exists(path1, &err));
    check_ptr(err, ==, NULL);

    tr_sys_path_remove(path1, NULL);

    /* Create directory and see that it exists */
    tr_sys_dir_create(path1, 0, 0777, NULL);
    check(tr_sys_path_exists(path1, &err));
    check_ptr(err, ==, NULL);

    tr_sys_path_remove(path1, NULL);

    if (create_symlink(path1, path2, false))
    {
        /* Non-existent file does not exist (via symlink) */
        check(!tr_sys_path_exists(path1, &err));
        check_ptr(err, ==, NULL);

        /* Create file and see that it exists (via symlink) */
        libtest_create_file_with_string_contents(path2, "test");
        check(tr_sys_path_exists(path1, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path2, NULL);
        tr_sys_path_remove(path1, NULL);

        /* Create directory and see that it exists (via symlink) */
        tr_sys_dir_create(path2, 0, 0777, NULL);
        check(create_symlink(path1, path2, true)); /* Win32: directory and file symlinks differ :( */
        check(tr_sys_path_exists(path1, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path2, NULL);
        tr_sys_path_remove(path1, NULL);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_path_is_relative(void)
{
    check(tr_sys_path_is_relative(""));
    check(tr_sys_path_is_relative("."));
    check(tr_sys_path_is_relative(".."));
    check(tr_sys_path_is_relative("x"));
    check(tr_sys_path_is_relative("\\"));
    check(tr_sys_path_is_relative(":"));

#ifdef _WIN32

    check(tr_sys_path_is_relative("/"));
    check(tr_sys_path_is_relative("\\x"));
    check(tr_sys_path_is_relative("/x"));
    check(tr_sys_path_is_relative("\\x\\y"));
    check(tr_sys_path_is_relative("/x/y"));
    check(tr_sys_path_is_relative("C:x"));
    check(tr_sys_path_is_relative("C:x\\y"));
    check(tr_sys_path_is_relative("C:x/y"));

    check(!tr_sys_path_is_relative("\\\\"));
    check(!tr_sys_path_is_relative("//"));
    check(!tr_sys_path_is_relative("\\\\x"));
    check(!tr_sys_path_is_relative("//x"));
    check(!tr_sys_path_is_relative("\\\\x\\y"));
    check(!tr_sys_path_is_relative("//x/y"));
    check(!tr_sys_path_is_relative("\\\\.\\x"));
    check(!tr_sys_path_is_relative("//./x"));

    check(!tr_sys_path_is_relative("a:"));
    check(!tr_sys_path_is_relative("a:\\"));
    check(!tr_sys_path_is_relative("a:/"));
    check(!tr_sys_path_is_relative("Z:"));
    check(!tr_sys_path_is_relative("Z:\\"));
    check(!tr_sys_path_is_relative("Z:/"));

#else /* _WIN32 */

    check(!tr_sys_path_is_relative("/"));
    check(!tr_sys_path_is_relative("/x"));
    check(!tr_sys_path_is_relative("/x/y"));
    check(!tr_sys_path_is_relative("//x"));

#endif /* _WIN32 */

    return 0;
}

static int test_path_is_same(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    char* path2;
    char* path3;

    path1 = tr_buildPath(test_dir, "a", NULL);
    path2 = tr_buildPath(test_dir, "b", NULL);
    path3 = tr_buildPath(path2, "c", NULL);

    /* Two non-existent files are not the same */
    check(!tr_sys_path_is_same(path1, path1, &err));
    check_ptr(err, ==, NULL);
    check(!tr_sys_path_is_same(path1, path2, &err));
    check_ptr(err, ==, NULL);

    /* Two same files are the same */
    libtest_create_file_with_string_contents(path1, "test");
    check(tr_sys_path_is_same(path1, path1, &err));
    check_ptr(err, ==, NULL);

    /* Existent and non-existent files are not the same */
    check(!tr_sys_path_is_same(path1, path2, &err));
    check_ptr(err, ==, NULL);
    check(!tr_sys_path_is_same(path2, path1, &err));
    check_ptr(err, ==, NULL);

    /* Two separate files (even with same content) are not the same */
    libtest_create_file_with_string_contents(path2, "test");
    check(!tr_sys_path_is_same(path1, path2, &err));
    check_ptr(err, ==, NULL);

    tr_sys_path_remove(path1, NULL);

    /* Two same directories are the same */
    tr_sys_dir_create(path1, 0, 0777, NULL);
    check(tr_sys_path_is_same(path1, path1, &err));
    check_ptr(err, ==, NULL);

    /* File and directory are not the same */
    check(!tr_sys_path_is_same(path1, path2, &err));
    check_ptr(err, ==, NULL);
    check(!tr_sys_path_is_same(path2, path1, &err));
    check_ptr(err, ==, NULL);

    tr_sys_path_remove(path2, NULL);

    /* Two separate directories are not the same */
    tr_sys_dir_create(path2, 0, 0777, NULL);
    check(!tr_sys_path_is_same(path1, path2, &err));
    check_ptr(err, ==, NULL);

    tr_sys_path_remove(path1, NULL);
    tr_sys_path_remove(path2, NULL);

    if (create_symlink(path1, ".", true))
    {
        /* Directory and symlink pointing to it are the same */
        check(tr_sys_path_is_same(path1, test_dir, &err));
        check_ptr(err, ==, NULL);
        check(tr_sys_path_is_same(test_dir, path1, &err));
        check_ptr(err, ==, NULL);

        /* Non-existent file and symlink are not the same */
        check(!tr_sys_path_is_same(path1, path2, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_is_same(path2, path1, &err));
        check_ptr(err, ==, NULL);

        /* Symlinks pointing to different directories are not the same */
        create_symlink(path2, "..", true);
        check(!tr_sys_path_is_same(path1, path2, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_is_same(path2, path1, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path2, NULL);

        /* Symlinks pointing to same directory are the same */
        create_symlink(path2, ".", true);
        check(tr_sys_path_is_same(path1, path2, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path2, NULL);

        /* Directory and symlink pointing to another directory are not the same */
        tr_sys_dir_create(path2, 0, 0777, NULL);
        check(!tr_sys_path_is_same(path1, path2, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_is_same(path2, path1, &err));
        check_ptr(err, ==, NULL);

        /* Symlinks pointing to same directory are the same */
        create_symlink(path3, "..", true);
        check(tr_sys_path_is_same(path1, path3, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path1, NULL);

        /* File and symlink pointing to directory are not the same */
        libtest_create_file_with_string_contents(path1, "test");
        check(!tr_sys_path_is_same(path1, path3, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_is_same(path3, path1, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path3, NULL);

        /* File and symlink pointing to same file are the same */
        create_symlink(path3, path1, false);
        check(tr_sys_path_is_same(path1, path3, &err));
        check_ptr(err, ==, NULL);
        check(tr_sys_path_is_same(path3, path1, &err));
        check_ptr(err, ==, NULL);

        /* Symlinks pointing to non-existent files are not the same */
        tr_sys_path_remove(path1, NULL);
        create_symlink(path1, "missing", false);
        tr_sys_path_remove(path3, NULL);
        create_symlink(path3, "missing", false);
        check(!tr_sys_path_is_same(path1, path3, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_is_same(path3, path1, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path3, NULL);

        /* Symlinks pointing to same non-existent file are not the same */
        create_symlink(path3, ".." NATIVE_PATH_SEP "missing", false);
        check(!tr_sys_path_is_same(path1, path3, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_is_same(path3, path1, &err));
        check_ptr(err, ==, NULL);

        /* Non-existent file and symlink pointing to non-existent file are not the same */
        tr_sys_path_remove(path3, NULL);
        check(!tr_sys_path_is_same(path1, path3, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_is_same(path3, path1, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path2, NULL);
        tr_sys_path_remove(path1, NULL);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_free(path3);
    path3 = tr_buildPath(test_dir, "c", NULL);

    libtest_create_file_with_string_contents(path1, "test");

    if (create_hardlink(path2, path1))
    {
        /* File and hardlink to it are the same */
        check(tr_sys_path_is_same(path1, path2, &err));
        check_ptr(err, ==, NULL);

        /* Two hardlinks to the same file are the same */
        create_hardlink(path3, path2);
        check(tr_sys_path_is_same(path2, path3, &err));
        check_ptr(err, ==, NULL);
        check(tr_sys_path_is_same(path1, path3, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path2, NULL);

        check(tr_sys_path_is_same(path1, path3, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path3, NULL);

        /* File and hardlink to another file are not the same */
        libtest_create_file_with_string_contents(path3, "test");
        create_hardlink(path2, path3);
        check(!tr_sys_path_is_same(path1, path2, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_is_same(path2, path1, &err));
        check_ptr(err, ==, NULL);

        tr_sys_path_remove(path3, NULL);
        tr_sys_path_remove(path2, NULL);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

    if (create_symlink(path2, path1, false) && create_hardlink(path3, path1))
    {
        check(tr_sys_path_is_same(path2, path3, &err));
        check_ptr(err, ==, NULL);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run combined symlink and hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path3, NULL);
    tr_sys_path_remove(path2, NULL);
    tr_sys_path_remove(path1, NULL);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_path_resolve(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    char* path2;

    path1 = tr_buildPath(test_dir, "a", NULL);
    path2 = tr_buildPath(test_dir, "b", NULL);

    libtest_create_file_with_string_contents(path1, "test");

    if (create_symlink(path2, path1, false))
    {
        char* tmp;

        tmp = tr_sys_path_resolve(path2, &err);
        check_str(tmp, !=, NULL);
        check_ptr(err, ==, NULL);
        check(path_contains_no_symlinks(tmp));
        tr_free(tmp);

        tr_sys_path_remove(path2, NULL);
        tr_sys_path_remove(path1, NULL);

        tr_sys_dir_create(path1, 0, 0755, NULL);
        check(create_symlink(path2, path1, true)); /* Win32: directory and file symlinks differ :( */
        tmp = tr_sys_path_resolve(path2, &err);
        check_str(tmp, !=, NULL);
        check_ptr(err, ==, NULL);
        check(path_contains_no_symlinks(tmp));
        tr_free(tmp);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path2, NULL);
    tr_sys_path_remove(path1, NULL);

    tr_free(path2);
    tr_free(path1);

#ifdef _WIN32

    {
        char* tmp;

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\NonExistent", &err);
        check_str(tmp, ==, NULL);
        check_ptr(err, !=, NULL);
        tr_error_clear(&err);

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\ADMIN$\\NonExistent", &err);
        check_str(tmp, ==, NULL);
        check_ptr(err, !=, NULL);
        tr_error_clear(&err);

        tmp = tr_sys_path_resolve("\\\\127.0.0.1\\ADMIN$\\System32", &err);
        check_str(tmp, ==, "\\\\127.0.0.1\\ADMIN$\\System32");
        check_ptr(err, ==, NULL);
        tr_free(tmp);
    }

#endif

    tr_free(test_dir);
    return 0;
}

struct xname_test_data
{
    char const* input;
    char const* output;
};

static int test_path_xname(struct xname_test_data const* data, size_t data_size, char* (*func)(char const*, tr_error**))
{
    for (size_t i = 0; i < data_size; ++i)
    {
        tr_error* err = NULL;
        char* name = func(data[i].input, &err);

        if (data[i].output != NULL)
        {
            check_str(name, !=, NULL);
            check_ptr(err, ==, NULL);
            check_str(name, ==, data[i].output);
            tr_free(name);
        }
        else
        {
            check_str(name, ==, NULL);
            check_ptr(err, !=, NULL);
            tr_error_clear(&err);
        }
    }

    return 0;
}

static int test_path_basename_dirname(void)
{
    struct xname_test_data const common_xname_tests[] =
    {
        { "/", "/" },
        { "", "." },
#ifdef _WIN32
        { "\\", "/" },
        /* Invalid paths */
        { "\\\\\\", NULL },
        { "123:", NULL },
        /* Reserved characters */
        { "<", NULL },
        { ">", NULL },
        { ":", NULL },
        { "\"", NULL },
        { "|", NULL },
        { "?", NULL },
        { "*", NULL },
        { "a\\<", NULL },
        { "a\\>", NULL },
        { "a\\:", NULL },
        { "a\\\"", NULL },
        { "a\\|", NULL },
        { "a\\?", NULL },
        { "a\\*", NULL },
        { "c:\\a\\b<c\\d", NULL },
        { "c:\\a\\b>c\\d", NULL },
        { "c:\\a\\b:c\\d", NULL },
        { "c:\\a\\b\"c\\d", NULL },
        { "c:\\a\\b|c\\d", NULL },
        { "c:\\a\\b?c\\d", NULL },
        { "c:\\a\\b*c\\d", NULL }
#else
        { "////", "/" }
#endif
    };

    if (test_path_xname(common_xname_tests, TR_N_ELEMENTS(common_xname_tests), tr_sys_path_basename) != 0)
    {
        return 1;
    }

    if (test_path_xname(common_xname_tests, TR_N_ELEMENTS(common_xname_tests), tr_sys_path_dirname) != 0)
    {
        return 1;
    }

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

    if (test_path_xname(basename_tests, TR_N_ELEMENTS(basename_tests), tr_sys_path_basename) != 0)
    {
        return 1;
    }

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

    if (test_path_xname(dirname_tests, TR_N_ELEMENTS(dirname_tests), tr_sys_path_dirname) != 0)
    {
        return 1;
    }

    /* TODO: is_same(dirname(x) + '/' + basename(x), x) */

    return 0;
}

static int test_path_rename(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    char* path2;
    char* path3;

    path1 = tr_buildPath(test_dir, "a", NULL);
    path2 = tr_buildPath(test_dir, "b", NULL);
    path3 = tr_buildPath(path2, "c", NULL);

    libtest_create_file_with_string_contents(path1, "test");

    /* Preconditions */
    check(tr_sys_path_exists(path1, NULL));
    check(!tr_sys_path_exists(path2, NULL));

    /* Forward rename works */
    check(tr_sys_path_rename(path1, path2, &err));
    check(!tr_sys_path_exists(path1, NULL));
    check(tr_sys_path_exists(path2, NULL));
    check_ptr(err, ==, NULL);

    /* Backward rename works */
    check(tr_sys_path_rename(path2, path1, &err));
    check(tr_sys_path_exists(path1, NULL));
    check(!tr_sys_path_exists(path2, NULL));
    check_ptr(err, ==, NULL);

    /* Another backward rename [of non-existent file] does not work */
    check(!tr_sys_path_rename(path2, path1, &err));
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);

    /* Rename to file which couldn't be created does not work */
    check(!tr_sys_path_rename(path1, path3, &err));
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);

    /* Rename of non-existent file does not work */
    check(!tr_sys_path_rename(path3, path2, &err));
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);

    libtest_create_file_with_string_contents(path2, "test");

    /* Renaming file does overwrite existing file */
    check(tr_sys_path_rename(path2, path1, &err));
    check_ptr(err, ==, NULL);

    tr_sys_dir_create(path2, 0, 0777, NULL);

    /* Renaming file does not overwrite existing directory, and vice versa */
    check(!tr_sys_path_rename(path1, path2, &err));
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);
    check(!tr_sys_path_rename(path2, path1, &err));
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);

    tr_sys_path_remove(path2, NULL);

    tr_free(path3);
    path3 = tr_buildPath(test_dir, "c", NULL);

    if (create_symlink(path2, path1, false))
    {
        /* Preconditions */
        check(tr_sys_path_exists(path2, NULL));
        check(!tr_sys_path_exists(path3, NULL));
        check(tr_sys_path_is_same(path1, path2, NULL));

        /* Rename of symlink works, files stay the same */
        check(tr_sys_path_rename(path2, path3, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_exists(path2, NULL));
        check(tr_sys_path_exists(path3, NULL));
        check(tr_sys_path_is_same(path1, path3, NULL));

        tr_sys_path_remove(path3, NULL);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

    if (create_hardlink(path2, path1))
    {
        /* Preconditions */
        check(tr_sys_path_exists(path2, NULL));
        check(!tr_sys_path_exists(path3, NULL));
        check(tr_sys_path_is_same(path1, path2, NULL));

        /* Rename of hardlink works, files stay the same */
        check(tr_sys_path_rename(path2, path3, &err));
        check_ptr(err, ==, NULL);
        check(!tr_sys_path_exists(path2, NULL));
        check(tr_sys_path_exists(path3, NULL));
        check(tr_sys_path_is_same(path1, path3, NULL));

        tr_sys_path_remove(path3, NULL);
    }
    else
    {
        fprintf(stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

    tr_sys_path_remove(path1, NULL);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_path_remove(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    char* path2;
    char* path3;

    path1 = tr_buildPath(test_dir, "a", NULL);
    path2 = tr_buildPath(test_dir, "b", NULL);
    path3 = tr_buildPath(path2, "c", NULL);

    /* Can't remove non-existent file/directory */
    check(!tr_sys_path_exists(path1, NULL));
    check(!tr_sys_path_remove(path1, &err));
    check_ptr(err, !=, NULL);
    check(!tr_sys_path_exists(path1, NULL));
    tr_error_clear(&err);

    /* Removing file works */
    libtest_create_file_with_string_contents(path1, "test");
    check(tr_sys_path_exists(path1, NULL));
    check(tr_sys_path_remove(path1, &err));
    check_ptr(err, ==, NULL);
    check(!tr_sys_path_exists(path1, NULL));

    /* Removing empty directory works */
    tr_sys_dir_create(path1, 0, 0777, NULL);
    check(tr_sys_path_exists(path1, NULL));
    check(tr_sys_path_remove(path1, &err));
    check_ptr(err, ==, NULL);
    check(!tr_sys_path_exists(path1, NULL));

    /* Removing non-empty directory fails */
    tr_sys_dir_create(path2, 0, 0777, NULL);
    libtest_create_file_with_string_contents(path3, "test");
    check(tr_sys_path_exists(path2, NULL));
    check(tr_sys_path_exists(path3, NULL));
    check(!tr_sys_path_remove(path2, &err));
    check_ptr(err, !=, NULL);
    check(tr_sys_path_exists(path2, NULL));
    check(tr_sys_path_exists(path3, NULL));
    tr_error_clear(&err);

    tr_sys_path_remove(path3, NULL);
    tr_sys_path_remove(path2, NULL);

    tr_free(path3);
    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_path_native_separators(void)
{
    check_str(tr_sys_path_native_separators(NULL), ==, NULL);

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

        check_str(output, ==, test_data[i].output);
        check_str(test_data[i].input, ==, test_data[i].output);
        check_ptr(output, ==, test_data[i].input);
    }

    return 0;
}

static int test_file_open(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    tr_sys_file_t fd;
    uint64_t n;
    tr_sys_path_info info;

    path1 = tr_buildPath(test_dir, "a", NULL);

    /* Can't open non-existent file */
    check(!tr_sys_path_exists(path1, NULL));
    check(tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err) == TR_BAD_SYS_FILE);
    check_ptr(err, !=, NULL);
    check(!tr_sys_path_exists(path1, NULL));
    tr_error_clear(&err);
    check(tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err) == TR_BAD_SYS_FILE);
    check_ptr(err, !=, NULL);
    check(!tr_sys_path_exists(path1, NULL));
    tr_error_clear(&err);

    /* Can't open directory */
    tr_sys_dir_create(path1, 0, 0777, NULL);
#ifdef _WIN32
    /* This works on *NIX */
    check(tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err) == TR_BAD_SYS_FILE);
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);
#endif
    check(tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err) == TR_BAD_SYS_FILE);
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);

    tr_sys_path_remove(path1, NULL);

    /* Can create non-existent file */
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0640, &err);
    check_int(fd, !=, TR_BAD_SYS_FILE);
    check_ptr(err, ==, NULL);
    tr_sys_file_close(fd, NULL);
    check(tr_sys_path_exists(path1, NULL));
    check(validate_permissions(path1, 0640));

    /* Can open existing file */
    check(tr_sys_path_exists(path1, NULL));
    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0600, &err);
    check_int(fd, !=, TR_BAD_SYS_FILE);
    check_ptr(err, ==, NULL);
    tr_sys_file_close(fd, NULL);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE, 0600, &err);
    check_int(fd, !=, TR_BAD_SYS_FILE);
    check_ptr(err, ==, NULL);
    tr_sys_file_close(fd, NULL);

    tr_sys_path_remove(path1, NULL);
    libtest_create_file_with_string_contents(path1, "test");

    /* Can't create new file if it already exists */
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE_NEW, 0640, &err);
    check_int(fd, ==, TR_BAD_SYS_FILE);
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, NULL);
    check_uint(info.size, ==, 4);

    /* Pointer is at the end of file */
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, NULL);
    check_uint(info.size, ==, 4);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_APPEND, 0600, &err);
    check_int(fd, !=, TR_BAD_SYS_FILE);
    check_ptr(err, ==, NULL);
    tr_sys_file_write(fd, "s", 1, NULL, NULL); /* On *NIX, pointer is positioned on each write but not initially */
    tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, NULL);
    check_uint(n, ==, 5);
    tr_sys_file_close(fd, NULL);

    /* File gets truncated */
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, NULL);
    check_uint(info.size, ==, 5);
    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0600, &err);
    check_int(fd, !=, TR_BAD_SYS_FILE);
    check_ptr(err, ==, NULL);
    tr_sys_file_get_info(fd, &info, NULL);
    check_uint(info.size, ==, 0);
    tr_sys_file_close(fd, NULL);
    tr_sys_path_get_info(path1, TR_SYS_PATH_NO_FOLLOW, &info, NULL);
    check_uint(info.size, ==, 0);

    /* TODO: symlink and hardlink tests */

    tr_sys_path_remove(path1, NULL);

    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_file_read_write_seek(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    tr_sys_file_t fd;
    uint64_t n;
    char buf[100];

    path1 = tr_buildPath(test_dir, "a", NULL);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, NULL);

    check(tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 0);

    check(tr_sys_file_write(fd, "test", 4, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 4);

    check(tr_sys_file_seek(fd, 0, TR_SEEK_CUR, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 4);

    check(tr_sys_file_seek(fd, 0, TR_SEEK_SET, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 0);

    check(tr_sys_file_read(fd, buf, sizeof(buf), &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 4);

    check_mem(buf, ==, "test", 4);

    check(tr_sys_file_seek(fd, -3, TR_SEEK_CUR, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 1);

    check(tr_sys_file_write(fd, "E", 1, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 1);

    check(tr_sys_file_seek(fd, -2, TR_SEEK_CUR, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 0);

    check(tr_sys_file_read(fd, buf, sizeof(buf), &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 4);

    check_mem(buf, ==, "tEst", 4);

    check(tr_sys_file_seek(fd, 0, TR_SEEK_END, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 4);

    check(tr_sys_file_write(fd, " ok", 3, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 3);

    check(tr_sys_file_seek(fd, 0, TR_SEEK_SET, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 0);

    check(tr_sys_file_read(fd, buf, sizeof(buf), &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 7);

    check_mem(buf, ==, "tEst ok", 7);

    check(tr_sys_file_write_at(fd, "-", 1, 4, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 1);

    check(tr_sys_file_read_at(fd, buf, 5, 2, &n, &err));
    check_ptr(err, ==, NULL);
    check_uint(n, ==, 5);

    check_mem(buf, ==, "st-ok", 5);

    tr_sys_file_close(fd, NULL);

    tr_sys_path_remove(path1, NULL);

    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_file_truncate(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    tr_sys_file_t fd;
    tr_sys_path_info info;

    path1 = tr_buildPath(test_dir, "a", NULL);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, NULL);

    check(tr_sys_file_truncate(fd, 10, &err));
    check_ptr(err, ==, NULL);
    tr_sys_file_get_info(fd, &info, NULL);
    check_uint(info.size, ==, 10);

    check(tr_sys_file_truncate(fd, 20, &err));
    check_ptr(err, ==, NULL);
    tr_sys_file_get_info(fd, &info, NULL);
    check_uint(info.size, ==, 20);

    check(tr_sys_file_truncate(fd, 0, &err));
    check_ptr(err, ==, NULL);
    tr_sys_file_get_info(fd, &info, NULL);
    check_uint(info.size, ==, 0);

    check(tr_sys_file_truncate(fd, 50, &err));
    check_ptr(err, ==, NULL);

    tr_sys_file_close(fd, NULL);

    tr_sys_path_get_info(path1, 0, &info, NULL);
    check_uint(info.size, ==, 50);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, NULL);

    check(tr_sys_file_truncate(fd, 25, &err));
    check_ptr(err, ==, NULL);

    tr_sys_file_close(fd, NULL);

    tr_sys_path_get_info(path1, 0, &info, NULL);
    check_uint(info.size, ==, 25);

    tr_sys_path_remove(path1, NULL);

    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_file_preallocate(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    tr_sys_file_t fd;
    tr_sys_path_info info;

    path1 = tr_buildPath(test_dir, "a", NULL);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, NULL);

    if (tr_sys_file_preallocate(fd, 50, 0, &err))
    {
        check_ptr(err, ==, NULL);
        tr_sys_file_get_info(fd, &info, NULL);
        check_uint(info.size, ==, 50);
    }
    else
    {
        check_ptr(err, !=, NULL);
        fprintf(stderr, "WARNING: [%s] unable to preallocate file (full): %s (%d)\n", __FUNCTION__, err->message, err->code);
        tr_error_clear(&err);
    }

    tr_sys_file_close(fd, NULL);

    tr_sys_path_remove(path1, NULL);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE, 0600, NULL);

    if (tr_sys_file_preallocate(fd, 500 * 1024 * 1024, TR_SYS_FILE_PREALLOC_SPARSE, &err))
    {
        check_ptr(err, ==, NULL);
        tr_sys_file_get_info(fd, &info, NULL);
        check_uint(info.size, ==, 500 * 1024 * 1024);
    }
    else
    {
        check_ptr(err, !=, NULL);
        fprintf(stderr, "WARNING: [%s] unable to preallocate file (sparse): %s (%d)\n", __FUNCTION__, err->message, err->code);
        tr_error_clear(&err);
    }

    tr_sys_file_close(fd, NULL);

    tr_sys_path_remove(path1, NULL);

    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_file_map(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    tr_sys_file_t fd;
    char* view;

    path1 = tr_buildPath(test_dir, "a", NULL);

    libtest_create_file_with_string_contents(path1, "test");

    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE, 0600, NULL);

    view = tr_sys_file_map_for_reading(fd, 0, 4, &err);
    check_ptr(view, !=, NULL);
    check_ptr(err, ==, NULL);

    check_mem(view, ==, "test", 4);

#ifdef HAVE_UNIFIED_BUFFER_CACHE

    tr_sys_file_write_at(fd, "E", 1, 1, NULL, NULL);

    check_mem(view, ==, "tEst", 4);

#endif

    check(tr_sys_file_unmap(view, 4, &err));
    check_ptr(err, ==, NULL);

    tr_sys_file_close(fd, NULL);

    tr_sys_path_remove(path1, NULL);

    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_file_utilities(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    tr_sys_file_t fd;
    char buffer[16];

    path1 = tr_buildPath(test_dir, "a", NULL);

    libtest_create_file_with_string_contents(path1, "a\nbc\r\ndef\nghij\r\n\n\nklmno\r");

    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ, 0, NULL);

    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "a");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "bc");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "def");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "ghij");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "");
    check(tr_sys_file_read_line(fd, buffer, 4, &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "klmn");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "o");
    check(!tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "o"); /* on EOF, buffer stays unchanged */

    tr_sys_file_close(fd, NULL);

    fd = tr_sys_file_open(path1, TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_TRUNCATE, 0, NULL);

    check(tr_sys_file_write_line(fd, "p", &err));
    check_ptr(err, ==, NULL);
    check(tr_sys_file_write_line(fd, "", &err));
    check_ptr(err, ==, NULL);
    check(tr_sys_file_write_line(fd, "qr", &err));
    check_ptr(err, ==, NULL);
    check(tr_sys_file_write_fmt(fd, "s%cu\r\n", &err, 't'));
    check_ptr(err, ==, NULL);
    check(tr_sys_file_write_line(fd, "", &err));
    check_ptr(err, ==, NULL);
    check(tr_sys_file_write_line(fd, "", &err));
    check_ptr(err, ==, NULL);
    check(tr_sys_file_write_fmt(fd, "v%sy%d", &err, "wx", 2));
    check_ptr(err, ==, NULL);

    tr_sys_file_seek(fd, 0, TR_SEEK_SET, NULL, NULL);

    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "p");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "qr");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "stu");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "");
    check(tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "vwxy2");
    check(!tr_sys_file_read_line(fd, buffer, TR_N_ELEMENTS(buffer), &err));
    check_ptr(err, ==, NULL);
    check_str(buffer, ==, "vwxy2"); /* on EOF, buffer stays unchanged */

    tr_sys_file_close(fd, NULL);

    tr_sys_path_remove(path1, NULL);

    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_dir_create(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    tr_error* err = NULL;
    char* path1;
    char* path2;

    path1 = tr_buildPath(test_dir, "a", NULL);
    path2 = tr_buildPath(path1, "b", NULL);

    /* Can create directory which has parent */
    check(tr_sys_dir_create(path1, 0, 0700, &err));
    check_ptr(err, ==, NULL);
    check(tr_sys_path_exists(path1, NULL));
    check(validate_permissions(path1, 0700));

    tr_sys_path_remove(path1, NULL);
    libtest_create_file_with_string_contents(path1, "test");

    /* Can't create directory where file already exists */
    check(!tr_sys_dir_create(path1, 0, 0700, &err));
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);
    check(!tr_sys_dir_create(path1, TR_SYS_DIR_CREATE_PARENTS, 0700, &err));
    check_ptr(err, !=, NULL);
    tr_error_clear(&err);

    tr_sys_path_remove(path1, NULL);

    /* Can't create directory which has no parent */
    check(!tr_sys_dir_create(path2, 0, 0700, &err));
    check_ptr(err, !=, NULL);
    check(!tr_sys_path_exists(path2, NULL));
    tr_error_clear(&err);

    /* Can create directory with parent directories */
    check(tr_sys_dir_create(path2, TR_SYS_DIR_CREATE_PARENTS, 0751, &err));
    check_ptr(err, ==, NULL);
    check(tr_sys_path_exists(path1, NULL));
    check(tr_sys_path_exists(path2, NULL));
    check(validate_permissions(path1, 0751));
    check(validate_permissions(path2, 0751));

    /* Can create existing directory (no-op) */
    check(tr_sys_dir_create(path1, 0, 0700, &err));
    check_ptr(err, ==, NULL);
    check(tr_sys_dir_create(path1, TR_SYS_DIR_CREATE_PARENTS, 0700, &err));
    check_ptr(err, ==, NULL);

    tr_sys_path_remove(path2, NULL);
    tr_sys_path_remove(path1, NULL);

    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

static int test_dir_read_impl(char const* path, bool* have1, bool* have2)
{
    tr_error* err = NULL;
    tr_sys_dir_t dd;
    char const* name;

    *have1 = *have2 = false;

    dd = tr_sys_dir_open(path, &err);
    check_ptr(dd, !=, TR_BAD_SYS_DIR);
    check_ptr(err, ==, NULL);

    while ((name = tr_sys_dir_read_name(dd, &err)) != NULL)
    {
        check_ptr(err, ==, NULL);

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
            check(false);
        }
    }

    check_ptr(err, ==, NULL);

    check(tr_sys_dir_close(dd, &err));
    check_ptr(err, ==, NULL);

    return 0;
}

static int test_dir_read(void)
{
    char* const test_dir = create_test_dir(__FUNCTION__);
    char* path1;
    char* path2;
    bool have1;
    bool have2;

    path1 = tr_buildPath(test_dir, "a", NULL);
    path2 = tr_buildPath(test_dir, "b", NULL);

    if (test_dir_read_impl(test_dir, &have1, &have2) != 0)
    {
        return 1;
    }

    check(!have1);
    check(!have2);

    libtest_create_file_with_string_contents(path1, "test");

    if (test_dir_read_impl(test_dir, &have1, &have2) != 0)
    {
        return 1;
    }

    check(have1);
    check(!have2);

    libtest_create_file_with_string_contents(path2, "test");

    if (test_dir_read_impl(test_dir, &have1, &have2) != 0)
    {
        return 1;
    }

    check(have1);
    check(have2);

    tr_sys_path_remove(path1, NULL);

    if (test_dir_read_impl(test_dir, &have1, &have2) != 0)
    {
        return 1;
    }

    check(!have1);
    check(have2);

    tr_free(path2);
    tr_free(path1);

    tr_free(test_dir);
    return 0;
}

int main(void)
{
    testFunc const tests[] =
    {
        test_get_info,
        test_path_exists,
        test_path_is_relative,
        test_path_is_same,
        test_path_resolve,
        test_path_basename_dirname,
        test_path_rename,
        test_path_remove,
        test_path_native_separators,
        test_file_open,
        test_file_read_write_seek,
        test_file_truncate,
        test_file_preallocate,
        test_file_map,
        test_file_utilities,
        test_dir_create,
        test_dir_read
    };

    /* init the session */
    session = libttest_session_init(NULL);

    int ret = runTests(tests, NUM_TESTS(tests));

    libttest_session_close(session);

    return ret;
}
