// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <climits> /* USHRT_MAX */
#include <cstdio> /* fprintf() */
#include <cstring> /* strchr(), memcmp(), memcpy() */
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include <event2/http.h> /* for HTTP_OK */

#include <fmt/core.h>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"

#include "announcer-common.h"
#include "benc.h"
#include "crypto-utils.h"
#include "error.h"
#include "log.h"
#include "net.h" /* tr_globalIPv6() */
#include "peer-mgr.h" /* pex */
#include "quark.h"
#include "torrent.h"
#include "utils.h"
#include "web-utils.h"
#include "web.h"

using namespace std::literals;

/****
*****
*****  ANNOUNCE
*****
****/

static char const* get_event_string(tr_announce_request const* req)
{
    return req->partial_seed && (req->event != TR_ANNOUNCE_EVENT_STOPPED) ? "paused" : tr_announce_event_get_string(req->event);
}

static tr_urlbuf announce_url_new(tr_session const* session, tr_announce_request const* req)
{
    auto url = tr_urlbuf{};
    auto out = std::back_inserter(url);

    auto escaped_info_hash = std::array<char, SHA_DIGEST_LENGTH * 3 + 1>{};
    tr_http_escape_sha1(std::data(escaped_info_hash), req->info_hash);

    fmt::format_to(
        out,
        "{url}"
        "{sep}info_hash={info_hash}"
        "&peer_id={peer_id}"
        "&port={port}"
        "&uploaded={uploaded}"
        "&downloaded={downloaded}"
        "&left={left}"
        "&numwant={numwant}"
        "&key={key}"
        "&compact=1"
        "&supportcrypto=1",
        fmt::arg("url", req->announce_url),
        fmt::arg("sep", tr_strvContains(req->announce_url.sv(), '?') ? '&' : '?'),
        fmt::arg("info_hash", std::data(escaped_info_hash)),
        fmt::arg("peer_id", std::string_view{ std::data(req->peer_id), std::size(req->peer_id) }),
        fmt::arg("port", req->port.host()),
        fmt::arg("uploaded", req->up),
        fmt::arg("downloaded", req->down),
        fmt::arg("left", req->leftUntilComplete),
        fmt::arg("numwant", req->numwant),
        fmt::arg("key", req->key));

    if (session->encryptionMode == TR_ENCRYPTION_REQUIRED)
    {
        fmt::format_to(out, "&requirecrypto=1");
    }

    if (req->corrupt != 0)
    {
        fmt::format_to(out, "&corrupt={}", req->corrupt);
    }

    if (char const* str = get_event_string(req); !tr_str_is_empty(str))
    {
        fmt::format_to(out, "&event={}", str);
    }

    if (!std::empty(req->tracker_id))
    {
        fmt::format_to(out, "&trackerid={}", req->tracker_id);
    }

    /* There are two incompatible techniques for announcing an IPv6 address.
       BEP-7 suggests adding an "ipv6=" parameter to the announce URL,
       while OpenTracker requires that peers announce twice, once over IPv4
       and once over IPv6.

       To be safe, we should do both: add the "ipv6=" parameter and
       announce twice. At any rate, we're already computing our IPv6
       address (for the LTEP handshake), so this comes for free. */

    if (auto const* const ipv6 = tr_globalIPv6(session); ipv6 != nullptr)
    {
        auto ipv6_readable = std::array<char, INET6_ADDRSTRLEN>{};
        evutil_inet_ntop(AF_INET6, ipv6, std::data(ipv6_readable), std::size(ipv6_readable));
        fmt::format_to(out, "&ipv6=");
        tr_http_escape(out, std::data(ipv6_readable), true);
    }

    return url;
}

static void verboseLog(std::string_view description, tr_direction direction, std::string_view message)
{
    auto& out = std::cerr;
    static bool const verbose = tr_env_key_exists("TR_CURL_VERBOSE");
    if (!verbose)
    {
        return;
    }

    auto const direction_sv = direction == TR_DOWN ? "<< "sv : ">> "sv;
    out << description << std::endl << "[raw]"sv << direction_sv;
    for (unsigned char ch : message)
    {
        if (isprint(ch) != 0)
        {
            out << ch;
        }
        else
        {
            out << R"(\x)" << std::hex << std::setw(2) << std::setfill('0') << unsigned(ch) << std::dec << std::setw(1)
                << std::setfill(' ');
        }
    }
    out << std::endl << "[b64]"sv << direction_sv << tr_base64_encode(message) << std::endl;
}

static auto constexpr MaxBencDepth = 8;

