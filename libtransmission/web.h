// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "transmission.h"

struct evbuffer;

enum tr_web_close_mode
{
    TR_WEB_CLOSE_WHEN_IDLE,
    TR_WEB_CLOSE_NOW
};

void tr_webClose(tr_session* session, tr_web_close_mode close_mode);

using tr_web_done_func = void (*)(
    tr_session* session,
    bool did_connect_flag,
    bool timeout_flag,
    long response_code,
    std::string_view response,
    void* user_data);

class tr_web_options
{
public:
    tr_web_options(std::string_view url_in, tr_web_done_func done_func_in, void* done_func_user_data_in)
        : url{ url_in }
        , done_func{ done_func_in }
        , done_func_user_data{ done_func_user_data_in }
    {
    }

    std::string url;
    std::optional<int> torrent_id;
    tr_web_done_func done_func = nullptr;
    void* done_func_user_data = nullptr;
    std::string range;
    std::string cookies;
    evbuffer* buffer = nullptr;
};

void tr_webRun(tr_session* session, tr_web_options&& options);
