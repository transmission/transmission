/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <climits> /* USHRT_MAX */
#include <cstdio> /* fprintf() */
#include <cstring> /* strchr(), memcmp(), memcpy() */

#include <event2/buffer.h>
#include <event2/http.h> /* for HTTP_OK */

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"
#include "announcer-common.h"
#include "log.h"
#include "net.h" /* tr_globalIPv6() */
#include "peer-mgr.h" /* pex */
#include "torrent.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "variant.h"
#include "web.h"
#include "web-utils.h"

#define dbgmsg(name, ...) tr_logAddDeepNamed(name, __VA_ARGS__)

/****
*****
*****  ANNOUNCE
*****
****/

static char const* get_event_string(tr_announce_request const* req)
{
    return req->partial_seed && (req->event != TR_ANNOUNCE_EVENT_STOPPED) ? "paused" : tr_announce_event_get_string(req->event);
}

static char* announce_url_new(tr_session const* session, tr_announce_request const* req)
{
    auto const announce_sv = tr_quark_get_string_view(req->announce_url);

    char escaped_info_hash[SHA_DIGEST_LENGTH * 3 + 1];
    tr_http_escape_sha1(escaped_info_hash, req->info_hash);

    auto* const buf = evbuffer_new();
    evbuffer_expand(buf, 1024);
    evbuffer_add_printf(
        buf,
        "%" TR_PRIsv
        "%c"
        "info_hash=%s"
        "&peer_id=%" TR_PRIsv
        "&port=%d"
        "&uploaded=%" PRIu64 //
        "&downloaded=%" PRIu64 //
        "&left=%" PRIu64
        "&numwant=%d"
        "&key=%x"
        "&compact=1"
        "&supportcrypto=1",
        TR_PRIsv_ARG(announce_sv),
        announce_sv.find('?') == announce_sv.npos ? '?' : '&',
        escaped_info_hash,
        TR_PRIsv_ARG(req->peer_id),
        req->port,
        req->up,
        req->down,
        req->leftUntilComplete,
        req->numwant,
        req->key);

    if (session->encryptionMode == TR_ENCRYPTION_REQUIRED)
    {
        evbuffer_add_printf(buf, "&requirecrypto=1");
    }

    if (req->corrupt != 0)
    {
        evbuffer_add_printf(buf, "&corrupt=%" PRIu64, req->corrupt);
    }

    char const* str = get_event_string(req);
    if (!tr_str_is_empty(str))
    {
        evbuffer_add_printf(buf, "&event=%s", str);
    }

    str = req->tracker_id_str;
    if (!tr_str_is_empty(str))
    {
        evbuffer_add_printf(buf, "&trackerid=%s", str);
    }

    /* There are two incompatible techniques for announcing an IPv6 address.
       BEP-7 suggests adding an "ipv6=" parameter to the announce URL,
       while OpenTracker requires that peers announce twice, once over IPv4
       and once over IPv6.

       To be safe, we should do both: add the "ipv6=" parameter and
       announce twice. At any rate, we're already computing our IPv6
       address (for the LTEP handshake), so this comes for free. */

    unsigned char const* const ipv6 = tr_globalIPv6();
    if (ipv6 != nullptr)
    {
        char ipv6_readable[INET6_ADDRSTRLEN];
        evutil_inet_ntop(AF_INET6, ipv6, ipv6_readable, INET6_ADDRSTRLEN);
        evbuffer_add_printf(buf, "&ipv6=");
        tr_http_escape(buf, ipv6_readable, true);
    }

    return evbuffer_free_to_str(buf, nullptr);
}