void tr_announcerParseHttpAnnounceResponse(tr_announce_response& response, std::string_view benc, char const* log_name)
{
    verboseLog("Announce response:", TR_DOWN, benc);

    struct AnnounceHandler final : public transmission::benc::BasicHandler<MaxBencDepth>
    {
        using BasicHandler = transmission::benc::BasicHandler<MaxBencDepth>;

        tr_announce_response& response_;
        std::string_view const log_name_;
        std::optional<size_t> row_;
        tr_pex pex_ = {};

        explicit AnnounceHandler(tr_announce_response& response, std::string_view log_name)
            : response_{ response }
            , log_name_{ log_name }
        {
        }

        bool StartDict(Context const& context) override
        {
            BasicHandler::StartDict(context);

            pex_ = {};

            return true;
        }

        bool EndDict(Context const& context) override
        {
            BasicHandler::EndDict(context);

            if (tr_address_is_valid_for_peers(&pex_.addr, pex_.port))
            {
                response_.pex.push_back(pex_);
                pex_ = {};
            }

            return true;
        }

        bool Int64(int64_t value, Context const& /*context*/) override
        {
            if (auto const key = currentKey(); key == "interval")
            {
                response_.interval = value;
            }
            else if (key == "min interval"sv)
            {
                response_.min_interval = value;
            }
            else if (key == "complete"sv)
            {
                response_.seeders = value;
            }
            else if (key == "incomplete"sv)
            {
                response_.leechers = value;
            }
            else if (key == "downloaded"sv)
            {
                response_.downloads = value;
            }
            else if (key == "port"sv)
            {
                pex_.port.setHost(static_cast<uint16_t>(value));
            }
            else
            {
                tr_logAddDebug(fmt::format("unexpected key '{}' int '{}'", key, value), log_name_);
            }

            return true;
        }

        bool String(std::string_view value, Context const& /*context*/) override
        {
            if (auto const key = currentKey(); key == "failure reason"sv)
            {
                response_.errmsg = value;
            }
            else if (key == "warning message"sv)
            {
                response_.warning = value;
            }
            else if (key == "tracker id"sv)
            {
                response_.tracker_id = value;
            }
            else if (key == "peers"sv)
            {
                response_.pex = tr_peerMgrCompactToPex(std::data(value), std::size(value), nullptr, 0);
            }
            else if (key == "peers6"sv)
            {
                response_.pex6 = tr_peerMgrCompact6ToPex(std::data(value), std::size(value), nullptr, 0);
            }
            else if (key == "ip")
            {
                tr_address_from_string(&pex_.addr, value);
            }
            else if (key == "peer id")
            {
                // unused
            }
            else if (key == "external ip"sv && std::size(value) == 4)
            {
                response_.external_ip = tr_address::from_4byte_ipv4(value);
            }
            else
            {
                tr_logAddDebug(fmt::format("unexpected key '{}' int '{}'", key, value), log_name_);
            }

            return true;
        }
    };

    auto stack = transmission::benc::ParserStack<MaxBencDepth>{};
    auto handler = AnnounceHandler{ response, log_name };
    tr_error* error = nullptr;
    transmission::benc::parse(benc, stack, handler, nullptr, &error);
    if (error != nullptr)
    {
        tr_logAddWarn(
            fmt::format(
                _("Couldn't parse announce response: {error} ({error_code})"),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)),
            log_name);
        tr_error_clear(&error);
    }
}

struct announce_data
{
    tr_announce_response response;
    tr_announce_response_func response_func;
    void* response_func_user_data;
    char log_name[128];
};

static void onAnnounceDone(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, vdata] = web_response;
    auto* data = static_cast<struct announce_data*>(vdata);

    tr_announce_response* const response = &data->response;
    response->did_connect = did_connect;
    response->did_timeout = did_timeout;
    tr_logAddTrace("Got announce response", data->log_name);

    if (status != HTTP_OK)
    {
        auto const* const response_str = tr_webGetResponseStr(status);
        response->errmsg = fmt::format(FMT_STRING("Tracker HTTP response {:d} ({:s}"), status, response_str);
    }
    else
    {
        tr_announcerParseHttpAnnounceResponse(*response, body, data->log_name);
    }

    if (!std::empty(response->pex6))
    {
        tr_logAddTrace(fmt::format("got a peers6 length of {}", std::size(response->pex6)), data->log_name);
    }

    if (!std::empty(response->pex))
    {
        tr_logAddTrace(fmt::format("got a peers length of {}", std::size(response->pex)), data->log_name);
    }

    if (data->response_func != nullptr)
    {
        data->response_func(&data->response, data->response_func_user_data);
    }

    delete data;
}

void tr_tracker_http_announce(
    tr_session* session,
    tr_announce_request const* request,
    tr_announce_response_func response_func,
    void* response_func_user_data)
{
    auto* const d = new announce_data();
    d->response_func = response_func;
    d->response_func_user_data = response_func_user_data;
    d->response.info_hash = request->info_hash;
    tr_strlcpy(d->log_name, request->log_name, sizeof(d->log_name));

    auto const url = announce_url_new(session, request);
    tr_logAddTrace(fmt::format("Sending announce to libcurl: '{}'", url), request->log_name);

    auto options = tr_web::FetchOptions{ url.sv(), onAnnounceDone, d };
    options.timeout_secs = 90L;
    options.sndbuf = 1024;
    options.rcvbuf = 3072;
    session->web->fetch(std::move(options));
}

/****
*****
*****  SCRAPE
*****
****/

