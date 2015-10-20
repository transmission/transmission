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

#ifdef _WIN32
 #include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct tr_error;

/**
 * @addtogroup file_io File IO
 * @{
 */

#ifndef _WIN32
 /** @brief Platform-specific file descriptor type. */
 typedef int tr_sys_file_t;
 /** @brief Platform-specific invalid file descriptor constant. */
 #define TR_BAD_SYS_FILE (-1)
 /** @brief Platform-specific directory descriptor type. */
 typedef void * tr_sys_dir_t;
 /** @brief Platform-specific end-of-line sequence. */
 #define TR_NATIVE_EOL_STR "\n"
 /** @brief Platform-specific end-of-line sequence length. */
 #define TR_NATIVE_EOL_STR_SIZE 1
#else
 typedef HANDLE tr_sys_file_t;
 #define TR_BAD_SYS_FILE INVALID_HANDLE_VALUE
 typedef struct tr_sys_dir_win32 * tr_sys_dir_t;
 #define TR_NATIVE_EOL_STR "\r\n"
 #define TR_NATIVE_EOL_STR_SIZE 2
#endif

/** @brief Platform-specific invalid directory descriptor constant. */
#define TR_BAD_SYS_DIR ((tr_sys_dir_t)NULL)

typedef enum
{
  TR_STD_SYS_FILE_IN,
  TR_STD_SYS_FILE_OUT,
  TR_STD_SYS_FILE_ERR
}
tr_std_sys_file_t;

typedef enum
{
  TR_SYS_FILE_READ       = 1 << 0,
  TR_SYS_FILE_WRITE      = 1 << 1,
  TR_SYS_FILE_CREATE     = 1 << 2,
  TR_SYS_FILE_CREATE_NEW = 1 << 3,
  TR_SYS_FILE_APPEND     = 1 << 4,
  TR_SYS_FILE_TRUNCATE   = 1 << 5,
  TR_SYS_FILE_SEQUENTIAL = 1 << 6
}
tr_sys_file_open_flags_t;

typedef enum
{
  TR_SEEK_SET,
  TR_SEEK_CUR,
  TR_SEEK_END
}
tr_seek_origin_t;

typedef enum
{
    TR_SYS_PATH_NO_FOLLOW = 1 << 0
}
tr_sys_path_get_info_flags_t;

typedef enum
{
    TR_SYS_FILE_PREALLOC_SPARSE = 1 << 0
}
tr_sys_file_preallocate_flags_t;

typedef enum
{
    TR_SYS_DIR_CREATE_PARENTS = 1 << 0
}
tr_sys_dir_create_flags_t;

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
 * Descriptors returned (@ref tr_sys_file_t and @ref tr_sys_dir_t) may have
 * different type depending on platform and should generally not be passed to
 * native functions, but to wrapper functions instead.
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
                                             struct tr_error   ** error);

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
                                             struct tr_error   ** error);

/**
 * @brief Check whether path is relative.
 *
 * This function only analyzes the string, so no error reporting is needed.
 *
 * @param[in] path Path to file or directory.
 *
 * @return `True` if path is relative, `false` otherwise
 */
bool            tr_sys_path_is_relative     (const char         * path);

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
                                             struct tr_error   ** error);

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
                                             struct tr_error   ** error);

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
                                             struct tr_error   ** error);

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
                                             struct tr_error   ** error);

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
                                             struct tr_error   ** error);

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
                                             struct tr_error   ** error);

/* File-related wrappers */

/**
 * @brief Get handle to one of standard I/O files.
 *
 * @param[in]  std_file Standard file identifier.
 * @param[out] error    Pointer to error object. Optional, pass `NULL` if you
 *                      are not interested in error details.
 *
 * @return Opened file descriptor on success, `TR_BAD_SYS_FILE` otherwise (with
 *         `error` set accordingly). DO NOT pass this descriptor to
 *         @ref tr_sys_file_close (unless you know what you are doing).
 */
