/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef TR_CRYPTO_UTILS_H
#define TR_CRYPTO_UTILS_H

#include <inttypes.h>
#include <stddef.h>

#include "utils.h" /* TR_GNUC_NULL_TERMINATED */

/**
*** @addtogroup utils Utilities
*** @{
**/

 /** @brief Opaque SHA1 context type. */
typedef void * tr_sha1_ctx_t;

/**
 * @brief Generate a SHA1 hash from one or more chunks of memory.
 */
bool             tr_sha1               (uint8_t        * hash,
                                        const void     * data1,
                                        int              data1_length,
                                                         ...) TR_GNUC_NULL_TERMINATED;

/**
 * @brief Allocate and initialize new SHA1 hasher context.
 */
tr_sha1_ctx_t    tr_sha1_init          (void);

/**
 * @brief Update SHA1 hash.
 */
bool             tr_sha1_update        (tr_sha1_ctx_t    handle,
                                        const void     * data,
                                        size_t           data_length);

/**
 * @brief Finalize and export SHA1 hash, free hasher context.
 */
bool             tr_sha1_final         (tr_sha1_ctx_t    handle,
                                        uint8_t        * hash);
/**
 * @brief Returns a random number in the range of [0...upper_bound).
 */
int              tr_rand_int           (int              upper_bound);

/**
 * @brief Returns a pseudorandom number in the range of [0...upper_bound).
 *
 * This is faster, BUT WEAKER, than tr_rand_int () and never be used in sensitive cases.
 * @see tr_rand_int ()
 */
int              tr_rand_int_weak      (int              upper_bound);

/**
 * @brief Fill a buffer with random bytes.
 */
bool             tr_rand_buffer        (void           * buffer,
                                        size_t           length);

/** @} */

#endif /* TR_CRYPTO_UTILS_H */
