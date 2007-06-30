#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "rc4.h"

struct tr_rc4_s
{
    uint8_t x;      
    uint8_t y;
    uint8_t state[256];
};

static void
swapByte( uint8_t* a, uint8_t* b )
{
    const uint8_t tmp = *a;
    *a = *b;
    *b = tmp;
}

tr_rc4_t *
tr_rc4New ( const uint8_t * key,
            int             keylen )
{
    uint8_t index1 = 0;
    uint8_t index2 = 0;
    uint8_t * state;
    int i;
    tr_rc4_t * ret = calloc( 1, sizeof(tr_rc4_t) );

    /* initialize the state array */
    state = ret->state;
    for( i=0; i<256; ++i )
        state[i] = (uint8_t)i;

    ret->x = 0;
    ret->y = 0;
    for( i=0; i<256; ++i )
    {
        index2 = (key[index1] + state[i] + index2) % 256;
        swapByte (state+i, state+index2);
        index1 = (index1 + 1) % keylen;
    }

    return ret;
}

void
tr_rc4Free( tr_rc4_t * rc4 )
{
    free( rc4 );
}

void
tr_rc4Step( tr_rc4_t     * key,
            uint8_t      * buf,
            int            buflen )
{
    uint8_t x = key->x;
    uint8_t y = key->y;
    uint8_t * state = key->state;
    uint8_t xorIndex;
    int i;

    for( i=0; i<buflen; ++i )
    {
        x = (x + 1) % 256;
        y = (state[x] + y) % 256;
        swapByte( state+x, state+y );
        xorIndex = (state[x] + state[y]) % 256;
        buf[i] ^= state[xorIndex];
    }

    key->x = x;
    key->y = y;
}
