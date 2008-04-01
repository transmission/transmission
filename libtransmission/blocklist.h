/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef TR_BLOCKLIST_H
#define TR_BLOCKLIST_H

struct in_addr;
typedef struct tr_blocklist tr_blocklist;

tr_blocklist* _tr_blocklistNew         ( const char * filename, int isEnabled );
void          _tr_blocklistFree        ( tr_blocklist * b );
int           _tr_blocklistGetRuleCount( tr_blocklist * b );
int           _tr_blocklistIsEnabled   ( tr_blocklist * b );
void          _tr_blocklistSetEnabled  ( tr_blocklist * b, int isEnabled );
int           _tr_blocklistHasAddress  ( tr_blocklist * b, const struct in_addr * addr );
int           _tr_blocklistExists      ( const tr_blocklist * b );
int           _tr_blocklistSetContent  ( tr_blocklist * b, const char * filename );

#endif
