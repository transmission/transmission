/*
 * This file Copyright (C) 2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tr_session_id * tr_session_id_t;

/**
 * Create new session identifier object.
 *
 * @return New session identifier object.
 */
tr_session_id_t tr_session_id_new (void);

/**
 * Free session identifier object.
 *
 * @param[in] session_id Session identifier object.
 */
void tr_session_id_free (tr_session_id_t session_id);

/**
 * Get current session identifier as string.
 *
 * @param[in] session_id Session identifier object.
 *
 * @return String representation of current session identifier.
 */
const char * tr_session_id_get_current (tr_session_id_t session_id);

#ifdef __cplusplus
}
#endif

