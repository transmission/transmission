/*
 * This file Copyright (C) 2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct tr_session_id* tr_session_id_t;

/**
 * Create new session identifier object.
 *
 * @return New session identifier object.
 */
tr_session_id_t tr_session_id_new(void);

/**
 * Free session identifier object.
 *
 * @param[in] session_id Session identifier object.
 */
void tr_session_id_free(tr_session_id_t session_id);

/**
 * Get current session identifier as string.
 *
 * @param[in] session_id Session identifier object.
 *
 * @return String representation of current session identifier.
 */
char const* tr_session_id_get_current(tr_session_id_t session_id);

/**
 * Check if session ID corresponds to session running on the same machine as
 * the caller.
 *
 * This is useful for various behavior alterations, such as transforming
 * relative paths to absolute before passing through RPC, or presenting
 * different UI for local and remote sessions.
 *
 * @param[in] session_id String representation of session identifier object.
 *
 * @return `True` if session is valid and local, `false` otherwise.
 */
bool tr_session_id_is_local(char const* session_id);

#ifdef __cplusplus
}
#endif
