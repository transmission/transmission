/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* memset() */

#include "transmission.h"
#include "history.h"
#include "utils.h"

void tr_historyAdd(tr_recentHistory* h, time_t now, unsigned int n)
{
    if (h->slices[h->newest].date == now)
    {
        h->slices[h->newest].n += n;
    }
    else
    {
        if (++h->newest == TR_RECENT_HISTORY_PERIOD_SEC)
        {
            h->newest = 0;
        }

        h->slices[h->newest].date = now;
        h->slices[h->newest].n = n;
    }
}

unsigned int tr_historyGet(tr_recentHistory const* h, time_t now, unsigned int sec)
{
    unsigned int n = 0;
    time_t const cutoff = (now != 0 ? now : tr_time()) - sec;
    int i = h->newest;

    for (;;)
    {
        if (h->slices[i].date <= cutoff)
        {
            break;
        }

        n += h->slices[i].n;

        if (--i == -1)
        {
            i = TR_RECENT_HISTORY_PERIOD_SEC - 1; /* circular history */
        }

        if (i == h->newest)
        {
            break; /* we've come all the way around */
        }
    }

    return n;
}
