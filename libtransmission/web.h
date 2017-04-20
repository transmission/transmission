/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <curl/curl.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct tr_address;
struct tr_web_task;

typedef enum
{
    TR_WEB_GET_CODE = CURLINFO_RESPONSE_CODE,
    TR_WEB_GET_REDIRECTS = CURLINFO_REDIRECT_COUNT,
    TR_WEB_GET_REAL_URL = CURLINFO_EFFECTIVE_URL
}
tr_web_task_info;

typedef enum
{
    TR_WEB_CLOSE_WHEN_IDLE,
    TR_WEB_CLOSE_NOW
}
tr_web_close_mode;

void tr_webClose(tr_session* session, tr_web_close_mode close_mode);

typedef void (* tr_web_done_func)(tr_session* session, bool did_connect_flag, bool timeout_flag, long response_code,
    void const* response, size_t response_byte_count, void* user_data);

char const* tr_webGetResponseStr(long response_code);

struct tr_web_task* tr_webRun(tr_session* session, char const* url, tr_web_done_func done_func, void* done_func_user_data);

struct tr_web_task* tr_webRunWithCookies(tr_session* session, char const* url, char const* cookies, tr_web_done_func done_func,
    void* done_func_user_data);

struct evbuffer;

struct tr_web_task* tr_webRunWebseed(tr_torrent* tor, char const* url, char const* range, tr_web_done_func done_func,
    void* done_func_user_data, struct evbuffer* buffer);

void tr_webGetTaskInfo(struct tr_web_task* task, tr_web_task_info info, void* dst);

void tr_http_escape(struct evbuffer* out, char const* str, size_t len, bool escape_slashes);

void tr_http_escape_sha1(char* out, uint8_t const* sha1_digest);

char* tr_http_unescape(char const* str, size_t len);

#ifdef __cplusplus
}
#endif
