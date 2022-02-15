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

    class Controller
    {
    public:
        virtual ~Controller() = default;

        [[nodiscard]] virtual std::optional<std::string> cookieFile() const
        {
            return std::nullopt;
        }

        [[nodiscard]] virtual std::optional<std::string> publicAddress() const
        {
            return std::nullopt;
        }

        [[nodiscard]] virtual std::optional<std::string> userAgent() const
        {
            return std::nullopt;
        }

        [[nodiscard]] virtual std::optional<long> desiredSpeedBytesPerSecond([[maybe_unused]] int speed_limit_tag) const
        {
            return std::nullopt;
        }

        virtual void run(done_func func, Response&& response) const
        {
            func(std::move(response));
        }
    };

    static std::unique_ptr<tr_web> create(Controller const& controller);
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
        std::string range;
        std::string cookies;
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
    explicit tr_web(Controller const& controller);
};

void tr_sessionFetch(struct tr_session* session, tr_web::RunOptions&& options);
