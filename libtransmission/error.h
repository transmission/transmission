/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <stdarg.h>

#include "tr-macros.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup error Error reporting
 * @{
 */

/** @brief Structure holding error information. */
typedef struct tr_error
{
    /** @brief Error code, platform-specific */
    int code;
    /** @brief Error message */
    char* message;
}
tr_error;

/**
 * @brief Create new error object using literal error message.
 *
 * @param[in] code    Error code (platform-specific).
 * @param[in] message Error message.
 *
 * @return Newly allocated error object on success, `NULL` otherwise.
 */
tr_error* tr_error_new_literal(int code, char const* message);

/**
 * @brief Create new error object using `vprintf`-style formatting.
 *
 * @param[in] code           Error code (platform-specific).
 * @param[in] message_format Error message format string.
 * @param[in] args           Format arguments.
 *
 * @return Newly allocated error object on success, `NULL` otherwise.
 */
tr_error* tr_error_new_valist(int code, char const* message_format, va_list args) TR_GNUC_PRINTF(2, 0);

/**
 * @brief Free memory used by error object.
 *
 * @param[in] error Error object to be freed.
 */
void tr_error_free(tr_error* error);

/**
 * @brief Create and set new error object using `printf`-style formatting.
 *
 * If passed pointer to error object is `NULL`, do nothing.
 *
 * @param[in,out] error          Pointer to error object to be set.
 * @param[in]     code           Error code (platform-specific).
 * @param[in]     message_format Error message format string.
 * @param[in]     ...            Format arguments.
 */
void tr_error_set(tr_error** error, int code, char const* message_format, ...) TR_GNUC_PRINTF(3, 4);

/**
 * @brief Create and set new error object using literal error message.
 *
 * If passed pointer to error object is `NULL`, do nothing.
 *
 * @param[in,out] error   Pointer to error object to be set.
 * @param[in]     code    Error code (platform-specific).
 * @param[in]     message Error message.
 */
void tr_error_set_literal(tr_error** error, int code, char const* message);

/**
 * @brief Propagate existing error object upwards.
 *
 * If passed pointer to new error object is not `NULL`, copy old error object to
 * new error object and free old error object. Otherwise, just free old error
 * object.
 *
 * @param[in,out] new_error Pointer to error object to be set.
 * @param[in,out] old_error Error object to be propagated. Cleared on return.
 */
void tr_error_propagate(tr_error** new_error, tr_error** old_error);

/**
 * @brief Clear error object.
 *
 * Free error object being pointed and set pointer to `NULL`. If passed pointer
 * is `NULL`, do nothing.
 *
 * @param[in,out] error Pointer to error object to be cleared.
 */
void tr_error_clear(tr_error** error);

/**
 * @brief Prefix message of exising error object.
 *
 * If passed pointer to error object is not `NULL`, prefix its message with
 * `printf`-style formatted text. Otherwise, do nothing.
 *
 * @param[in,out] error         Pointer to error object to be set.
 * @param[in]     prefix_format Prefix format string.
 * @param[in]     ...           Format arguments.
 */
void tr_error_prefix(tr_error** error, char const* prefix_format, ...) TR_GNUC_PRINTF(2, 3);

/**
 * @brief Prefix message and propagate existing error object upwards.
 *
 * If passed pointer to new error object is not `NULL`, copy old error object to
 * new error object, prefix its message with `printf`-style formatted text, and
 * free old error object. Otherwise, just free old error object.
 *
 * @param[in,out] new_error Pointer to error object to be set.
 * @param[in,out] old_error     Error object to be propagated. Cleared on return.
 * @param[in] prefix_format Prefix format string.
 * @param[in] ... Format arguments.
 */
void tr_error_propagate_prefixed(tr_error** new_error, tr_error** old_error, char const* prefix_format, ...) TR_GNUC_PRINTF(3,
    4);

/** @} */

#ifdef __cplusplus
}
#endif
