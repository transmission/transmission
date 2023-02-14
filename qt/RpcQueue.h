// This file Copyright © 2016-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>

#include <QFutureInterface>
#include <QFutureWatcher>
#include <QObject>
#include <QPair>
#include <QQueue>

#include <libtransmission/tr-macros.h>

#include "RpcClient.h"

class RpcQueue : public QObject
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(RpcQueue)

public:
    explicit RpcQueue(QObject* parent = nullptr);

    void setTolerateErrors(bool tolerate_errors = true)
    {
        tolerate_errors_ = tolerate_errors;
    }

    template<typename Func>
    void add(Func func)
    {
        queue_.enqueue(qMakePair(normalizeFunc(func), ErrorHandlerFunction()));
    }

    template<typename Func, typename ErrorHandler>
    void add(Func func, ErrorHandler error_handler)
    {
        queue_.enqueue(qMakePair(normalizeFunc(func), normalizeErrorHandler(error_handler)));
    }

    // The first function in queue is ran synchronously
    // (hence it may be e. g. a lambda capturing local variables by reference).
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
    template<
        typename Func,
        typename std::enable_if_t<std::is_same_v<typename std::invoke_result_t<Func, RpcResponse const&>, RpcResponseFuture>>* =
            nullptr>
    QueuedFunction normalizeFunc(Func const& func) const
    {
        return [func](RpcResponseFuture const& r)
        {
            return func(r.result());
        };
    }

    // closure without argument (first step), takes nothing and returns new future
    template<
        typename Func,
        typename std::enable_if_t<std::is_same_v<typename std::invoke_result_t<Func>, RpcResponseFuture>>* = nullptr>
    QueuedFunction normalizeFunc(Func const& func) const
    {
        return [func](RpcResponseFuture const&)
        {
            return func();
        };
    }

    // closure without return value ("auxiliary"), takes response and returns nothing
    template<
        typename Func,
        typename std::enable_if_t<std::is_same_v<typename std::invoke_result_t<Func, RpcResponse const&>, void>>* = nullptr>
    QueuedFunction normalizeFunc(Func const& func) const
    {
        return [func](RpcResponseFuture const& r)
        {
            func(r.result());
            return createFinishedFuture();
        };
    }

    // closure without argument and return value, takes nothing and returns nothing -- next function will also get nothing
    template<typename Func, typename std::enable_if_t<std::is_same_v<typename std::invoke_result_t<Func>, void>>* = nullptr>
    QueuedFunction normalizeFunc(Func const& func) const
    {
        return [func](RpcResponseFuture const&)
        {
            func();
            return createFinishedFuture();
        };
    }

    // normal error handler, takes last response
    template<
        typename Func,
        typename std::enable_if_t<std::is_same_v<typename std::invoke_result_t<Func, RpcResponse const&>, void>>* = nullptr>
    ErrorHandlerFunction normalizeErrorHandler(Func const& func) const
    {
        return [func](RpcResponseFuture const& r)
        {
            func(r.result());
        };
    }

    // error handler without an argument, takes nothing
    template<typename Func, typename std::enable_if_t<std::is_same_v<typename std::invoke_result_t<Func>, void>>* = nullptr>
    ErrorHandlerFunction normalizeErrorHandler(Func const& func) const
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
    QQueue<QPair<QueuedFunction, ErrorHandlerFunction>> queue_;
    ErrorHandlerFunction next_error_handler_;
    QFutureWatcher<RpcResponse> future_watcher_;

private slots:
    void stepFinished();
};