void tr_announcerParseHttpScrapeResponse(tr_scrape_response& response, std::string_view benc, char const* log_name)
{
    verboseLog("Scrape response:", TR_DOWN, benc);

    struct ScrapeHandler final : public transmission::benc::BasicHandler<MaxBencDepth>
    {
        using BasicHandler = transmission::benc::BasicHandler<MaxBencDepth>;

        tr_scrape_response& response_;
        std::string_view const log_name_;
        std::optional<size_t> row_;

        explicit ScrapeHandler(tr_scrape_response& response, std::string_view const log_name)
            : response_{ response }
            , log_name_{ log_name }
        {
        }

        bool Key(std::string_view value, Context const& context) override
        {
            BasicHandler::Key(value, context);

            if (auto needle = tr_sha1_digest_t{}; depth() == 2 && key(1) == "files"sv && std::size(value) == std::size(needle))
            {
                std::copy_n(reinterpret_cast<std::byte const*>(std::data(value)), std::size(value), std::data(needle));
                auto const it = std::find_if(
                    std::begin(response_.rows),
                    std::end(response_.rows),
                    [needle](auto const& row) { return row.info_hash == needle; });

                if (it == std::end(response_.rows))
                {
                    row_.reset();
                }
                else
                {
                    row_ = std::distance(std::begin(response_.rows), it);
                }
            }

            return true;
        }

        bool Int64(int64_t value, Context const& /*context*/) override
        {
            if (auto const key = currentKey(); row_ && key == "complete"sv)
            {
                response_.rows[*row_].seeders = value;
            }
            else if (row_ && key == "downloaded"sv)
            {
                response_.rows[*row_].downloads = value;
            }
            else if (row_ && key == "incomplete"sv)
            {
                response_.rows[*row_].leechers = value;
            }
            else if (row_ && key == "downloaders"sv)
            {
                response_.rows[*row_].downloaders = value;
            }
            else if (key == "min_request_interval"sv)
            {
                response_.min_request_interval = value;
            }
            else
            {
                tr_logAddDebug(fmt::format("unexpected key '{}' int '{}'", key, value), log_name_);
            }

            return true;
        }

        bool String(std::string_view value, Context const& /*context*/) override
        {
            if (auto const key = currentKey(); depth() == 1 && key == "failure reason"sv)
            {
                response_.errmsg = value;
            }
            else
            {
                tr_logAddDebug(fmt::format("unexpected key '{}' str '{}'", key, value), log_name_);
            }

            return true;
        }
    };

    auto stack = transmission::benc::ParserStack<MaxBencDepth>{};
    auto handler = ScrapeHandler{ response, log_name };
    tr_error* error = nullptr;
    transmission::benc::parse(benc, stack, handler, nullptr, &error);
    if (error != nullptr)
    {
        tr_logAddWarn(
            fmt::format(
                _("Couldn't parse scrape response: {error} ({error_code})"),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)),
            log_name);
        tr_error_clear(&error);
    }
}

struct scrape_data
{
    tr_scrape_response response;
    tr_scrape_response_func response_func;
    void* response_func_user_data;
    char log_name[128];
};

static void onScrapeDone(tr_web::FetchResponse const& web_response)
{
    auto const& [status, body, did_connect, did_timeout, vdata] = web_response;
    auto* const data = static_cast<struct scrape_data*>(vdata);

    tr_scrape_response& response = data->response;
    response.did_connect = did_connect;
    response.did_timeout = did_timeout;

    auto const scrape_url_sv = response.scrape_url.sv();
    tr_logAddTrace(fmt::format("Got scrape response for '{}'", scrape_url_sv), data->log_name);

    if (status != HTTP_OK)
    {
        auto const* const response_str = tr_webGetResponseStr(status);
        response.errmsg = fmt::format(FMT_STRING("Tracker HTTP response {:d} ({:s})"), status, response_str);
    }
    else if (!std::empty(body))
    {
        tr_announcerParseHttpScrapeResponse(response, body, data->log_name);
    }

    if (data->response_func != nullptr)
    {
        data->response_func(&data->response, data->response_func_user_data);
    }

    delete data;
}

static auto scrape_url_new(tr_scrape_request const* req)
{
    auto const sv = req->scrape_url.sv();
    char delimiter = tr_strvContains(sv, '?') ? '&' : '?';

    auto scrape_url = tr_pathbuf{ sv };

    for (int i = 0; i < req->info_hash_count; ++i)
    {
        char str[SHA_DIGEST_LENGTH * 3 + 1];
        tr_http_escape_sha1(str, req->info_hash[i]);
        scrape_url.append(delimiter, "info_hash=", str);
        delimiter = '&';
    }

    return scrape_url;
}

void tr_tracker_http_scrape(
    tr_session* session,
    tr_scrape_request const* request,
    tr_scrape_response_func response_func,
    void* response_func_user_data)
{
    auto* d = new scrape_data();
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

    auto const url = scrape_url_new(request);
    tr_logAddTrace(fmt::format("Sending scrape to libcurl: '{}'", url), request->log_name);

    auto options = tr_web::FetchOptions{ url, onScrapeDone, d };
    options.timeout_secs = 30L;
    options.sndbuf = 4096;
    options.rcvbuf = 4096;
    session->web->fetch(std::move(options));
}