static tr_pex* listToPex(tr_variant* peerList, size_t* setme_len)
{
    size_t n = 0;
    size_t const len = tr_variantListSize(peerList);
    tr_pex* pex = tr_new0(tr_pex, len);

    for (size_t i = 0; i < len; ++i)
    {
        tr_variant* const peer = tr_variantListChild(peerList, i);

        if (peer == nullptr)
        {
            continue;
        }

        auto ip = std::string_view{};
        if (!tr_variantDictFindStrView(peer, TR_KEY_ip, &ip))
        {
            continue;
        }

        auto addr = tr_address{};
        if (!tr_address_from_string(&addr, ip))
        {
            continue;
        }

        auto port = int64_t{};
        if (!tr_variantDictFindInt(peer, TR_KEY_port, &port))
        {
            continue;
        }

        if (port < 0 || port > USHRT_MAX)
        {
            continue;
        }

        if (!tr_address_is_valid_for_peers(&addr, port))
        {
            continue;
        }

        pex[n].addr = addr;
        pex[n].port = htons((uint16_t)port);
        ++n;
    }

    *setme_len = n;
    return pex;
}

struct announce_data
{
    tr_announce_response response;
    tr_announce_response_func response_func;
    void* response_func_user_data;
    char log_name[128];
};

static void on_announce_done_eventthread(void* vdata)
{
    auto* data = static_cast<struct announce_data*>(vdata);

    if (data->response_func != nullptr)
    {
        data->response_func(&data->response, data->response_func_user_data);
    }

    tr_free(data->response.pex6);
    tr_free(data->response.pex);
    tr_free(data->response.tracker_id_str);
    tr_free(data->response.warning);
    tr_free(data->response.errmsg);
    tr_free(data);
}

static void on_announce_done(
    tr_session* session,
    bool did_connect,
    bool did_timeout,
    long response_code,
    std::string_view msg,
    void* vdata)
{
    auto* data = static_cast<struct announce_data*>(vdata);

    tr_announce_response* const response = &data->response;
    response->did_connect = did_connect;
    response->did_timeout = did_timeout;
    dbgmsg(data->log_name, "Got announce response");

    if (response_code != HTTP_OK)
    {
        char const* fmt = _("Tracker gave HTTP response code %1$ld (%2$s)");
        char const* response_str = tr_webGetResponseStr(response_code);
        response->errmsg = tr_strdup_printf(fmt, response_code, response_str);
    }
    else
    {
        tr_variant benc;
        auto const variant_loaded = tr_variantFromBuf(&benc, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, msg);

        if (tr_env_key_exists("TR_CURL_VERBOSE"))
        {
            if (!variant_loaded)
            {
                fprintf(stderr, "%s", "Announce response was not in benc format\n");
            }
            else
            {
                auto len = size_t{};
                char* str = tr_variantToStr(&benc, TR_VARIANT_FMT_JSON, &len);
                fprintf(stderr, "%s", "Announce response:\n< ");

                for (size_t i = 0; i < len; ++i)
                {
                    fputc(str[i], stderr);
                }

                fputc('\n', stderr);
                tr_free(str);
            }
        }

        if (variant_loaded && tr_variantIsDict(&benc))
        {
            auto i = int64_t{};
            auto sv = std::string_view{};
            tr_variant* tmp = nullptr;

            if (tr_variantDictFindStrView(&benc, TR_KEY_failure_reason, &sv))
            {
                response->errmsg = tr_strvDup(sv);
            }

            if (tr_variantDictFindStrView(&benc, TR_KEY_warning_message, &sv))
            {
                response->warning = tr_strvDup(sv);
            }

            if (tr_variantDictFindInt(&benc, TR_KEY_interval, &i))
            {
                response->interval = i;
            }

            if (tr_variantDictFindInt(&benc, TR_KEY_min_interval, &i))
            {
                response->min_interval = i;
            }

            if (tr_variantDictFindStrView(&benc, TR_KEY_tracker_id, &sv))
            {
                response->tracker_id_str = tr_strvDup(sv);
            }

            if (tr_variantDictFindInt(&benc, TR_KEY_complete, &i))
            {
                response->seeders = i;
            }

            if (tr_variantDictFindInt(&benc, TR_KEY_incomplete, &i))
            {
                response->leechers = i;
            }

            if (tr_variantDictFindInt(&benc, TR_KEY_downloaded, &i))
            {
                response->downloads = i;
            }

            if (tr_variantDictFindStrView(&benc, TR_KEY_peers6, &sv))
            {
                dbgmsg(data->log_name, "got a peers6 length of %zu", std::size(sv));
                response->pex6 = tr_peerMgrCompact6ToPex(std::data(sv), std::size(sv), nullptr, 0, &response->pex6_count);
            }

            if (tr_variantDictFindStrView(&benc, TR_KEY_peers, &sv))
            {
                dbgmsg(data->log_name, "got a compact peers length of %zu", std::size(sv));
                response->pex = tr_peerMgrCompactToPex(std::data(sv), std::size(sv), nullptr, 0, &response->pex_count);
            }
            else if (tr_variantDictFindList(&benc, TR_KEY_peers, &tmp))
            {
                response->pex = listToPex(tmp, &response->pex_count);
                dbgmsg(data->log_name, "got a peers list with %zu entries", response->pex_count);
            }
        }

        if (variant_loaded)
        {
            tr_variantFree(&benc);
        }
    }

    tr_runInEventThread(session, on_announce_done_eventthread, data);
}

