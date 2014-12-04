/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <stdlib.h> /* abs (), srand (), rand () */

#include "transmission.h"
#include "crypto-utils.h"
#include "utils.h"

/***
****
***/

int
tr_rand_int (int upper_bound)
{
  int noise;

  assert (upper_bound > 0);

  while (tr_rand_buffer (&noise, sizeof (noise)))
    {
      noise = abs(noise) % upper_bound;
      /* abs(INT_MIN) is undefined and could return negative value */
      if (noise >= 0)
        return noise;
    }

  /* fall back to a weaker implementation... */
  return tr_rand_int_weak (upper_bound);
}

int
tr_rand_int_weak (int upper_bound)
{
  static bool init = false;

  assert (upper_bound > 0);

  if (!init)
    {
      srand (tr_time_msec ());
      init = true;
    }

  return rand () % upper_bound;
}
