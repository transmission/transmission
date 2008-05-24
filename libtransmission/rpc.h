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

#ifndef TR_RPC_H
#define TR_RPC_H

/***
****  RPC processing
***/

struct tr_benc;
struct tr_handle;

/* http://www.json.org/ */
char*
tr_rpc_request_exec_json( struct tr_handle  * handle,
                          const void        * request_json,
                          int                 request_len,
                          int               * response_len );

/* see the RPC spec's "Request URI Notation" section */
char*
tr_rpc_request_exec_uri( struct tr_handle  * handle,
                         const void        * request_uri,
                         int                 request_len,
                         int               * response_len );

void
tr_rpc_parse_list_str( struct tr_benc  * setme,
                       const char      * list_str,
                       size_t            list_str_len );

#endif
