// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>
#include <memory>
#include <string>
#include <string_view>

struct evbuffer;

class tr_web
{
public:
    struct Response
    {
        long status;
        std::string body;
        bool did_connect;
        bool did_timeout;
        void* user_data;
    };

    using done_func = void (*)(Response&& response);

    /**
     * Mediates between tr_web and its clients.
     *
     * NB: Note that tr_web calls all these methods in its own thread.
     */
    class Controller
    {
    public:
        virtual ~Controller() = default;

        // Return the location of the cookie file, or nullopt to not use one
        [[nodiscard]] virtual std::optional<std::string> cookieFile() const
        {
            return std::nullopt;
        }

        // Return the preferred user public address string, or nullopt to not use one
        [[nodiscard]] virtual std::optional<std::string> publicAddress() const
        {
            return std::nullopt;
        }

        // Return the preferred user aagent, or nullopt to not use one
        [[nodiscard]] virtual std::optional<std::string> userAgent() const
        {
            return std::nullopt;
        }

        // Notify the system that `byte_count` of download bandwidth was used
        virtual void notifyBandwidthConsumed([[maybe_unused]] int bandwidth_tag, [[maybe_unused]] size_t byte_count)
        {
        }

        // return the number of bytes that should be allowed. See Bandwidth::clamp()
        [[nodiscard]] virtual unsigned int clamp([[maybe_unused]] int bandwidth_tag, unsigned int byte_count) const
        {
            return byte_count;
        }

        virtual void run(done_func func, Response&& response) const
        {
            func(std::move(response));
        }
    };

    static std::unique_ptr<tr_web> create(Controller& controller);
    ~tr_web();

    class RunOptions
    {
    public:
        RunOptions(std::string_view url_in, done_func done_func_in, void* done_func_user_data_in)
            : url{ url_in }
            , done_func{ done_func_in }
            , done_func_user_data{ done_func_user_data_in }
        {
        }

        static constexpr int DefaultTimeoutSecs = 120;

        std::string url;
        std::optional<std::string> cookies;
        std::optional<std::string> range;
        std::optional<int> speed_limit_tag;
        std::optional<int> sndbuf;
        std::optional<int> rcvbuf;
        done_func done_func = nullptr;
        void* done_func_user_data = nullptr;
        evbuffer* buffer = nullptr;
        int timeout_secs = DefaultTimeoutSecs;
    };

    void run(RunOptions&& options);
    void closeSoon();
    [[nodiscard]] bool isClosed() const;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
    explicit tr_web(Controller& controller);
};

void tr_sessionFetch(struct tr_session* session, tr_web::RunOptions&& options);
