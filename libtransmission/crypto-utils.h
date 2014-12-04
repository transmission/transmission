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

#include <stddef.h>

/**
*** @addtogroup utils Utilities
*** @{
**/

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
