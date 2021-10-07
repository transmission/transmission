/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cerrno>
#include <cstring> /* memcpy */
#include <list>
#include <string>

#include <zlib.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h> /* TODO: eventually remove this */

#include "transmission.h"
#include "crypto.h" /* tr_ssha1_matches() */
#include "crypto-utils.h" /* tr_rand_buffer() */
#include "error.h"
#include "fdlimit.h"
#include "log.h"
#include "net.h"
#include "platform.h" /* tr_getWebClientDir() */
#include "rpcimpl.h"
#include "rpc-server.h"
#include "session.h"
#include "session-id.h"
#include "tr-assert.h"
#include "trevent.h"
#include "utils.h"
#include "variant.h"
#include "web.h"

/* session-id is used to make cross-site request forgery attacks difficult.
 * Don't disable this feature unless you really know what you're doing!
 * http://en.wikipedia.org/wiki/Cross-site_request_forgery
 * http://shiflett.org/articles/cross-site-request-forgeries
 * http://www.webappsec.org/lists/websecurity/archive/2008-04/msg00037.html */
#define REQUIRE_SESSION_ID

#define MY_NAME "RPC Server"
#define MY_REALM "Transmission"

struct tr_rpc_server
{
    bool isEnabled;
    bool isPasswordEnabled;
    bool isWhitelistEnabled;
    bool isHostWhitelistEnabled;
    tr_port port;
    char* url;
    struct tr_address bindAddress;
    struct evhttp* httpd;
    struct event* start_retry_timer;
    int start_retry_counter;
    tr_session* session;
    char* username;
    char* password;
    std::string whitelistStr;
    std::list<std::string> whitelist;
    std::list<std::string> hostWhitelist;
    int loginattempts;
    bool isAntiBruteForceEnabled;
    int antiBruteForceThreshold;

    bool isStreamInitialized;
    z_stream stream;
};

#define dbgmsg(...) tr_logAddDeepNamed(MY_NAME, __VA_ARGS__)

/***
****
***/

static char const* get_current_session_id(struct tr_rpc_server* server)
{
    return tr_session_id_get_current(server->session->session_id);
}

/**
***
**/

static void send_simple_response(struct evhttp_request* req, int code, char const* text)
{
    char const* code_text = tr_webGetResponseStr(code);
    struct evbuffer* body = evbuffer_new();

    evbuffer_add_printf(body, "<h1>%d: %s</h1>", code, code_text);

    if (text != nullptr)
    {
        evbuffer_add_printf(body, "%s", text);
    }

    evhttp_send_reply(req, code, code_text, body);

    evbuffer_free(body);
}

struct tr_mimepart
{
    std::string headers;
    std::string body;
};

static auto extract_parts_from_multipart(struct evkeyvalq const* headers, struct evbuffer* body)
{
    auto ret = std::vector<tr_mimepart>{};

    char const* content_type = evhttp_find_header(headers, "Content-Type");
    char const* in = (char const*)evbuffer_pullup(body, -1);
    size_t inlen = evbuffer_get_length(body);

    char const* boundary_key = "boundary=";
    char const* boundary_key_begin = content_type != nullptr ? strstr(content_type, boundary_key) : nullptr;
    char const* boundary_val = boundary_key_begin != nullptr ? boundary_key_begin + strlen(boundary_key) : "arglebargle";
    char* boundary = tr_strdup_printf("--%s", boundary_val);
    size_t const boundary_len = strlen(boundary);

    char const* delim = tr_memmem(in, inlen, boundary, boundary_len);

    while (delim != nullptr)
    {
        size_t part_len;
        char const* part = delim + boundary_len;

        inlen -= part - in;
        in = part;

        delim = tr_memmem(in, inlen, boundary, boundary_len);
        part_len = delim != nullptr ? (size_t)(delim - part) : inlen;

        if (part_len != 0)
        {
            char const* rnrn = tr_memmem(part, part_len, "\r\n\r\n", 4);

            if (rnrn != nullptr)
            {
                auto tmp = tr_mimepart{};
                tmp.headers.assign(part, rnrn - part);
                tmp.body.assign(rnrn + 4, (part + part_len) - (rnrn + 4));
                ret.push_back(tmp);
            }
        }
    }

    tr_free(boundary);

    return ret;
}

