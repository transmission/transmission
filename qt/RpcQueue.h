// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <type_traits>
#include <utility>

#include <QFutureInterface>
#include <QFutureWatcher>
#include <QObject>

#include "RpcClient.h"

namespace detail
{
template<typename Func, typename T, typename... Args>
concept invoke_result_is = std::same_as<std::invoke_result_t<Func, Args...>, T>;
}

class RpcQueue : public QObject
{
    Q_OBJECT

public:
    explicit RpcQueue(QObject* parent = nullptr);
    ~RpcQueue() override = default;
    RpcQueue(RpcQueue&&) = delete;
    RpcQueue(RpcQueue const&) = delete;
    RpcQueue& operator=(RpcQueue&&) = delete;
    RpcQueue& operator=(RpcQueue const&) = delete;

    constexpr void setTolerateErrors(bool tolerate_errors = true)
    {
        tolerate_errors_ = tolerate_errors;
    }

    template<typename Func>
    void add(Func const& func)
    {
        queue_.emplace(normalizeFunc(func), ErrorHandlerFunction());
    }

    template<typename Func, typename ErrorHandler>
    void add(Func const& func, ErrorHandler const& error_handler)
    {
        queue_.emplace(normalizeFunc(func), normalizeErrorHandler(error_handler));
    }

    // The first function in queue is ran synchronously
    // (hence it may be e.g. a lambda capturing local variables by reference).
    void run();

    using Tag = uint64_t;

    [[nodiscard]] constexpr auto tag() const noexcept
    {
        return tag_;
    }

private:
    // Internally queued function. Takes the last response future, makes a
    // request and returns a new response future.
    using QueuedFunction = std::function<RpcResponseFuture(RpcResponseFuture const&)>;

    // Internally stored error handler function. Takes the last response future and returns nothing.
    using ErrorHandlerFunction = std::function<void(RpcResponseFuture const&)>;

    void runNext(RpcResponseFuture const& response);

    // These overloads convert various forms of input closures to what we store internally.

    // normal closure, takes response and returns new future
    template<detail::invoke_result_is<RpcResponseFuture, RpcResponse const&> Func>
    static QueuedFunction normalizeFunc(Func const& func)
    {
        return [func](RpcResponseFuture const& r)
        {
            return func(r.result());
        };
    }

    // closure without argument (first step), takes nothing and returns new future
    template<detail::invoke_result_is<RpcResponseFuture> Func>
    static QueuedFunction normalizeFunc(Func const& func)
    {
        return [func](RpcResponseFuture const&)
        {
            return func();
        };
    }

    // closure without return value ("auxiliary"), takes response and returns nothing
    template<detail::invoke_result_is<void, RpcResponse const&> Func>
    static QueuedFunction normalizeFunc(Func const& func)
    {
        return [func](RpcResponseFuture const& r)
        {
            func(r.result());
            return createFinishedFuture();
        };
    }

    // closure without argument and return value, takes nothing and returns nothing -- next function will also get nothing
    template<detail::invoke_result_is<void> Func>
    static QueuedFunction normalizeFunc(Func const& func)
    {
        return [func](RpcResponseFuture const&)
        {
            func();
            return createFinishedFuture();
        };
    }

    // normal error handler, takes last response
    template<detail::invoke_result_is<void, RpcResponse const&> Func>
    static ErrorHandlerFunction normalizeErrorHandler(Func const& func)
    {
        return [func](RpcResponseFuture const& r)
        {
            func(r.result());
        };
    }

    // error handler without an argument, takes nothing
    template<detail::invoke_result_is<void> Func>
    static ErrorHandlerFunction normalizeErrorHandler(Func const& func)
    {
        return [func](RpcResponseFuture const&)
        {
            func();
        };
    }

    static RpcResponseFuture createFinishedFuture()
    {
        QFutureInterface<RpcResponse> promise;
        promise.reportStarted();
        promise.reportFinished();
        return promise.future();
    }

    static inline Tag next_tag = {};

    Tag const tag_ = next_tag++;
    bool tolerate_errors_ = {};
    QFutureInterface<RpcResponse> promise_;
    std::queue<std::pair<QueuedFunction, ErrorHandlerFunction>> queue_;
    ErrorHandlerFunction next_error_handler_;
    QFutureWatcher<RpcResponse> future_watcher_;

private slots:
    void stepFinished();
};
