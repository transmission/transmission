/*
 * This file Copyright (C) 2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h>
#include <time.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "session-id.h"
#include "utils.h"

#define SESSION_ID_SIZE         48
#define SESSION_ID_DURATION_SEC (60 * 60) /* expire in an hour */

struct tr_session_id
{
  char   * current_value;
  char   * previous_value;
  time_t   expires_at;
};

static char *
generate_new_session_id_value (void)
{
  const char   pool[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const size_t pool_size = sizeof (pool) - 1;

  char * buf = tr_new (char, SESSION_ID_SIZE + 1);

  tr_rand_buffer (buf, SESSION_ID_SIZE);
  for (size_t i = 0; i < SESSION_ID_SIZE; ++i)
    buf[i] = pool[(unsigned char) buf[i] % pool_size];
  buf[SESSION_ID_SIZE] = '\0';

  return buf;
}

tr_session_id_t
tr_session_id_new (void)
{
  return tr_new0 (struct tr_session_id, 1);
}

void
tr_session_id_free (tr_session_id_t session_id)
{
  if (session_id == NULL)
    return;

  tr_free (session_id->previous_value);
  tr_free (session_id->current_value);
  tr_free (session_id);
}

const char *
tr_session_id_get_current (tr_session_id_t session_id)
{
  const time_t now = tr_time ();

  if (session_id->current_value == NULL || now >= session_id->expires_at)
    {
      tr_free (session_id->previous_value);
      session_id->previous_value = session_id->current_value;
      session_id->current_value = generate_new_session_id_value ();
      session_id->expires_at = now + SESSION_ID_DURATION_SEC;
    }

  return session_id->current_value;
}
