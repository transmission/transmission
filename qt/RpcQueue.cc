// This file Copyright © 2016-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cassert>

#include "RpcQueue.h"

RpcQueue::RpcQueue(QObject* parent)
    : QObject(parent)
{
    connect(&future_watcher_, &QFutureWatcher<RpcResponse>::finished, this, &RpcQueue::stepFinished);
}

void RpcQueue::stepFinished()
{
    RpcResponse result;

    if (future_watcher_.future().isResultReadyAt(0))
    {
        result = future_watcher_.result();
        RpcResponseFuture const future = future_watcher_.future();

        // we can't handle network errors, abort queue and pass the error upwards
        if (result.networkError != QNetworkReply::NoError)
        {
            assert(!result.success);

            promise_.reportFinished(&result);
            deleteLater();
            return;
        }

        // call user-handler for ordinary errors
        if (!result.success && next_error_handler_)
        {
            next_error_handler_(future);
        }

        // run next request, if we have one to run and there was no error (or if we tolerate errors)
        if ((result.success || tolerate_errors_) && !queue_.isEmpty())
        {
            runNext(future);
            return;
        }
    }
    else
    {
        assert(!next_error_handler_);
        assert(queue_.isEmpty());

        // one way or another, the last step returned nothing.
        // assume it is OK and ensure that we're not going to give an empty response object to any of the next steps.
        result.success = true;
    }

    promise_.reportFinished(&result);
    deleteLater();
}

void RpcQueue::runNext(RpcResponseFuture const& response)
{
    assert(!queue_.isEmpty());

    auto next = queue_.dequeue();
    next_error_handler_ = next.second;
    future_watcher_.setFuture((next.first)(response));
}

void RpcQueue::run()
{
    runNext(RpcResponseFuture());
}
