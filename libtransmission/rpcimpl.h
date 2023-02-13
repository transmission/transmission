// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include "transmission.h"

struct tr_variant;

using tr_rpc_response_func = void (*)(tr_session* session, tr_variant* response, void* user_data);

/* https://www.json.org/ */
void tr_rpc_request_exec_json(
    tr_session* session,
    tr_variant const* request,
    tr_rpc_response_func callback,
    void* callback_user_data);

void tr_rpc_parse_list_str(tr_variant* setme, std::string_view str);
