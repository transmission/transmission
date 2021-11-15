/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cstdint>
#include <string_view>

#include "transmission.h"

struct evbuffer;
struct tr_address;
struct tr_web_task;

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

struct tr_web_task* tr_webRun(tr_session* session, std::string_view url, tr_web_done_func done_func, void* done_func_user_data);

struct tr_web_task* tr_webRunWithCookies(
    tr_session* session,
    std::string_view url,
    std::string_view cookies,
    tr_web_done_func done_func,
    void* done_func_user_data);

struct tr_web_task* tr_webRunWebseed(
    tr_torrent* tor,
    std::string_view url,
    std::string_view range,
    tr_web_done_func done_func,
    void* done_func_user_data,
    struct evbuffer* buffer);

long tr_webGetTaskResponseCode(struct tr_web_task* task);

char const* tr_webGetTaskRealUrl(struct tr_web_task* task);