static void handle_upload(struct evhttp_request* req, struct tr_rpc_server* server)
{
    if (req->type != EVHTTP_REQ_POST)
    {
        send_simple_response(req, 405, nullptr);
    }
    else
    {
        bool hasSessionId = false;

        char const* query = strchr(req->uri, '?');
        bool const paused = query != nullptr && strstr(query + 1, "paused=true") != nullptr;

        auto const parts = extract_parts_from_multipart(req->input_headers, req->input_buffer);

        /* first look for the session id */
        for (auto const& p : parts)
        {
            if (tr_strcasestr(p.headers.c_str(), TR_RPC_SESSION_ID_HEADER) != nullptr)
            {
                char const* ours = get_current_session_id(server);
                size_t const ourlen = strlen(ours);
                hasSessionId = ourlen <= std::size(p.body) && memcmp(p.body.c_str(), ours, ourlen) == 0;
                break;
            }
        }

        if (!hasSessionId)
        {
            int code = 409;
            char const* codetext = tr_webGetResponseStr(code);
            struct evbuffer* body = evbuffer_new();
            evbuffer_add_printf(body, "%s", "{ \"success\": false, \"msg\": \"Bad Session-Id\" }");
            evhttp_send_reply(req, code, codetext, body);
            evbuffer_free(body);
        }
        else
        {
            for (auto const& p : parts)
            {
                auto const& body = p.body;
                size_t body_len = std::size(body);
                tr_variant top;
                tr_variant* args;
                tr_variant test;
                bool have_source = false;

                if (body_len >= 2 && memcmp(&body[body_len - 2], "\r\n", 2) == 0)
                {
                    body_len -= 2;
                }

                tr_variantInitDict(&top, 2);
                tr_variantDictAddStr(&top, TR_KEY_method, "torrent-add");
                args = tr_variantDictAddDict(&top, TR_KEY_arguments, 2);
                tr_variantDictAddBool(args, TR_KEY_paused, paused);

                if (tr_urlIsValid(body.c_str(), body_len))
                {
                    tr_variantDictAddRaw(args, TR_KEY_filename, body.c_str(), body_len);
                    have_source = true;
                }
                else if (tr_variantFromBenc(&test, body.c_str(), body_len) == 0)
                {
                    auto* b64 = static_cast<char*>(tr_base64_encode(body.c_str(), body_len, nullptr));
                    tr_variantDictAddStr(args, TR_KEY_metainfo, b64);
                    tr_free(b64);
                    have_source = true;
                }

                if (have_source)
                {
                    tr_rpc_request_exec_json(server->session, &top, nullptr, nullptr);
                }

                tr_variantFree(&top);
            }
        }

        /* send "success" response */
        {
            int code = HTTP_OK;
            char const* codetext = tr_webGetResponseStr(code);
            struct evbuffer* body = evbuffer_new();
            evbuffer_add_printf(body, "%s", "{ \"success\": true, \"msg\": \"Torrent Added\" }");
            evhttp_send_reply(req, code, codetext, body);
            evbuffer_free(body);
        }
    }
}

/***
****
***/

static char const* mimetype_guess(char const* path)
{
    struct
    {
        char const* suffix;
        char const* mime_type;
    } const types[] = {
        /* these are the ones we need for serving the web client's files... */
        { "css", "text/css" },
        { "gif", "image/gif" },
        { "html", "text/html" },
        { "ico", "image/vnd.microsoft.icon" },
        { "js", "application/javascript" },
        { "png", "image/png" },
        { "svg", "image/svg+xml" },
    };
    char const* dot = strrchr(path, '.');

    for (unsigned int i = 0; dot != nullptr && i < TR_N_ELEMENTS(types); ++i)
    {
        if (strcmp(dot + 1, types[i].suffix) == 0)
        {
            return types[i].mime_type;
        }
    }

    return "application/octet-stream";
}

static void add_response(
    struct evhttp_request* req,
    struct tr_rpc_server* server,
    struct evbuffer* out,
    struct evbuffer* content)
{
    char const* key = "Accept-Encoding";
    char const* encoding = evhttp_find_header(req->input_headers, key);
    bool const do_compress = encoding != nullptr && strstr(encoding, "gzip") != nullptr;

