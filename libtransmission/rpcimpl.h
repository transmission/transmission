/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
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

#include "transmission.h"
#include "variant.h"

/***
****  RPC processing
***/

typedef void (* tr_rpc_response_func)(tr_session* session, tr_variant* response, void* user_data);

/* http://www.json.org/ */
void tr_rpc_request_exec_json(tr_session* session, tr_variant const* request, tr_rpc_response_func callback,
    void* callback_user_data);

/* see the RPC spec's "Request URI Notation" section */
void tr_rpc_request_exec_uri(tr_session* session, void const* request_uri, size_t request_uri_len,
    tr_rpc_response_func callback, void* callback_user_data);

void tr_rpc_parse_list_str(tr_variant* setme, char const* list_str, size_t list_str_len);

#ifdef __cplusplus
}
#endif
