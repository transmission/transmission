/*
 * This file Copyright (C) 2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cassert>

#include "RpcQueue.h"

RpcQueue::RpcQueue(QObject* parent) :
    QObject(parent),
    myTolerateErrors(false)
{
    connect(&myFutureWatcher, SIGNAL(finished()), SLOT(stepFinished()));
}

void RpcQueue::stepFinished()
{
    RpcResponse result;

    if (myFutureWatcher.future().isResultReadyAt(0))
    {
        result = myFutureWatcher.result();
        RpcResponseFuture future = myFutureWatcher.future();

        // we can't handle network errors, abort queue and pass the error upwards
        if (result.networkError != QNetworkReply::NoError)
        {
            assert(!result.success);

            myPromise.reportFinished(&result);
            deleteLater();
            return;
        }

        // call user-handler for ordinary errors
        if (!result.success && myNextErrorHandler)
        {
            myNextErrorHandler(future);
        }

        // run next request, if we have one to run and there was no error (or if we tolerate errors)
        if ((result.success || myTolerateErrors) && !myQueue.isEmpty())
        {
            runNext(future);
            return;
        }
    }
    else
    {
        assert(!myNextErrorHandler);
        assert(myQueue.isEmpty());

        // one way or another, the last step returned nothing.
        // assume it is OK and ensure that we're not going to give an empty response object to any of the next steps.
        result.success = true;
    }

    myPromise.reportFinished(&result);
    deleteLater();
}

void RpcQueue::runNext(RpcResponseFuture const& response)
{
    assert(!myQueue.isEmpty());

    RpcResponseFuture const oldFuture = myFutureWatcher.future();

    while (true)
    {
        auto next = myQueue.dequeue();
        myNextErrorHandler = next.second;
        myFutureWatcher.setFuture((next.first)(response));

        if (oldFuture != myFutureWatcher.future())
        {
            break;
        }

        if (myQueue.isEmpty())
        {
            deleteLater();
            break;
        }
    }
}

void RpcQueue::run()
{
    runNext(RpcResponseFuture());
}