    if (!do_compress)
    {
        evbuffer_add_buffer(out, content);
    }
    else
    {
        int state;
        struct evbuffer_iovec iovec[1];
        void* content_ptr = evbuffer_pullup(content, -1);
        size_t const content_len = evbuffer_get_length(content);

        if (!server->isStreamInitialized)
        {
            int compressionLevel;

            server->isStreamInitialized = true;
            server->stream.zalloc = (alloc_func)Z_NULL;
            server->stream.zfree = (free_func)Z_NULL;
            server->stream.opaque = (voidpf)Z_NULL;

            /* zlib's manual says: "Add 16 to windowBits to write a simple gzip header
             * and trailer around the compressed data instead of a zlib wrapper." */
#ifdef TR_LIGHTWEIGHT
            compressionLevel = Z_DEFAULT_COMPRESSION;
#else
            compressionLevel = Z_BEST_COMPRESSION;
#endif
            deflateInit2(&server->stream, compressionLevel, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
        }

        server->stream.next_in = static_cast<Bytef*>(content_ptr);
        server->stream.avail_in = content_len;

        /* allocate space for the raw data and call deflate() just once --
         * we won't use the deflated data if it's longer than the raw data,
         * so it's okay to let deflate() run out of output buffer space */
        evbuffer_reserve_space(out, content_len, iovec, 1);
        server->stream.next_out = static_cast<Bytef*>(iovec[0].iov_base);
        server->stream.avail_out = iovec[0].iov_len;
        state = deflate(&server->stream, Z_FINISH);

        if (state == Z_STREAM_END)
        {
            iovec[0].iov_len -= server->stream.avail_out;

#if 0

            fprintf(stderr, "compressed response is %.2f of original (raw==%zu bytes; compressed==%zu)\n",
                (double)evbuffer_get_length(out) / content_len, content_len, evbuffer_get_length(out));

#endif

            evhttp_add_header(req->output_headers, "Content-Encoding", "gzip");
        }
        else
        {
            memcpy(iovec[0].iov_base, content_ptr, content_len);
            iovec[0].iov_len = content_len;
        }

        evbuffer_commit_space(out, iovec, 1);
        deflateReset(&server->stream);
    }
}

static void add_time_header(struct evkeyvalq* headers, char const* key, time_t value)
{
    char buf[128];
    struct tm tm;
    /* According to RFC 2616 this must follow RFC 1123's date format,
       so use gmtime instead of localtime... */
    tr_gmtime_r(&value, &tm);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    evhttp_add_header(headers, key, buf);
}

static void evbuffer_ref_cleanup_tr_free(void const* data, size_t datalen, void* extra)
{
    TR_UNUSED(data);
    TR_UNUSED(datalen);

    tr_free(extra);
}

static void serve_file(struct evhttp_request* req, struct tr_rpc_server* server, char const* filename)
{
    if (req->type != EVHTTP_REQ_GET)
    {
        evhttp_add_header(req->output_headers, "Allow", "GET");
        send_simple_response(req, 405, nullptr);
    }
    else
    {
        void* file;
        size_t file_len;
        tr_error* error = nullptr;

        file_len = 0;
        file = tr_loadFile(filename, &file_len, &error);

        if (file == nullptr)
        {
            char* tmp = tr_strdup_printf("%s (%s)", filename, error->message);
            send_simple_response(req, HTTP_NOTFOUND, tmp);
            tr_free(tmp);
            tr_error_free(error);
        }
        else
        {
            struct evbuffer* content;
            struct evbuffer* out;
            time_t const now = tr_time();

            content = evbuffer_new();
            evbuffer_add_reference(content, file, file_len, evbuffer_ref_cleanup_tr_free, file);

            out = evbuffer_new();
            evhttp_add_header(req->output_headers, "Content-Type", mimetype_guess(filename));
            add_time_header(req->output_headers, "Date", now);
            add_time_header(req->output_headers, "Expires", now + (24 * 60 * 60));
            add_response(req, server, out, content);
            evhttp_send_reply(req, HTTP_OK, "OK", out);

            evbuffer_free(out);
            evbuffer_free(content);
        }
    }
}

static void handle_web_client(struct evhttp_request* req, struct tr_rpc_server* server)
{
    char const* webClientDir = tr_getWebClientDir(server->session);

    if (tr_str_is_empty(webClientDir))
    {
        send_simple_response(
            req,
            HTTP_NOTFOUND,
            "<p>Couldn't find Transmission's web interface files!</p>"
            "<p>Users: to tell Transmission where to look, "
            "set the TRANSMISSION_WEB_HOME environment "
            "variable to the folder where the web interface's "
            "index.html is located.</p>"
            "<p>Package Builders: to set a custom default at compile time, "
            "#define PACKAGE_DATA_DIR in libtransmission/platform.c "
            "or tweak tr_getClutchDir() by hand.</p>");
    }
    else
    {
        char* pch;
        char* subpath;

        subpath = tr_strdup(req->uri + strlen(server->url) + 4);

        if ((pch = strchr(subpath, '?')) != nullptr)
        {
            *pch = '\0';
        }

        if (strstr(subpath, "..") != nullptr)
        {
            send_simple_response(req, HTTP_NOTFOUND, "<p>Tsk, tsk.</p>");
        }
        else
        {
            char* filename = tr_strdup_printf(
                "%s%s%s",
                webClientDir,
                TR_PATH_DELIMITER_STR,
                tr_str_is_empty(subpath) ? "index.html" : subpath);
            serve_file(req, server, filename);
            tr_free(filename);
        }

        tr_free(subpath);
    }
}

struct rpc_response_data
{
    struct evhttp_request* req;
    struct tr_rpc_server* server;
};

static void rpc_response_func(tr_session* session, tr_variant* response, void* user_data)
{
    TR_UNUSED(session);

    auto* data = static_cast<struct rpc_response_data*>(user_data);
    struct evbuffer* response_buf = tr_variantToBuf(response, TR_VARIANT_FMT_JSON_LEAN);
    struct evbuffer* buf = evbuffer_new();

    add_response(data->req, data->server, buf, response_buf);
    evhttp_add_header(data->req->output_headers, "Content-Type", "application/json; charset=UTF-8");
    evhttp_send_reply(data->req, HTTP_OK, "OK", buf);

    evbuffer_free(buf);
    evbuffer_free(response_buf);
    tr_free(data);
}

static void handle_rpc_from_json(struct evhttp_request* req, struct tr_rpc_server* server, char const* json, size_t json_len)
{
    tr_variant top;
    bool have_content = tr_variantFromJson(&top, json, json_len) == 0;
    struct rpc_response_data* data;

    data = tr_new0(struct rpc_response_data, 1);
    data->req = req;
    data->server = server;

    tr_rpc_request_exec_json(server->session, have_content ? &top : nullptr, rpc_response_func, data);

    if (have_content)
    {
        tr_variantFree(&top);
    }
}

static void handle_rpc(struct evhttp_request* req, struct tr_rpc_server* server)
{
    if (req->type == EVHTTP_REQ_POST)
    {
        handle_rpc_from_json(
            req,
            server,
            (char const*)evbuffer_pullup(req->input_buffer, -1),
            evbuffer_get_length(req->input_buffer));
        return;
    }

    if (req->type == EVHTTP_REQ_GET)
    {
        char const* q = strchr(req->uri, '?');

        if (q != nullptr)
        {
            struct rpc_response_data* data = tr_new0(struct rpc_response_data, 1);
            data->req = req;
            data->server = server;
            tr_rpc_request_exec_uri(server->session, q + 1, TR_BAD_SIZE, rpc_response_func, data);
            return;
        }
    }

    send_simple_response(req, 405, nullptr);
}

static bool isAddressAllowed(tr_rpc_server const* server, char const* address)
{
    auto const& src = server->whitelist;

    return !server->isWhitelistEnabled ||
        std::any_of(std::begin(src), std::end(src), [&address](auto const& s) { return tr_wildmat(address, s.c_str()); });
}

static bool isIPAddressWithOptionalPort(char const* host)
{
    struct sockaddr_storage address;
    int address_len = sizeof(address);

    /* TODO: move to net.{c,h} */
    return evutil_parse_sockaddr_port(host, (struct sockaddr*)&address, &address_len) != -1;
}

static bool isHostnameAllowed(tr_rpc_server const* server, struct evhttp_request* req)
{
    /* If password auth is enabled, any hostname is permitted. */
    if (server->isPasswordEnabled)
    {
        return true;
    }

    /* If whitelist is disabled, no restrictions. */
    if (!server->isHostWhitelistEnabled)
    {
        return true;
    }

    char const* const host = evhttp_find_header(req->input_headers, "Host");

    /* No host header, invalid request. */
    if (host == nullptr)
    {
        return false;
    }

    /* IP address is always acceptable. */
    if (isIPAddressWithOptionalPort(host))
    {
        return true;
    }

    /* Host header might include the port. */
    auto const hostname = std::string(host, strcspn(host, ":"));

    /* localhost is always acceptable. */
    if (hostname == "localhost" || hostname == "localhost.")
    {
        return true;
    }

    auto const& src = server->hostWhitelist;
    return std::any_of(
        std::begin(src),
        std::end(src),
        [&hostname](auto const& str) { return tr_wildmat(hostname.c_str(), str.c_str()); });
}

static bool test_session_id(struct tr_rpc_server* server, struct evhttp_request* req)
{
    char const* ours = get_current_session_id(server);
    char const* theirs = evhttp_find_header(req->input_headers, TR_RPC_SESSION_ID_HEADER);
    bool const success = theirs != nullptr && strcmp(theirs, ours) == 0;
    return success;
}

static void handle_request(struct evhttp_request* req, void* arg)
{
    auto* server = static_cast<struct tr_rpc_server*>(arg);

    if (req != nullptr && req->evcon != nullptr)
    {
        char const* auth;
        char* user = nullptr;
        char* pass = nullptr;

        evhttp_add_header(req->output_headers, "Server", MY_REALM);

        if (server->isAntiBruteForceEnabled && server->loginattempts >= server->antiBruteForceThreshold)
        {
            send_simple_response(req, 403, "<p>Too many unsuccessful login attempts. Please restart transmission-daemon.</p>");
            return;
        }

        if (!isAddressAllowed(server, req->remote_host))
        {
            send_simple_response(
                req,
                403,
                "<p>Unauthorized IP Address.</p>"
                "<p>Either disable the IP address whitelist or add your address to it.</p>"
                "<p>If you're editing settings.json, see the 'rpc-whitelist' and 'rpc-whitelist-enabled' entries.</p>"
                "<p>If you're still using ACLs, use a whitelist instead. See the transmission-daemon manpage for details.</p>");
            return;
        }

        auth = evhttp_find_header(req->input_headers, "Authorization");

        if (auth != nullptr && evutil_ascii_strncasecmp(auth, "basic ", 6) == 0)
        {
            auto* p = static_cast<char*>(tr_base64_decode_str(auth + 6, nullptr));

            if (p != nullptr)
            {
                if ((pass = strchr(p, ':')) != nullptr)
                {
                    user = p;
                    *pass++ = '\0';
                }
                else
                {
                    tr_free(p);
                }
            }
        }

        if (server->isPasswordEnabled &&
            (pass == nullptr || user == nullptr || strcmp(server->username, user) != 0 ||
             !tr_ssha1_matches(server->password, pass)))
        {
            evhttp_add_header(req->output_headers, "WWW-Authenticate", "Basic realm=\"" MY_REALM "\"");
            if (server->isAntiBruteForceEnabled)
            {
                server->loginattempts++;
            }

            char* unauthuser = tr_strdup_printf(
                "<p>Unauthorized User. %d unsuccessful login attempts.</p>",
                server->loginattempts);
            send_simple_response(req, 401, unauthuser);
            tr_free(unauthuser);
            tr_free(user);
            return;
        }

        server->loginattempts = 0;

        if (strncmp(req->uri, server->url, strlen(server->url)) != 0)
        {
            char* location = tr_strdup_printf("%sweb/", server->url);
            evhttp_add_header(req->output_headers, "Location", location);
            send_simple_response(req, HTTP_MOVEPERM, nullptr);
            tr_free(location);
        }
        else if (strncmp(req->uri + strlen(server->url), "web/", 4) == 0)
        {
            handle_web_client(req, server);
        }
        else if (strcmp(req->uri + strlen(server->url), "upload") == 0)
        {
            handle_upload(req, server);
        }
        else if (!isHostnameAllowed(server, req))
        {
            char* const tmp = tr_strdup_printf(
                "<p>Transmission received your request, but the hostname was unrecognized.</p>"
                "<p>To fix this, choose one of the following options:"
                "<ul>"
                "<li>Enable password authentication, then any hostname is allowed.</li>"
                "<li>Add the hostname you want to use to the whitelist in settings.</li>"
                "</ul></p>"
                "<p>If you're editing settings.json, see the 'rpc-host-whitelist' and 'rpc-host-whitelist-enabled' entries.</p>"
                "<p>This requirement has been added to help prevent "
                "<a href=\"https://en.wikipedia.org/wiki/DNS_rebinding\">DNS Rebinding</a> "
                "attacks.</p>");
            send_simple_response(req, 421, tmp);
            tr_free(tmp);
        }

#ifdef REQUIRE_SESSION_ID

        else if (!test_session_id(server, req))
        {
            char const* sessionId = get_current_session_id(server);
            char* tmp = tr_strdup_printf(
                "<p>Your request had an invalid session-id header.</p>"
                "<p>To fix this, follow these steps:"
                "<ol><li> When reading a response, get its X-Transmission-Session-Id header and remember it"
                "<li> Add the updated header to your outgoing requests"
                "<li> When you get this 409 error message, resend your request with the updated header"
                "</ol></p>"
                "<p>This requirement has been added to help prevent "
                "<a href=\"https://en.wikipedia.org/wiki/Cross-site_request_forgery\">CSRF</a> "
                "attacks.</p>"
                "<p><code>%s: %s</code></p>",
                TR_RPC_SESSION_ID_HEADER,
                sessionId);
            evhttp_add_header(req->output_headers, TR_RPC_SESSION_ID_HEADER, sessionId);
            send_simple_response(req, 409, tmp);
            tr_free(tmp);
        }

#endif

        else if (strncmp(req->uri + strlen(server->url), "rpc", 3) == 0)
        {
            handle_rpc(req, server);
        }
        else
        {
            send_simple_response(req, HTTP_NOTFOUND, req->uri);
        }

        tr_free(user);
    }
}

enum
{
    SERVER_START_RETRY_COUNT = 10,
    SERVER_START_RETRY_DELAY_STEP = 3,
    SERVER_START_RETRY_DELAY_INCREMENT = 5,
    SERVER_START_RETRY_MAX_DELAY = 60
};

static void startServer(void* vserver);

static void rpc_server_on_start_retry(evutil_socket_t fd, short type, void* context)
{
    TR_UNUSED(fd);
    TR_UNUSED(type);

    startServer(context);
}

static int rpc_server_start_retry(tr_rpc_server* server)
{
    int retry_delay = (server->start_retry_counter / SERVER_START_RETRY_DELAY_STEP + 1) * SERVER_START_RETRY_DELAY_INCREMENT;
    retry_delay = std::min(retry_delay, int{ SERVER_START_RETRY_MAX_DELAY });

    if (server->start_retry_timer == nullptr)
    {
        server->start_retry_timer = evtimer_new(server->session->event_base, rpc_server_on_start_retry, server);
    }

    tr_timerAdd(server->start_retry_timer, retry_delay, 0);
    ++server->start_retry_counter;

    return retry_delay;
}

static void rpc_server_start_retry_cancel(tr_rpc_server* server)
{
    if (server->start_retry_timer != nullptr)
    {
        event_free(server->start_retry_timer);
        server->start_retry_timer = nullptr;
    }

    server->start_retry_counter = 0;
}

static void startServer(void* vserver)
{
    auto* server = static_cast<tr_rpc_server*>(vserver);

    if (server->httpd != nullptr)
    {
        return;
    }

    struct evhttp* httpd = evhttp_new(server->session->event_base);

    char const* address = tr_rpcGetBindAddress(server);

    tr_port const port = server->port;

    if (evhttp_bind_socket(httpd, address, port) == -1)
    {
        evhttp_free(httpd);

        if (server->start_retry_counter < SERVER_START_RETRY_COUNT)
        {
            int const retry_delay = rpc_server_start_retry(server);

            tr_logAddNamedDbg(MY_NAME, "Unable to bind to %s:%d, retrying in %d seconds", address, port, retry_delay);
            return;
        }

        tr_logAddNamedError(
            MY_NAME,
            "Unable to bind to %s:%d after %d attempts, giving up",
            address,
            port,
            SERVER_START_RETRY_COUNT);
    }
    else
    {
        evhttp_set_gencb(httpd, handle_request, server);
        server->httpd = httpd;

        tr_logAddNamedDbg(MY_NAME, "Started listening on %s:%d", address, port);
    }

    rpc_server_start_retry_cancel(server);
}

static void stopServer(tr_rpc_server* server)
{
    rpc_server_start_retry_cancel(server);

    struct evhttp* httpd = server->httpd;

    if (httpd == nullptr)
    {
        return;
    }

    char const* address = tr_rpcGetBindAddress(server);
    int const port = server->port;

    server->httpd = nullptr;
    evhttp_free(httpd);

    tr_logAddNamedDbg(MY_NAME, "Stopped listening on %s:%d", address, port);
}

static void onEnabledChanged(void* vserver)
{
    auto* server = static_cast<tr_rpc_server*>(vserver);

    if (!server->isEnabled)
    {
        stopServer(server);
    }
    else
    {
        startServer(server);
    }
}

void tr_rpcSetEnabled(tr_rpc_server* server, bool isEnabled)
{
    server->isEnabled = isEnabled;

    tr_runInEventThread(server->session, onEnabledChanged, server);
}

bool tr_rpcIsEnabled(tr_rpc_server const* server)
{
    return server->isEnabled;
}

static void restartServer(void* vserver)
{
    auto* server = static_cast<tr_rpc_server*>(vserver);

    if (server->isEnabled)
    {
        stopServer(server);
        startServer(server);
    }
}

void tr_rpcSetPort(tr_rpc_server* server, tr_port port)
{
    TR_ASSERT(server != nullptr);

    if (server->port != port)
    {
        server->port = port;

        if (server->isEnabled)
        {
            tr_runInEventThread(server->session, restartServer, server);
        }
    }
}

tr_port tr_rpcGetPort(tr_rpc_server const* server)
{
    return server->port;
}

void tr_rpcSetUrl(tr_rpc_server* server, char const* url)
{
    char* tmp = server->url;
    server->url = tr_strdup(url);
    dbgmsg("setting our URL to [%s]", server->url);
    tr_free(tmp);
}

char const* tr_rpcGetUrl(tr_rpc_server const* server)
{
    return server->url != nullptr ? server->url : "";
}

static auto parseWhitelist(char const* whitelistStr)
{
    auto list = std::list<std::string>{};

    /* build the new whitelist entries */
    for (char const* walk = whitelistStr; !tr_str_is_empty(walk);)
    {
        char const* delimiters = " ,;";
        size_t const len = strcspn(walk, delimiters);
        list.emplace_back(walk, len);
        auto const token = list.back().c_str();

        if (strcspn(token, "+-") < len)
        {
            tr_logAddNamedInfo(
                MY_NAME,
                "Adding address to whitelist: %s (And it has a '+' or '-'!  Are you using an old ACL by mistake?)",
                token);
        }
        else
        {
            tr_logAddNamedInfo(MY_NAME, "Adding address to whitelist: %s", token);
        }

        walk += len;

        if (*walk == '\0')
        {
            break;
        }

        ++walk;
    }

    return list;
}

void tr_rpcSetHostWhitelist(tr_rpc_server* server, char const* str)
{
    server->hostWhitelist = parseWhitelist(str);
}

void tr_rpcSetWhitelist(tr_rpc_server* server, char const* str)
{
    server->whitelistStr = str ? str : "";
    server->whitelist = parseWhitelist(str);
}

char const* tr_rpcGetWhitelist(tr_rpc_server const* server)
{
    return server->whitelistStr.c_str();
}

void tr_rpcSetWhitelistEnabled(tr_rpc_server* server, bool isEnabled)
{
    server->isWhitelistEnabled = isEnabled;
}

bool tr_rpcGetWhitelistEnabled(tr_rpc_server const* server)
{
    return server->isWhitelistEnabled;
}

void tr_rpcSetHostWhitelistEnabled(tr_rpc_server* server, bool isEnabled)
{
    server->isHostWhitelistEnabled = isEnabled;
}

/****
*****  PASSWORD
****/

void tr_rpcSetUsername(tr_rpc_server* server, char const* username)
{
    char* tmp = server->username;
    server->username = tr_strdup(username);
    dbgmsg("setting our Username to [%s]", server->username);
    tr_free(tmp);
}

char const* tr_rpcGetUsername(tr_rpc_server const* server)
{
    return server->username != nullptr ? server->username : "";
}

void tr_rpcSetPassword(tr_rpc_server* server, char const* password)
{
    tr_free(server->password);

    if (*password != '{')
    {
        server->password = tr_ssha1(password);
    }
    else
    {
        server->password = strdup(password);
    }

    dbgmsg("setting our Password to [%s]", server->password);
}

char const* tr_rpcGetPassword(tr_rpc_server const* server)
{
    return server->password != nullptr ? server->password : "";
}

void tr_rpcSetPasswordEnabled(tr_rpc_server* server, bool isEnabled)
{
    server->isPasswordEnabled = isEnabled;
    dbgmsg("setting 'password enabled' to %d", (int)isEnabled);
}

bool tr_rpcIsPasswordEnabled(tr_rpc_server const* server)
{
    return server->isPasswordEnabled;
}

char const* tr_rpcGetBindAddress(tr_rpc_server const* server)
{
    return tr_address_to_string(&server->bindAddress);
}

bool tr_rpcGetAntiBruteForceEnabled(tr_rpc_server const* server)
{
    return server->isAntiBruteForceEnabled;
}

void tr_rpcSetAntiBruteForceEnabled(tr_rpc_server* server, bool isEnabled)
{
    server->isAntiBruteForceEnabled = isEnabled;
    if (!isEnabled)
    {
        server->loginattempts = 0;
    }
}

int tr_rpcGetAntiBruteForceThreshold(tr_rpc_server const* server)
{
    return server->antiBruteForceThreshold;
}

void tr_rpcSetAntiBruteForceThreshold(tr_rpc_server* server, int badRequests)
{
    server->antiBruteForceThreshold = badRequests;
}

/****
*****  LIFE CYCLE
****/

static void closeServer(void* vserver)
{
    auto* server = static_cast<tr_rpc_server*>(vserver);

    stopServer(server);

    if (server->isStreamInitialized)
    {
        deflateEnd(&server->stream);
    }

    tr_free(server->url);
    tr_free(server->username);
    tr_free(server->password);
    delete server;
}

void tr_rpcClose(tr_rpc_server** ps)
{
    tr_runInEventThread((*ps)->session, closeServer, *ps);
    *ps = nullptr;
}

static void missing_settings_key(tr_quark const q)
{
    char const* str = tr_quark_get_string(q, nullptr);
    tr_logAddNamedError(MY_NAME, _("Couldn't find settings key \"%s\""), str);
}

tr_rpc_server* tr_rpcInit(tr_session* session, tr_variant* settings)
{
    bool boolVal;
    int64_t i;
    char const* str;
    tr_address address;

    tr_rpc_server* s = new tr_rpc_server{};
    s->session = session;

    tr_quark key = TR_KEY_rpc_enabled;

    if (!tr_variantDictFindBool(settings, key, &boolVal))
    {
        missing_settings_key(key);
    }
    else
    {
        s->isEnabled = boolVal;
    }

    key = TR_KEY_rpc_port;

    if (!tr_variantDictFindInt(settings, key, &i))
    {
        missing_settings_key(key);
    }
    else
    {
        s->port = (tr_port)i;
    }

    key = TR_KEY_rpc_url;

    if (!tr_variantDictFindStr(settings, key, &str, nullptr))
    {
        missing_settings_key(key);
    }
    else
    {
        s->url = tr_strdup(str);
    }

    key = TR_KEY_rpc_whitelist_enabled;

    if (!tr_variantDictFindBool(settings, key, &boolVal))
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetWhitelistEnabled(s, boolVal);
    }

