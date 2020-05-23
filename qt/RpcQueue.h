/*
 * This file Copyright (C) 2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <functional>
#include <type_traits>

#include <QFutureInterface>
#include <QFutureWatcher>
#include <QObject>
#include <QPair>
#include <QQueue>

#include "RpcClient.h"

class RpcQueue : public QObject
{
    Q_OBJECT

public:
    explicit RpcQueue(QObject* parent = nullptr);

    void setTolerateErrors(bool tolerateErrors = true)
    {
        myTolerateErrors = tolerateErrors;
    }

    template<typename Func>
    void add(Func func)
    {
        myQueue.enqueue(qMakePair(normalizeFunc(func), ErrorHandlerFunction()));
    }

    template<typename Func, typename ErrorHandler>
    void add(Func func, ErrorHandler errorHandler)
    {
        myQueue.enqueue(qMakePair(normalizeFunc(func), normalizeErrorHandler(errorHandler)));
    }

    // The first function in queue is ran synchronously
    // (hence it may be e. g. a lambda capturing local variables by reference).
    void run();

private:
    // Internally queued function. Takes the last response future, makes a
    // request and returns a new response future.
    using QueuedFunction = std::function<RpcResponseFuture(RpcResponseFuture const&)>;

    // Internally stored error handler function. Takes the last response future and returns nothing.
    using ErrorHandlerFunction = std::function<void (RpcResponseFuture const&)>;

private slots:
    void stepFinished();

private:
    void runNext(RpcResponseFuture const& response);

    // These overloads convert various forms of input closures to what we store internally.

    // normal closure, takes response and returns new future
    template<typename Func, typename std::enable_if<
        std::is_same_v<typename std::invoke_result_t<Func, RpcResponse const&>, RpcResponseFuture>
        >::type* = nullptr>
    QueuedFunction normalizeFunc(Func const& func)
    {
        return [func](RpcResponseFuture const& r)
            {
                return func(r.result());
            };
    }

    // closure without argument (first step), takes nothing and returns new future
    template<typename Func, typename std::enable_if<
        std::is_same_v<typename std::invoke_result_t<Func>, RpcResponseFuture>
        >::type* = nullptr>
    QueuedFunction normalizeFunc(Func const& func)
    {
        return [func](RpcResponseFuture const&)
            {
                return func();
            };
    }

    // closure without return value ("auxiliary"), takes response and returns nothing -- internally we reuse the last future
    template<typename Func, typename std::enable_if<
        std::is_same_v<typename std::invoke_result_t<Func, RpcResponse const&>, void>
        >::type* = nullptr>
    QueuedFunction normalizeFunc(Func const& func)
    {
        return [func](RpcResponseFuture const& r)
            {
                func(r.result());
                return r;
            };
    }

    // closure without argument and return value, takes nothing and returns nothing -- next function will also get nothing
    template<typename Func, typename std::enable_if<
        std::is_same_v<typename std::invoke_result_t<Func>, void>
        >::type* = nullptr>
    QueuedFunction normalizeFunc(Func const& func)
    {
        return [func](RpcResponseFuture const& r)
            {
                func();
                return r;
            };
    }

    // normal error handler, takes last response
    template<typename Func, typename std::enable_if<
        std::is_same_v<typename std::invoke_result_t<Func, RpcResponse const&>, void>
        >::type* = nullptr>
    ErrorHandlerFunction normalizeErrorHandler(Func const& func)
    {
        return [func](RpcResponseFuture const& r)
            {
                func(r.result());
            };
    }

    // error handler without an argument, takes nothing
    template<typename Func, typename std::enable_if<
        std::is_same_v<typename std::invoke_result_t<Func>, void>
        >::type* = nullptr>
    ErrorHandlerFunction normalizeErrorHandler(Func const& func)
    {
        return [func](RpcResponseFuture const&)
            {
                func();
            };
    }

private:
    bool myTolerateErrors;
    QFutureInterface<RpcResponse> myPromise;
    QQueue<QPair<QueuedFunction, ErrorHandlerFunction>> myQueue;
    ErrorHandlerFunction myNextErrorHandler;
    QFutureWatcher<RpcResponse> myFutureWatcher;
};
