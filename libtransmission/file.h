/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef TR_FILE_H
#define TR_FILE_H

#include <inttypes.h>
#include <time.h>

#ifdef WIN32
 #include <windows.h>
#endif

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup file_io File IO
 * @{
 */

typedef enum
{
    TR_SYS_PATH_NO_FOLLOW = 1 << 0
}
tr_sys_path_get_info_flags_t;

typedef enum
{
  TR_SYS_PATH_IS_FILE,
  TR_SYS_PATH_IS_DIRECTORY,
  TR_SYS_PATH_IS_OTHER
}
tr_sys_path_type_t;

typedef struct tr_sys_path_info
{
  tr_sys_path_type_t type;
  uint64_t           size;
  time_t             last_modified_at;
}
tr_sys_path_info;

/**
 * @name Platform-specific wrapper functions
 *
 * Following functions accept paths in UTF-8 encoding and convert them to native
 * encoding internally if needed.
 *
 * @{
 */

/* Path-related wrappers */

/**
 * @brief Portability wrapper for `stat ()`.
 *
 * @param[in]  path  Path to file or directory.
 * @param[in]  flags Combination of @ref tr_sys_path_get_info_flags_t values.
 * @param[out] info  Result buffer.
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_path_get_info        (const char         * path,
                                             int                  flags,
                                             tr_sys_path_info   * info,
                                             tr_error          ** error);

/**
 * @brief Portability wrapper for `access ()`.
 *
 * @param[in]  path  Path to file or directory.
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return `True` if path exists, `false` otherwise. Note that `false` will also
 *         be returned in case of error; if you need to distinguish the two,
 *         check if `error` is `NULL` afterwards.
 */
bool            tr_sys_path_exists          (const char         * path,
                                             tr_error          ** error);

/**
 * @brief Test to see if the two filenames point to the same file.
 *
 * @param[in]  path1  Path to first file or directory.
 * @param[in]  path2  Path to second file or directory.
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return `True` if two paths point to the same file or directory, `false`
 *         otherwise. Note that `false` will also be returned in case of error;
 *         if you need to distinguish the two, check if `error` is `NULL`
 *         afterwards.
 */
bool            tr_sys_path_is_same         (const char         * path1,
                                             const char         * path2,
                                             tr_error          ** error);

/**
 * @brief Portability wrapper for `realpath ()`.
 *
 * @param[in]  path  Path to file or directory.
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return Pointer to newly allocated buffer containing full path (with symbolic
 *         links, `.` and `..` resolved) on success (use @ref tr_free to free it
 *         when no longer needed), `NULL` otherwise (with `error` set
 *         accordingly).
 */
char          * tr_sys_path_resolve         (const char         * path,
                                             tr_error          ** error);

/**
 * @brief Portability wrapper for `basename ()`.
 *
 * @param[in]  path  Path to file or directory.
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return Pointer to newly allocated buffer containing base name (last path
 *         component; parent path removed) on success (use @ref tr_free to free
 *         it when no longer needed), `NULL` otherwise (with `error` set
 *         accordingly).
 */
char          * tr_sys_path_basename        (const char         * path,
                                             tr_error          ** error);

/**
 * @brief Portability wrapper for `dirname ()`.
 *
 * @param[in]  path  Path to file or directory.
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return Pointer to newly allocated buffer containing directory (parent path;
 *         last path component removed) on success (use @ref tr_free to free it
 *         when no longer needed), `NULL` otherwise (with `error` set
 *         accordingly).
 */
char          * tr_sys_path_dirname         (const char         * path,
                                             tr_error          ** error);

/**
 * @brief Portability wrapper for `rename ()`.
 *
 * @param[in]  src_path Path to source file or directory.
 * @param[in]  dst_path Path to destination file or directory.
 * @param[out] error    Pointer to error object. Optional, pass `NULL` if you
 *                      are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 *         Rename will generally only succeed if both source and destination are
 *         on the same partition.
 */
bool            tr_sys_path_rename          (const char         * src_path,
                                             const char         * dst_path,
                                             tr_error          ** error);

/**
 * @brief Portability wrapper for `remove ()`.
 *
 * @param[in]  path  Path to file or directory.
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 *         Directory removal will only succeed if it is empty (contains no other
 *         files and directories).
 */
bool            tr_sys_path_remove          (const char         * path,
                                             tr_error          ** error);

/** @} */
/** @} */

#ifdef __cplusplus
}
#endif

#endif
