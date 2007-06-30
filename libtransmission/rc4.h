#ifndef TR_RC4_H
#define TR_RC4_H

typedef struct tr_rc4_s tr_rc4_t;

tr_rc4_t *  tr_rc4New     ( const uint8_t   * key,
                            int               keylen );

void        tr_rc4Step    ( tr_rc4_t        * rc4,
                            uint8_t         * buf,
                            int               buflen );

void        tr_rc4Free    ( tr_rc4_t        * rc4 );

#endif