void tr_tracker_http_announce(
    tr_session* session,
    tr_announce_request const* request,
    tr_announce_response_func response_func,
    void* response_func_user_data)
{
    auto* const d = tr_new0(announce_data, 1);
    d->response.seeders = -1;
    d->response.leechers = -1;
    d->response.downloads = -1;
    d->response_func = response_func;
    d->response_func_user_data = response_func_user_data;
    d->response.info_hash = request->info_hash;
    tr_strlcpy(d->log_name, request->log_name, sizeof(d->log_name));

    char* const url = announce_url_new(session, request);
    dbgmsg(request->log_name, "Sending announce to libcurl: \"%s\"", url);
    tr_webRun(session, url, on_announce_done, d);
    tr_free(url);
}

/****
*****
*****  SCRAPE
*****
****/

struct scrape_data
{
    tr_scrape_response response;
    tr_scrape_response_func response_func;
    void* response_func_user_data;
    char log_name[128];
};

static void on_scrape_done_eventthread(void* vdata)
{
    auto* data = static_cast<struct scrape_data*>(vdata);

    if (data->response_func != nullptr)
    {
        data->response_func(&data->response, data->response_func_user_data);
    }

    delete data;
}

static void on_scrape_done(
    tr_session* session,
    bool did_connect,
    bool did_timeout,
    long response_code,
    std::string_view msg,
    void* vdata)
{
    auto* data = static_cast<struct scrape_data*>(vdata);

    tr_scrape_response* response = &data->response;
    response->did_connect = did_connect;
    response->did_timeout = did_timeout;

    auto const scrape_url_sv = tr_quark_get_string_view(response->scrape_url);
    dbgmsg(data->log_name, "Got scrape response for \"%" TR_PRIsv "\"", TR_PRIsv_ARG(scrape_url_sv));

    if (response_code != HTTP_OK)
    {
        char const* fmt = _("Tracker gave HTTP response code %1$ld (%2$s)");
        char const* response_str = tr_webGetResponseStr(response_code);
        char buf[512];
        tr_snprintf(buf, sizeof(buf), fmt, response_code, response_str);
        response->errmsg = buf;
    }
    else
    {
        auto top = tr_variant{};

        auto const variant_loaded = tr_variantFromBuf(&top, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, msg);

        if (tr_env_key_exists("TR_CURL_VERBOSE"))
        {
            if (!variant_loaded)
            {
                fprintf(stderr, "%s", "Scrape response was not in benc format\n");
            }
            else
            {
                auto len = size_t{};
                char* str = tr_variantToStr(&top, TR_VARIANT_FMT_JSON, &len);
                fprintf(stderr, "%s", "Scrape response:\n< ");

                for (size_t i = 0; i < len; ++i)
                {
                    fputc(str[i], stderr);
                }

                fputc('\n', stderr);
                tr_free(str);
            }
        }

        if (variant_loaded)
        {
            auto sv = std::string_view{};
            if (tr_variantDictFindStrView(&top, TR_KEY_failure_reason, &sv))
            {
                response->errmsg = sv;
            }

            tr_variant* flags = nullptr;
            auto intVal = int64_t{};
            if (tr_variantDictFindDict(&top, TR_KEY_flags, &flags) &&
                tr_variantDictFindInt(flags, TR_KEY_min_request_interval, &intVal))
            {
                response->min_request_interval = intVal;
            }

            tr_variant* files = nullptr;
            if (tr_variantDictFindDict(&top, TR_KEY_files, &files))
            {
                auto key = tr_quark{};
                tr_variant* val = nullptr;

                for (int i = 0; tr_variantDictChild(files, i, &key, &val); ++i)
                {
                    /* populate the corresponding row in our response array */
                    for (int j = 0; j < response->row_count; ++j)
                    {
                        struct tr_scrape_response_row* row = &response->rows[j];

                        // TODO(ckerr): ugh, interning info dict hashes is awful
                        auto const& hash = row->info_hash;
                        auto const key_sv = tr_quark_get_string_view(key);
                        if (std::size(hash) == std::size(key_sv) &&
                            memcmp(std::data(hash), std::data(key_sv), std::size(hash)) == 0)
                        {
                            if (tr_variantDictFindInt(val, TR_KEY_complete, &intVal))
                            {
                                row->seeders = intVal;
                            }

                            if (tr_variantDictFindInt(val, TR_KEY_incomplete, &intVal))
                            {
                                row->leechers = intVal;
                            }

                            if (tr_variantDictFindInt(val, TR_KEY_downloaded, &intVal))
                            {
                                row->downloads = intVal;
                            }

                            if (tr_variantDictFindInt(val, TR_KEY_downloaders, &intVal))
                            {
                                row->downloaders = intVal;
                            }

                            break;
                        }
                    }
                }
            }

            tr_variantFree(&top);
        }
    }

    tr_runInEventThread(session, on_scrape_done_eventthread, data);
}

