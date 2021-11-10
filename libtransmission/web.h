/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cstdint>

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
    void const* response,
    size_t response_byte_count,
    void* user_data);

struct tr_web_task* tr_webRun(tr_session* session, char const* url, tr_web_done_func done_func, void* done_func_user_data);

struct tr_web_task* tr_webRunWithCookies(
    tr_session* session,
    char const* url,
    char const* cookies,
    tr_web_done_func done_func,
    void* done_func_user_data);

struct tr_web_task* tr_webRunWebseed(
    tr_torrent* tor,
    char const* url,
    char const* range,
    tr_web_done_func done_func,
    void* done_func_user_data,
    struct evbuffer* buffer);

long tr_webGetTaskResponseCode(struct tr_web_task* task);

char const* tr_webGetTaskRealUrl(struct tr_web_task* task);
