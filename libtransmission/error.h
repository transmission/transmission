// This file Copyright © 2013-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include "tr-macros.h"

/**
 * @addtogroup error Error reporting
 * @{
 */

/** @brief Structure holding error information. */
struct tr_error
{
    /** @brief Error code, platform-specific */
    int code;
    /** @brief Error message */
    char* message;
};

/**
 * @brief Free memory used by error object.
 *
 * @param[in] error Error object to be freed.
 */
void tr_error_free(tr_error* error);

/**
 * @brief Create and set new error object using literal error message.
 *
 * If passed pointer to error object is `nullptr`, do nothing.
 *
 * @param[in,out] error   Pointer to error object to be set.
 * @param[in]     code    Error code (platform-specific).
 * @param[in]     message Error message.
 */
void tr_error_set(tr_error** error, int code, std::string_view message);

/**
 * @brief shorthand for `tr_error_set(error, errno, tr_strerror(errno))`
 */
void tr_error_set_from_errno(tr_error** error, int errnum);

/**
 * @brief Propagate existing error object upwards.
 *
 * If passed pointer to new error object is not `nullptr`, copy old error object
 * to new error object and free old error object. Otherwise, just free old error
 * object.
 *
 * @param[in,out] new_error Pointer to error object to be set.
 * @param[in,out] old_error Error object to be propagated. Cleared on return.
 */
void tr_error_propagate(tr_error** new_error, tr_error** old_error);

/**
 * @brief Clear error object.
 *
 * Free error object being pointed and set pointer to `nullptr`. If passed
 * pointer is `nullptr`, do nothing.
 *
 * @param[in,out] error Pointer to error object to be cleared.
 */
void tr_error_clear(tr_error** error);

/**
 * @brief Prefix message of existing error object.
 *
 * If passed pointer to error object is not `nullptr`, prefix its message with
 * `printf`-style formatted text. Otherwise, do nothing.
 *
 * @param[in,out] error         Pointer to error object to be set.
 * @param[in]     prefix_format Prefix format string.
 * @param[in]     ...           Format arguments.
 */
void tr_error_prefix(tr_error** error, char const* prefix);

/** @} */