static char* scrape_url_new(tr_scrape_request const* req)
{
    auto const sv = tr_quark_get_string_view(req->scrape_url);

    auto* const buf = evbuffer_new();
    evbuffer_add(buf, std::data(sv), std::size(sv));

    char delimiter = sv.find('?') == sv.npos ? '?' : '&';
    for (int i = 0; i < req->info_hash_count; ++i)
    {
        char str[SHA_DIGEST_LENGTH * 3 + 1];
        tr_http_escape_sha1(str, req->info_hash[i]);
        evbuffer_add_printf(buf, "%cinfo_hash=%s", delimiter, str);
        delimiter = '&';
    }

    return evbuffer_free_to_str(buf, nullptr);
}

void tr_tracker_http_scrape(
    tr_session* session,
    tr_scrape_request const* request,
    tr_scrape_response_func response_func,
    void* response_func_user_data)
{
    char* url = scrape_url_new(request);

    auto* d = new scrape_data{};
    d->response.scrape_url = request->scrape_url;
    d->response_func = response_func;
    d->response_func_user_data = response_func_user_data;
    d->response.row_count = request->info_hash_count;

    for (int i = 0; i < d->response.row_count; ++i)
    {
        d->response.rows[i].info_hash = request->info_hash[i];
        d->response.rows[i].seeders = -1;
        d->response.rows[i].leechers = -1;
        d->response.rows[i].downloads = -1;
    }

    tr_strlcpy(d->log_name, request->log_name, sizeof(d->log_name));

    dbgmsg(request->log_name, "Sending scrape to libcurl: \"%s\"", url);
    tr_webRun(session, url, on_scrape_done, d);

    tr_free(url);
}
