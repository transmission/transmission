/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef TR_UTILS_H
#define TR_UTILS_H 1

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h> /* size_t */
#include <time.h> /* time_t */

#ifdef __cplusplus
extern "C" {
#endif

/***
****
***/

struct evbuffer;
struct event;
struct timeval;

struct tr_error;

/**
 * @addtogroup utils Utilities
 * @{
 */

#ifndef UNUSED
 #ifdef __GNUC__
  #define UNUSED __attribute__ ((unused))
 #else
  #define UNUSED
 #endif
#endif

#ifndef TR_GNUC_PRINTF
 #ifdef __GNUC__
  #define TR_GNUC_PRINTF(fmt, args) __attribute__ ((format (printf, fmt, args)))
 #else
  #define TR_GNUC_PRINTF(fmt, args)
 #endif
#endif

#ifndef TR_GNUC_NONNULL
 #ifdef __GNUC__
  #define TR_GNUC_NONNULL(...) __attribute__ ((nonnull (__VA_ARGS__)))
 #else
  #define TR_GNUC_NONNULL(...)
 #endif
#endif

#ifndef TR_GNUC_NULL_TERMINATED
 #if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3)
  #define TR_GNUC_NULL_TERMINATED __attribute__ ((__sentinel__))
  #define TR_GNUC_HOT __attribute ((hot))
 #else
  #define TR_GNUC_NULL_TERMINATED
  #define TR_GNUC_HOT
 #endif
#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96)
 #define TR_GNUC_MALLOC __attribute__ ((__malloc__))
#else
 #define TR_GNUC_MALLOC
#endif


#ifndef __has_feature
 #define __has_feature(x) 0
#endif
#ifndef __has_extension
 #define __has_extension __has_feature
#endif

#ifdef __UCLIBC__
 #define TR_UCLIBC_CHECK_VERSION(major, minor, micro) \
   (__UCLIBC_MAJOR__ > (major) || \
    (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ > (minor)) || \
    (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ == (minor) && \
       __UCLIBC_SUBLEVEL__ >= (micro)))
#else
 #define TR_UCLIBC_CHECK_VERSION(major, minor, micro) 0
#endif

/**
 * @def TR_STATIC_ASSERT
 * @brief This helper allows to perform static checks at compile time
 */
#if defined (static_assert)
 #define TR_STATIC_ASSERT static_assert
#elif __has_feature (c_static_assert) || __has_extension (c_static_assert)
 #define TR_STATIC_ASSERT _Static_assert
#else
 #define TR_STATIC_ASSERT(x, msg) { typedef char __tr_static_check__[(x) ? 1 : -1] UNUSED; }
#endif


/***
****
***/

const char * tr_strip_positional_args (const char * fmt);

#if !defined (_)
 #if defined (HAVE_LIBINTL_H) && !defined (__APPLE__)
  #include <libintl.h>
  #define _(a) gettext (a)
 #else
  #define _(a)(a)
 #endif
#endif

/* #define DISABLE_GETTEXT */
#ifndef DISABLE_GETTEXT
 #if defined (_WIN32) || defined (TR_LIGHTWEIGHT)
   #define DISABLE_GETTEXT
 #endif
#endif
#ifdef DISABLE_GETTEXT
 #undef _
 #define _(a) tr_strip_positional_args (a)
#endif

/****
*****
****/

/**
 * @brief Rich Salz's classic implementation of shell-style pattern matching for ?, \, [], and * characters.
 * @return 1 if the pattern matches, 0 if it doesn't, or -1 if an error occured
 */
bool tr_wildmat (const char * text, const char * pattern) TR_GNUC_NONNULL (1,2);


/**
 * @brief Loads a file and returns its contents.
 * On failure, NULL is returned and errno is set.
 */
uint8_t * tr_loadFile (const char       * filename,
                       size_t           * size,
                       struct tr_error ** error) TR_GNUC_MALLOC TR_GNUC_NONNULL (1);


/** @brief build a filename from a series of elements using the
           platform's correct directory separator. */
char* tr_buildPath (const char * first_element, ...) TR_GNUC_NULL_TERMINATED
                                                      TR_GNUC_MALLOC;

/**
 * @brief Get available disk space (in bytes) for the specified folder.
 * @return zero or positive integer on success, -1 in case of error.
 */
int64_t tr_getDirFreeSpace (const char * path);

/**
 * @brief Convenience wrapper around timer_add () to have a timer wake up in a number of seconds and microseconds
 * @param timer
 * @param seconds
 * @param microseconds
 */
void tr_timerAdd (struct event * timer, int seconds, int microseconds) TR_GNUC_NONNULL (1);

/**
 * @brief Convenience wrapper around timer_add () to have a timer wake up in a number of milliseconds
 * @param timer
 * @param milliseconds
 */
void tr_timerAddMsec (struct event * timer, int milliseconds) TR_GNUC_NONNULL (1);


/** @brief return the current date in milliseconds */
uint64_t tr_time_msec (void);

/** @brief sleep the specified number of milliseconds */
void tr_wait_msec (long int delay_milliseconds);

/**
 * @brief make a copy of 'str' whose non-utf8 content has been corrected or stripped
 * @return a newly-allocated string that must be freed with tr_free ()
 * @param str the string to make a clean copy of
 * @param len the length of the string to copy. If -1, the entire string is used.
 */
char* tr_utf8clean (const char * str, size_t len) TR_GNUC_MALLOC;

#ifdef _WIN32

char    * tr_win32_native_to_utf8    (const wchar_t * text,
                                      int             text_size);
wchar_t * tr_win32_utf8_to_native    (const char    * text,
                                      int             text_size);
wchar_t * tr_win32_utf8_to_native_ex (const char    * text,
                                      int             text_size,
                                      int             extra_chars_before,
                                      int             extra_chars_after,
                                      int           * real_result_size);
char    * tr_win32_format_message    (uint32_t        code);

void      tr_win32_make_args_utf8    (int    * argc,
                                      char *** argv);

int       tr_main_win32              (int     argc,
                                      char ** argv,
                                      int   (*real_main) (int, char **));

#define tr_main(...) \
  main_impl (__VA_ARGS__); \
  int \
  main (int    argc, \
        char * argv[]) \
  { \
    return tr_main_win32 (argc, argv, &main_impl); \
  } \
  int \
  main_impl (__VA_ARGS__)

#else

#define tr_main main

#endif

/***
****
***/

/* Sometimes the system defines MAX/MIN, sometimes not.
   In the latter case, define those here since we will use them */
#ifndef MAX
 #define MAX(a, b)((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
 #define MIN(a, b)((a) > (b) ? (b) : (a))
#endif

/***
****
***/

/** @brief Portability wrapper around malloc () in which `0' is a safe argument */
void* tr_malloc (size_t size);

/** @brief Portability wrapper around calloc () in which `0' is a safe argument */
void* tr_malloc0 (size_t size);

/** @brief Portability wrapper around reallocf () in which `0' is a safe argument */
void * tr_realloc (void * p, size_t size);

/** @brief Portability wrapper around free () in which `NULL' is a safe argument */
void tr_free (void * p);

/**
 * @brief make a newly-allocated copy of a chunk of memory
 * @param src the memory to copy
 * @param byteCount the number of bytes to copy
 * @return a newly-allocated copy of `src' that can be freed with tr_free ()
 */
void* tr_memdup (const void * src, size_t byteCount);

#define tr_new(struct_type, n_structs)           \
  ((struct_type *) tr_malloc (sizeof (struct_type) * ((size_t)(n_structs))))

#define tr_new0(struct_type, n_structs)          \
  ((struct_type *) tr_malloc0 (sizeof (struct_type) * ((size_t)(n_structs))))

#define tr_renew(struct_type, mem, n_structs)    \
  ((struct_type *) tr_realloc ((mem), sizeof (struct_type) * ((size_t)(n_structs))))

void* tr_valloc (size_t bufLen);

/**
 * @brief make a newly-allocated copy of a substring
 * @param in is a void* so that callers can pass in both signed & unsigned without a cast
 * @param len length of the substring to copy. if a length less than zero is passed in, strlen (len) is used
 * @return a newly-allocated copy of `in' that can be freed with tr_free ()
 */
char* tr_strndup (const void * in, size_t len) TR_GNUC_MALLOC;

/**
 * @brief make a newly-allocated copy of a string
 * @param in is a void* so that callers can pass in both signed & unsigned without a cast
 * @return a newly-allocated copy of `in' that can be freed with tr_free ()
 */
char* tr_strdup (const void * in);

/**
 * @brief like strcmp () but gracefully handles NULL strings
 */
int tr_strcmp0 (const char * str1, const char * str2);

char* evbuffer_free_to_str (struct evbuffer * buf,
                            size_t          * result_len);

/** @brief similar to bsearch () but returns the index of the lower bound */
int tr_lowerBound (const void * key,
                   const void * base,
                   size_t       nmemb,
                   size_t       size,
                   int     (* compar)(const void* key, const void* arrayMember),
                   bool       * exact_match) TR_GNUC_HOT TR_GNUC_NONNULL (1,5,6);

/** @brief moves the best k items to the first slots in the array. O(n) */
void tr_quickfindFirstK (void * base, size_t nmemb, size_t size,
                         int (*compar)(const void *, const void *), size_t k);

/**
 * @brief sprintf () a string into a newly-allocated buffer large enough to hold it
 * @return a newly-allocated string that can be freed with tr_free ()
 */
char* tr_strdup_printf (const char * fmt, ...) TR_GNUC_PRINTF (1, 2)
                                                TR_GNUC_MALLOC;
char * tr_strdup_vprintf (const char * fmt,
                          va_list      args) TR_GNUC_MALLOC;

/** @brief Portability wrapper for strlcpy () that uses the system implementation if available */
size_t tr_strlcpy (char * dst, const void * src, size_t siz);

/** @brief Portability wrapper for snprintf () that uses the system implementation if available */
int tr_snprintf (char * buf, size_t buflen,
                 const char * fmt, ...) TR_GNUC_PRINTF (3, 4) TR_GNUC_NONNULL (1,3);

/** @brief Convenience wrapper around strerorr () guaranteed to not return NULL
    @param errno */
const char* tr_strerror (int);

/** @brief strips leading and trailing whitspace from a string
    @return the stripped string */
char* tr_strstrip (char * str);

/** @brief Returns true if the string ends with the specified case-insensitive suffix */
bool tr_str_has_suffix (const char *str, const char *suffix);


/** @brief Portability wrapper for memmem () that uses the system implementation if available */
const char* tr_memmem (const char * haystack, size_t haystack_len,
                       const char * needle, size_t needle_len);

/** @brief Portability wrapper for strsep () that uses the system implementation if available */
char* tr_strsep (char ** str, const char * delim);

/***
****
***/

int compareInt (const void * va, const void * vb);

void tr_binary_to_hex (const void * input, char * output, size_t byte_length) TR_GNUC_NONNULL (1,2);
void tr_hex_to_binary (const char * input, void * output, size_t byte_length) TR_GNUC_NONNULL (1,2);

/** @brief convenience function to determine if an address is an IP address (IPv4 or IPv6) */
bool tr_addressIsIP (const char * address);

/** @brief return true if the url is a http or https or UDP url that Transmission understands */
bool tr_urlIsValidTracker (const char * url);

/** @brief return true if the url is a [ http, https, ftp, sftp ] url that Transmission understands */
bool tr_urlIsValid (const char * url, size_t url_len);

/** @brief parse a URL into its component parts
    @return True on success or false if an error occurred */
bool tr_urlParse (const char * url,
                  size_t       url_len,
                  char      ** setme_scheme,
                  char      ** setme_host,
                  int        * setme_port,
                  char      ** setme_path) TR_GNUC_NONNULL (1);


/** @brief return TR_RATIO_NA, TR_RATIO_INF, or a number in [0..1]
    @return TR_RATIO_NA, TR_RATIO_INF, or a number in [0..1] */
double tr_getRatio (uint64_t numerator, uint64_t denominator);

/**
 * @brief Given a string like "1-4" or "1-4,6,9,14-51", this returns a
 *        newly-allocated array of all the integers in the set.
 * @return a newly-allocated array of integers that must be freed with tr_free (),
 *         or NULL if a fragment of the string can't be parsed.
 *
 * For example, "5-8" will return [ 5, 6, 7, 8 ] and setmeCount will be 4.
 */
int* tr_parseNumberRange (const char * str,
                          size_t       str_len,
                          int        * setmeCount) TR_GNUC_MALLOC TR_GNUC_NONNULL (1);


/**
 * @brief truncate a double value at a given number of decimal places.
 *
 * this can be used to prevent a printf () call from rounding up:
 * call with the decimal_places argument equal to the number of
 * decimal places in the printf ()'s precision:
 *
 * - printf ("%.2f%%",           99.999  ) ==> "100.00%"
 *
 * - printf ("%.2f%%", tr_truncd (99.999, 2)) ==>  "99.99%"
 *             ^                        ^
 *             |   These should match   |
 *             +------------------------+
 */
double tr_truncd (double x, int decimal_places);

/* return a percent formatted string of either x.xx, xx.x or xxx */
char* tr_strpercent (char * buf, double x, size_t buflen);

/**
 * @param buf the buffer to write the string to
 * @param buflef buf's size
 * @param ratio the ratio to convert to a string
 * @param the string represntation of "infinity"
 */
char* tr_strratio (char * buf, size_t buflen, double ratio, const char * infinity) TR_GNUC_NONNULL (1,4);

/** @brief Portability wrapper for localtime_r () that uses the system implementation if available */
struct tm * tr_localtime_r (const time_t *_clock, struct tm *_result);

/** @brief Portability wrapper for gettimeofday (), with tz argument dropped */
int tr_gettimeofday (struct timeval * tv);


/**
 * @brief move a file
 * @return `True` on success, `false` otherwise (with `error` set accordingly).
 */
bool tr_moveFile (const char       * oldpath,
                  const char       * newpath,
                  struct tr_error ** error) TR_GNUC_NONNULL (1,2);

/** @brief convenience function to remove an item from an array */
void tr_removeElementFromArray (void         * array,
                                unsigned int   index_to_remove,
                                size_t         sizeof_element,
                                size_t         nmemb);

/***
****
***/

/** @brief Private libtransmission variable that's visible only for inlining in tr_time () */
extern time_t __tr_current_time;

/**
 * @brief very inexpensive form of time (NULL)
 * @return the current epoch time in seconds
 *
 * This function returns a second counter that is updated once per second.
 * If something blocks the libtransmission thread for more than a second,
 * that counter may be thrown off, so this function is not guaranteed
 * to always be accurate. However, it is *much* faster when 100% accuracy
 * isn't needed
 */
static inline time_t tr_time (void) { return __tr_current_time; }

/** @brief Private libtransmission function to update tr_time ()'s counter */
static inline void tr_timeUpdate (time_t now) { __tr_current_time = now; }

/** @brief Portability wrapper for htonll () that uses the system implementation if available */
uint64_t tr_htonll (uint64_t);

/** @brief Portability wrapper for htonll () that uses the system implementation if available */
uint64_t tr_ntohll (uint64_t);

/***
****
***/

/* example: tr_formatter_size_init (1024, _ ("KiB"), _ ("MiB"), _ ("GiB"), _ ("TiB")); */

void tr_formatter_size_init (unsigned int kilo, const char * kb, const char * mb,
                                                const char * gb, const char * tb);

void tr_formatter_speed_init (unsigned int kilo, const char * kb, const char * mb,
                                                 const char * gb, const char * tb);

void tr_formatter_mem_init (unsigned int kilo, const char * kb, const char * mb,
                                               const char * gb, const char * tb);

extern unsigned int tr_speed_K;
extern unsigned int tr_mem_K;
extern unsigned int tr_size_K;

/* format a speed from KBps into a user-readable string. */
char* tr_formatter_speed_KBps (char * buf, double KBps, size_t buflen);

/* format a memory size from bytes into a user-readable string. */
char* tr_formatter_mem_B (char * buf, int64_t bytes, size_t buflen);

/* format a memory size from MB into a user-readable string. */
static inline char* tr_formatter_mem_MB (char * buf, double MBps, size_t buflen) { return tr_formatter_mem_B (buf, MBps * tr_mem_K * tr_mem_K, buflen); }

/* format a file size from bytes into a user-readable string. */
char* tr_formatter_size_B (char * buf, int64_t bytes, size_t buflen);

void tr_formatter_get_units (void * dict);

/***
****
***/

/** @brief Check if environment variable exists. */
bool   tr_env_key_exists (const char * key);

/** @brief Get environment variable value as int. */
int    tr_env_get_int    (const char * key,
                          int          default_value);

/** @brief Get environment variable value as string (should be freed afterwards). */
char * tr_env_get_string (const char * key,
                          const char * default_value);

/***
****
***/

void tr_net_init (void);

/***
****
***/

#ifdef __cplusplus
}
#endif

/** @} */

#endif