    key = TR_KEY_rpc_host_whitelist_enabled;

    if (!tr_variantDictFindBool(settings, key, &boolVal))
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetHostWhitelistEnabled(s, boolVal);
    }

    key = TR_KEY_rpc_host_whitelist;

    if (!tr_variantDictFindStr(settings, key, &str, nullptr) && str != nullptr)
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetHostWhitelist(s, str);
    }

    key = TR_KEY_rpc_authentication_required;

    if (!tr_variantDictFindBool(settings, key, &boolVal))
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetPasswordEnabled(s, boolVal);
    }

    key = TR_KEY_rpc_whitelist;

    if (!tr_variantDictFindStr(settings, key, &str, nullptr) && str != nullptr)
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetWhitelist(s, str);
    }

    key = TR_KEY_rpc_username;

    if (!tr_variantDictFindStr(settings, key, &str, nullptr))
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetUsername(s, str);
    }

    key = TR_KEY_rpc_password;

    if (!tr_variantDictFindStr(settings, key, &str, nullptr))
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetPassword(s, str);
    }

    key = TR_KEY_anti_brute_force_enabled;

    if (!tr_variantDictFindBool(settings, key, &boolVal))
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetAntiBruteForceEnabled(s, boolVal);
    }

    key = TR_KEY_anti_brute_force_threshold;

    if (!tr_variantDictFindInt(settings, key, &i))
    {
        missing_settings_key(key);
    }
    else
    {
        tr_rpcSetAntiBruteForceThreshold(s, i);
    }

    key = TR_KEY_rpc_bind_address;

    if (!tr_variantDictFindStr(settings, key, &str, nullptr))
    {
        missing_settings_key(key);
        address = tr_inaddr_any;
    }
    else if (!tr_address_from_string(&address, str))
    {
        tr_logAddNamedError(MY_NAME, _("%s is not a valid address"), str);
        address = tr_inaddr_any;
    }
    else if (address.type != TR_AF_INET && address.type != TR_AF_INET6)
    {
        tr_logAddNamedError(MY_NAME, _("%s is not an IPv4 or IPv6 address. RPC listeners must be IPv4 or IPv6"), str);
        address = tr_inaddr_any;
    }

    s->bindAddress = address;

    if (s->isEnabled)
    {
        tr_logAddNamedInfo(
            MY_NAME,
            _("Serving RPC and Web requests on %s:%d%s"),
            tr_rpcGetBindAddress(s),
            (int)s->port,
            s->url);
        tr_runInEventThread(session, startServer, s);

        if (s->isWhitelistEnabled)
        {
            tr_logAddNamedInfo(MY_NAME, "%s", _("Whitelist enabled"));
        }

        if (s->isPasswordEnabled)
        {
            tr_logAddNamedInfo(MY_NAME, "%s", _("Password required"));
        }
    }

    char const* webClientDir = tr_getWebClientDir(s->session);
    if (!tr_str_is_empty(webClientDir))
    {
        tr_logAddNamedInfo(MY_NAME, _("Serving RPC and Web requests from directory '%s'"), webClientDir);
    }

    return s;
}