tr_sys_file_t   tr_sys_file_get_std         (tr_std_sys_file_t    std_file,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `open ()`.
 *
 * @param[in]  path        Path to file.
 * @param[in]  flags       Combination of @ref tr_sys_file_open_flags_t values.
 * @param[in]  permissions Permissions to create file with (in case
                           @ref TR_SYS_FILE_CREATE is used). Not used on Windows.
 * @param[out] error       Pointer to error object. Optional, pass `NULL` if you
 *                         are not interested in error details.
 *
 * @return Opened file descriptor on success, `TR_BAD_SYS_FILE` otherwise (with
 *         `error` set accordingly).
 */
tr_sys_file_t   tr_sys_file_open            (const char         * path,
                                             int                  flags,
                                             int                  permissions,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `mkstemp ()`.
 *
 * @param[in,out] path_template Template path to file. Should end with at least
 *                              six 'X' characters. Upon success, trailing 'X'
 *                              characters are replaced with actual random
 *                              characters used to form a unique path to
 *                              temporary file.
 * @param[out]    error         Pointer to error object. Optional, pass `NULL`
 *                              if you are not interested in error details.
 *
 * @return Opened file descriptor on success, `TR_BAD_SYS_FILE` otherwise (with
 *         `error` set accordingly).
 */
tr_sys_file_t   tr_sys_file_open_temp       (char               * path_template,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `close ()`.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_close           (tr_sys_file_t        handle,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `fstat ()`.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[out] info   Result buffer.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_get_info        (tr_sys_file_t        handle,
                                             tr_sys_path_info   * info,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `lseek ()`.
 *
 * @param[in]  handle     Valid file descriptor.
 * @param[in]  offset     Relative file offset in bytes to seek to.
 * @param[in]  origin     Offset origin.
 * @param[out] new_offset New offset in bytes from beginning of file. Optional,
 *                        pass `NULL` if you are not interested.
 * @param[out] error      Pointer to error object. Optional, pass `NULL` if you
 *                        are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_seek            (tr_sys_file_t        handle,
                                             int64_t              offset,
                                             tr_seek_origin_t     origin,
                                             uint64_t           * new_offset,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `read ()`.
 *
 * @param[in]  handle     Valid file descriptor.
 * @param[out] buffer     Buffer to store read data to.
 * @param[in]  size       Number of bytes to read.
 * @param[out] bytes_read Number of bytes actually read. Optional, pass `NULL`
 *                        if you are not interested.
 * @param[out] error      Pointer to error object. Optional, pass `NULL` if you
 *                        are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_read            (tr_sys_file_t        handle,
                                             void               * buffer,
                                             uint64_t             size,
                                             uint64_t           * bytes_read,
                                             struct tr_error   ** error);

/**
 * @brief Like `pread ()`, except that the position is undefined afterwards.
 *        Not thread-safe.
 *
 * @param[in]  handle     Valid file descriptor.
 * @param[out] buffer     Buffer to store read data to.
 * @param[in]  size       Number of bytes to read.
 * @param[in]  offset     File offset in bytes to start reading from.
 * @param[out] bytes_read Number of bytes actually read. Optional, pass `NULL`
 *                        if you are not interested.
 * @param[out] error      Pointer to error object. Optional, pass `NULL` if you
 *                        are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_read_at         (tr_sys_file_t        handle,
                                             void               * buffer,
                                             uint64_t             size,
                                             uint64_t             offset,
                                             uint64_t           * bytes_read,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `write ()`.
 *
 * @param[in]  handle        Valid file descriptor.
 * @param[in]  buffer        Buffer to get data being written from.
 * @param[in]  size          Number of bytes to write.
 * @param[out] bytes_written Number of bytes actually written. Optional, pass
 *                           `NULL` if you are not interested.
 * @param[out] error         Pointer to error object. Optional, pass `NULL` if
 *                           you are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_write           (tr_sys_file_t        handle,
                                             const void         * buffer,
                                             uint64_t             size,
                                             uint64_t           * bytes_written,
                                             struct tr_error   ** error);

/**
 * @brief Like `pwrite ()`, except that the position is undefined afterwards.
 *        Not thread-safe.
 *
 * @param[in]  handle        Valid file descriptor.
 * @param[in]  buffer        Buffer to get data being written from.
 * @param[in]  size          Number of bytes to write.
 * @param[in]  offset        File offset in bytes to start writing from.
 * @param[out] bytes_written Number of bytes actually written. Optional, pass
 *                           `NULL` if you are not interested.
 * @param[out] error         Pointer to error object. Optional, pass `NULL` if you
 *                           are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_write_at        (tr_sys_file_t        handle,
                                             const void         * buffer,
                                             uint64_t             size,
                                             uint64_t             offset,
                                             uint64_t           * bytes_written,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `fsync ()`.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_flush           (tr_sys_file_t        handle,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `ftruncate ()`.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[in]  size   Number of bytes to truncate (or extend) file to.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_truncate        (tr_sys_file_t        handle,
                                             uint64_t             size,
                                             struct tr_error   ** error);

/**
 * @brief Tell system to prefetch some part of file which is to be read soon.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[in]  offset Offset in file to prefetch from.
 * @param[in]  size   Number of bytes to prefetch.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_prefetch        (tr_sys_file_t        handle,
                                             uint64_t             offset,
                                             uint64_t             size,
                                             struct tr_error   ** error);

/**
 * @brief Preallocate file to specified size in full or sparse mode.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[in]  size   Number of bytes to preallocate file to.
 * @param[in]  flags  Combination of @ref tr_sys_file_preallocate_flags_t values.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_preallocate     (tr_sys_file_t        handle,
                                             uint64_t             size,
                                             int                  flags,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `mmap ()` for files.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[in]  offset Offset in file to map from.
 * @param[in]  size   Number of bytes to map.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return Pointer to mapped file data on success, `NULL` otherwise (with
 *         `error` set accordingly).
 */
void          * tr_sys_file_map_for_reading (tr_sys_file_t        handle,
                                             uint64_t             offset,
                                             uint64_t             size,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `munmap ()` for files.
 *
 * @param[in]  address Pointer to mapped file data.
 * @param[in]  size    Size of mapped data in bytes.
 * @param[out] error   Pointer to error object. Optional, pass `NULL` if you are
 *                     not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_unmap           (const void         * address,
                                             uint64_t             size,
                                             struct tr_error   ** error);

/* File-related wrappers (utility) */

/**
 * @brief Portability wrapper for `fgets ()`, removing EOL internally.
 *
 * Special care should be taken when reading from one of standard input streams
 * (@ref tr_std_sys_file_t) since no UTF-8 conversion is currently being made.
 *
 * Reading from other streams (files, pipes) also leaves data untouched, so it
 * should already be in UTF-8 encoding, or whichever else you expect.
 *
 * @param[in]  handle      Valid file descriptor.
 * @param[out] buffer      Buffer to store read zero-terminated string to.
 * @param[in]  buffer_size Buffer size in bytes, taking '\0' character into
 *                         account.
 * @param[out] error       Pointer to error object. Optional, pass `NULL` if you
 *                         are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 *         Note that `false` will also be returned in case of end of file; if
 *         you need to distinguish the two, check if `error` is `NULL`
 *         afterwards.
 */
bool            tr_sys_file_read_line       (tr_sys_file_t        handle,
                                             char               * buffer,
                                             size_t               buffer_size,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `fputs ()`, appending EOL internally.
 *
 * Special care should be taken when writing to one of standard output streams
 * (@ref tr_std_sys_file_t) since no UTF-8 conversion is currently being made.
 *
 * Writing to other streams (files, pipes) also leaves data untouched, so it
 * should already be in UTF-8 encoding, or whichever else you expect.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[in]  buffer Zero-terminated string to write.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_write_line      (tr_sys_file_t        handle,
                                             const char         * buffer,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `fprintf ()`.
 *
 * Special care should be taken when writing to one of standard output streams
 * (@ref tr_std_sys_file_t) since no UTF-8 conversion is currently being made.
 *
 * Writing to other streams (files, pipes) also leaves data untouched, so it
 * should already be in UTF-8 encoding, or whichever else you expect.
 *
 * @param[in]  handle Valid file descriptor.
 * @param[in]  format String format to write.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 * @param[in]  ...    Format arguments.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_file_write_fmt       (tr_sys_file_t        handle,
                                             const char         * format,
                                             struct tr_error   ** error,
                                                                  ...);

/* Directory-related wrappers */

/**
 * @brief Portability wrapper for `getcwd ()`.
 *
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return Pointer to newly allocated buffer containing path to current
 *         directory (use @ref tr_free to free it when no longer needed) on
 *         success, `NULL` otherwise (with `error` set accordingly).
 */
char          * tr_sys_dir_get_current      (struct tr_error   ** error);

/**
 * @brief Like `mkdir ()`, but makes parent directories if needed.
 *
 * @param[in]  path        Path to directory.
 * @param[in]  flags       Combination of @ref tr_sys_dir_create_flags_t values.
 * @param[in]  permissions Permissions to create directory with. Not used on
                           Windows.
 * @param[out] error       Pointer to error object. Optional, pass `NULL` if you
 *                         are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_dir_create           (const char         * path,
                                             int                  flags,
                                             int                  permissions,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `mkdtemp ()`.
 *
 * @param[in,out] path_template Template path to directory. Should end with at
 *                              least six 'X' characters. Upon success, trailing
 *                              'X' characters are replaced with actual random
 *                              characters used to form a unique path to
 *                              temporary directory.
 * @param[out]    error         Pointer to error object. Optional, pass `NULL`
 *                              if you are not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_dir_create_temp      (char               * path_template,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `opendir ()`.
 *
 * @param[in]  path  Path to directory.
 * @param[out] error Pointer to error object. Optional, pass `NULL` if you are
 *                   not interested in error details.
 *
 * @return Opened directory descriptor on success, `TR_BAD_SYS_DIR` otherwise
 *         (with `error` set accordingly).
 */
tr_sys_dir_t    tr_sys_dir_open             (const char         * path,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `readdir ()`.
 *
 * @param[in]  handle Valid directory descriptor.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return Pointer to next directory entry name (stored internally, DO NOT pass
 *         it to @ref tr_free) on success, `NULL` otherwise (with `error` set
 *         accordingly). Note that `NULL` will also be returned in case of end
 *         of directory; if you need to distinguish the two, check if `error`
 *         is `NULL` afterwards.
 */
const char    * tr_sys_dir_read_name        (tr_sys_dir_t         handle,
                                             struct tr_error   ** error);

/**
 * @brief Portability wrapper for `closedir ()`.
 *
 * @param[in]  handle Valid directory descriptor.
 * @param[out] error  Pointer to error object. Optional, pass `NULL` if you are
 *                    not interested in error details.
 *
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool            tr_sys_dir_close            (tr_sys_dir_t         handle,
                                             struct tr_error   ** error);

/** @} */
/** @} */

#ifdef __cplusplus
}
#endif

#endif
