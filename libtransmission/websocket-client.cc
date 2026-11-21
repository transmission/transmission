// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Persistent WS/WSS tracker sockets using libcurl, including Transmission's proxy.
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <curl/curl.h>

#include "libtransmission/curl-tls.h"
#include "libtransmission/env.h"
#include "libtransmission/log.h"
#include "libtransmission/websocket-client.h"

using namespace std::chrono_literals;

namespace
{
constexpr size_t MaxPendingBytes = 4U * 1024U * 1024U;
constexpr size_t MaxIncomingBytes = 1024U * 1024U;
constexpr auto PollInterval = 25ms;

struct CurlDeleter
{
    void operator()(CURL* curl) const
    {
        curl_easy_cleanup(curl);
    }
};

} // namespace

bool tr_websocket_client::supports_websockets()
{
    auto const* info = curl_version_info(CURLVERSION_NOW);
    if (info == nullptr || info->protocols == nullptr)
    {
        return false;
    }
    bool has_ws = false;
    bool has_wss = false;
    for (auto const* protocol = info->protocols; *protocol != nullptr; ++protocol)
    {
        has_ws |= std::string_view{ *protocol } == "ws";
        has_wss |= std::string_view{ *protocol } == "wss";
    }
    return has_ws && has_wss;
}

tr_websocket_client::tr_websocket_client(std::string url, std::optional<std::string> proxy_url, Callbacks callbacks)
    : url_{ std::move(url) }
    , proxy_url_{ std::move(proxy_url) }
    , callbacks_{ std::move(callbacks) }
{
}

tr_websocket_client::~tr_websocket_client()
{
    close();
}

void tr_websocket_client::open()
{
    worker_ = std::thread([this] { run(); });
}

bool tr_websocket_client::send(std::string message)
{
    auto const lock = std::lock_guard{ mutex_ };
    if (stopping_)
    {
        return false;
    }
    if (message.size() > MaxPendingBytes - pending_bytes_)
    {
        backpressured_ = true;
        return false;
    }
    pending_bytes_ += message.size();
    pending_.emplace_back(std::move(message));
    return true;
}

void tr_websocket_client::close()
{
    stopping_ = true;
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void tr_websocket_client::run()
{
    if (!supports_websockets())
    {
        if (!stopping_)
        {
            callbacks_.closed("libcurl was built without WS/WSS protocol support");
        }
        return;
    }

    auto const curl = std::unique_ptr<CURL, CurlDeleter>{ curl_easy_init() };
    if (!curl)
    {
        if (!stopping_)
        {
            callbacks_.closed("WebSocket transport initialization failed");
        }
        return;
    }

    // Follow libcurl's environment proxy defaults when Transmission has no proxy.
    // A configured Transmission proxy must never be bypassed by NO_PROXY.
    curl_easy_setopt(curl.get(), CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 12000L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl.get(), CURLOPT_PROXY, proxy_url_ ? proxy_url_->c_str() : nullptr);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROXY, proxy_url_ ? "" : nullptr);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPPROXYTUNNEL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "ws,wss");

    auto const curl_ssl_verify = !tr_env_key_exists("TR_CURL_SSL_NO_VERIFY");
    auto const curl_proxy_ssl_verify = !tr_env_key_exists("TR_CURL_PROXY_SSL_NO_VERIFY");
    auto const curl_ca_bundle = tr_env_get_string("CURL_CA_BUNDLE");

    tr_curl_configure_tls(curl.get(), curl_ca_bundle, curl_ssl_verify, curl_proxy_ssl_verify);

    auto const result = curl_easy_perform(curl.get());
    if (stopping_)
    {
        return;
    }
    if (result != CURLE_OK)
    {
        tr_logAddDebug(
            "WSS connection failed: libcurl error " + std::to_string(static_cast<int>(result)) + " (" +
                curl_easy_strerror(result) + ")",
            "WebSocketTracker");
        callbacks_.closed("WebSocket tracker connection failed (curl error " + std::to_string(result) + ")");
        return;
    }
    callbacks_.opened();

    auto incoming = std::string{};
    auto outgoing = std::string{};
    size_t written = 0;
    std::array<char, 16U * 1024U> buffer{};

    while (!stopping_)
    {
        bool did_work = false;
        if (outgoing.empty())
        {
            bool notify_writable = false;
            {
                auto const lock = std::lock_guard{ mutex_ };
                if (!pending_.empty())
                {
                    outgoing = std::move(pending_.front());
                    pending_.pop_front();
                    pending_bytes_ -= outgoing.size();
                    written = 0;
                    if (backpressured_ && pending_bytes_ < MaxPendingBytes / 2U)
                    {
                        backpressured_ = false;
                        notify_writable = true;
                    }
                }
            }
            if (notify_writable && !stopping_)
            {
                callbacks_.writable();
            }
        }

        if (!outgoing.empty())
        {
            size_t n = 0;
            auto const ec = curl_ws_send(curl.get(), outgoing.data() + written, outgoing.size() - written, &n, 0, CURLWS_TEXT);
            written += n;
            if (ec != CURLE_OK && ec != CURLE_AGAIN)
            {
                callbacks_.closed("WebSocket tracker send failed");
                return;
            }
            did_work = n != 0;
            if (written == outgoing.size())
            {
                outgoing.clear();
            }
        }

        size_t received = 0;
        // libcurl 8.2 made received WebSocket frame metadata const.
#if LIBCURL_VERSION_NUM < 0x080200
        curl_ws_frame* frame = nullptr;
#else
        curl_ws_frame const* frame = nullptr;
#endif
        auto const ec = curl_ws_recv(curl.get(), buffer.data(), buffer.size(), &received, &frame);
        if (ec == CURLE_GOT_NOTHING)
        {
            break;
        }
        if (ec != CURLE_OK && ec != CURLE_AGAIN)
        {
            callbacks_.closed("WebSocket tracker receive failed");
            return;
        }
        if (ec == CURLE_OK && frame != nullptr)
        {
            did_work = true;
            if ((frame->flags & CURLWS_CLOSE) != 0)
            {
                break;
            }
            if ((frame->flags & CURLWS_BINARY) != 0)
            {
                // WebTorrent tracker signaling is JSON text only.
                incoming.clear();
            }
            else if ((frame->flags & CURLWS_TEXT) != 0)
            {
                if (received > MaxIncomingBytes - incoming.size())
                {
                    callbacks_.closed("WebSocket tracker message too large");
                    return;
                }
                incoming.append(buffer.data(), received);
                if (frame->bytesleft == 0 && (frame->flags & CURLWS_CONT) == 0)
                {
                    callbacks_.message(std::move(incoming));
                    incoming.clear();
                }
            }
        }
        if (!did_work)
        {
            std::this_thread::sleep_for(PollInterval);
        }
    }
    if (!stopping_)
    {
        callbacks_.closed("WebSocket tracker closed");
    }
}
