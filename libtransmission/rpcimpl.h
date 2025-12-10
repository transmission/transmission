// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int16_t
#include <functional>
#include <optional>

struct tr_session;
struct tr_variant;

namespace JsonRpc
{
auto constexpr Version = std::string_view{ "2.0" };

namespace Error
{
enum Code : int16_t
{
    PARSE_ERROR = -32700,
    INVALID_REQUEST = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS = -32602,
    INTERNAL_ERROR = -32603,
    SUCCESS = 0,
    SET_ANNOUNCE_LIST,
    INVALID_TRACKER_LIST,
    PATH_NOT_ABSOLUTE,
    UNRECOGNIZED_INFO,
    SYSTEM_ERROR,
    FILE_IDX_OOR,
    PIECE_IDX_OOR,
    HTTP_ERROR,
    CORRUPT_TORRENT
};

[[nodiscard]] std::string_view to_string(Code code);

[[nodiscard]] std::optional<Code> to_code(std::string_view errmsg);
} // namespace Error
} // namespace JsonRpc

using tr_rpc_response_func = std::function<void(tr_session* session, tr_variant&& response)>;

void tr_rpc_request_exec(tr_session* session, tr_variant const& request, tr_rpc_response_func&& callback = {});

void tr_rpc_request_exec(tr_session* session, std::string_view request, tr_rpc_response_func&& callback = {});
