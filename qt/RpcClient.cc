/*
 * This file Copyright (C) 2014-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstring>
#include <iostream>

#include <QApplication>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <event2/buffer.h>

#include <libtransmission/transmission.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/utils.h> // tr_free
#include <libtransmission/version.h> // LONG_VERSION_STRING

#include "RpcClient.h"

// #define DEBUG_HTTP

#define REQUEST_DATA_PROPERTY_KEY "requestData"
#define REQUEST_FUTUREINTERFACE_PROPERTY_KEY "requestReplyFutureInterface"

namespace
{

void destroyVariant(tr_variant* json)
{
    tr_variantFree(json);
    tr_free(json);
}

TrVariantPtr createVariant()
{
    return TrVariantPtr(tr_new0(tr_variant, 1), &destroyVariant);
}

} // namespace

RpcClient::RpcClient(QObject* parent) :
    QObject(parent),
    mySession(nullptr),
    myNAM(nullptr),
    myNextTag(0)
{
    qRegisterMetaType<TrVariantPtr>("TrVariantPtr");
}

void RpcClient::stop()
{
    mySession = nullptr;
    mySessionId.clear();
    myUrl.clear();

    if (myNAM != nullptr)
    {
        myNAM->deleteLater();
        myNAM = nullptr;
    }
}

void RpcClient::start(tr_session* session)
{
    mySession = session;
}

void RpcClient::start(QUrl const& url)
{
    myUrl = url;
}

bool RpcClient::isLocal() const
{
    if (mySession != nullptr)
    {
        return true;
    }

    if (QHostAddress(myUrl.host()).isLoopback())
    {
        return true;
    }

    return false;
}

QUrl const& RpcClient::url() const
{
    return myUrl;
}

RpcResponseFuture RpcClient::exec(tr_quark method, tr_variant* args)
{
    return exec(tr_quark_get_string(method, nullptr), args);
}

RpcResponseFuture RpcClient::exec(char const* method, tr_variant* args)
{
    TrVariantPtr json = createVariant();
    tr_variantInitDict(json.get(), 3);
    tr_variantDictAddStr(json.get(), TR_KEY_method, method);

    if (args != nullptr)
    {
        tr_variantDictSteal(json.get(), TR_KEY_arguments, args);
    }

    return sendRequest(json);
}

int64_t RpcClient::getNextTag()
{
    return myNextTag++;
}

void RpcClient::sendNetworkRequest(TrVariantPtr json, QFutureInterface<RpcResponse> const& promise)
{
    QNetworkRequest request;
    request.setUrl(myUrl);
    request.setRawHeader("User-Agent", (qApp->applicationName() + QLatin1Char('/') +
        QString::fromUtf8(LONG_VERSION_STRING)).toUtf8());
    request.setRawHeader("Content-Type", "application/json; charset=UTF-8");

    if (!mySessionId.isEmpty())
    {
        request.setRawHeader(TR_RPC_SESSION_ID_HEADER, mySessionId.toUtf8());
    }

    size_t rawJsonDataLength;
    char* rawJsonData = tr_variantToStr(json.get(), TR_VARIANT_FMT_JSON_LEAN, &rawJsonDataLength);
    QByteArray jsonData(rawJsonData, rawJsonDataLength);
    tr_free(rawJsonData);

    QNetworkReply* reply = networkAccessManager()->post(request, jsonData);
    reply->setProperty(REQUEST_DATA_PROPERTY_KEY, QVariant::fromValue(json));
    reply->setProperty(REQUEST_FUTUREINTERFACE_PROPERTY_KEY, QVariant::fromValue(promise));

    connect(reply, SIGNAL(downloadProgress(qint64, qint64)), this, SIGNAL(dataReadProgress()));
    connect(reply, SIGNAL(uploadProgress(qint64, qint64)), this, SIGNAL(dataSendProgress()));

#ifdef DEBUG_HTTP
    std::cerr << "sending " << "POST " << qPrintable(myUrl.path()) << std::endl;

    for (QByteArray const& b : request.rawHeaderList())
    {
        std::cerr << b.constData() << ": " << request.rawHeader(b).constData() << std::endl;
    }

    std::cerr << "Body:\n" << jsonData.constData() << std::endl;
#endif
}

void RpcClient::sendLocalRequest(TrVariantPtr json, QFutureInterface<RpcResponse> const& promise, int64_t tag)
{
    myLocalRequests.insert(tag, promise);
    tr_rpc_request_exec_json(mySession, json.get(), localSessionCallback, this);
}

RpcResponseFuture RpcClient::sendRequest(TrVariantPtr json)
{
    int64_t tag = getNextTag();
    tr_variantDictAddInt(json.get(), TR_KEY_tag, tag);

    QFutureInterface<RpcResponse> promise;
    promise.setExpectedResultCount(1);
    promise.setProgressRange(0, 1);
    promise.setProgressValue(0);
    promise.reportStarted();

    if (mySession != nullptr)
    {
        sendLocalRequest(json, promise, tag);
    }
    else if (!myUrl.isEmpty())
    {
        sendNetworkRequest(json, promise);
    }

    return promise.future();
}

QNetworkAccessManager* RpcClient::networkAccessManager()
{
    if (myNAM == nullptr)
    {
        myNAM = new QNetworkAccessManager();

        connect(myNAM, SIGNAL(finished(QNetworkReply*)), this, SLOT(networkRequestFinished(QNetworkReply*)));

        connect(myNAM, SIGNAL(authenticationRequired(QNetworkReply*, QAuthenticator*)), this,
            SIGNAL(httpAuthenticationRequired()));
    }

    return myNAM;
}

void RpcClient::localSessionCallback(tr_session* s, tr_variant* response, void* vself)
{
    Q_UNUSED(s)

    RpcClient* self = static_cast<RpcClient*>(vself);

    TrVariantPtr json = createVariant();
    *json = *response;
    tr_variantInitBool(response, false);

    // this callback is invoked in the libtransmission thread, so we don't want
    // to process the response here... let's push it over to the Qt thread.
    QMetaObject::invokeMethod(self, "localRequestFinished", Qt::QueuedConnection, Q_ARG(TrVariantPtr, json));
}

void RpcClient::networkRequestFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    QFutureInterface<RpcResponse> promise = reply->property(REQUEST_FUTUREINTERFACE_PROPERTY_KEY).
        value<QFutureInterface<RpcResponse>>();

#ifdef DEBUG_HTTP
    std::cerr << "http response header: " << std::endl;

    for (QByteArray const& b : reply->rawHeaderList())
    {
        std::cerr << b.constData() << ": " << reply->rawHeader(b).constData() << std::endl;
    }

    std::cerr << "json:\n" << reply->peek(reply->bytesAvailable()).constData() << std::endl;
#endif

    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 409 &&
        reply->hasRawHeader(TR_RPC_SESSION_ID_HEADER))
    {
        // we got a 409 telling us our session id has expired.
        // update it and resubmit the request.
        mySessionId = QString::fromUtf8(reply->rawHeader(TR_RPC_SESSION_ID_HEADER));

        sendNetworkRequest(reply->property(REQUEST_DATA_PROPERTY_KEY).value<TrVariantPtr>(), promise);
        return;
    }

    emit networkResponse(reply->error(), reply->errorString());

    if (reply->error() != QNetworkReply::NoError)
    {
        RpcResponse result;
        result.networkError = reply->error();

        promise.setProgressValueAndText(1, reply->errorString());
        promise.reportFinished(&result);
    }
    else
    {
        RpcResponse result;

        QByteArray const jsonData = reply->readAll().trimmed();
        TrVariantPtr json = createVariant();

        if (tr_variantFromJson(json.get(), jsonData.constData(), jsonData.size()) == 0)
        {
            result = parseResponseData(*json);
        }

        promise.setProgressValue(1);
        promise.reportFinished(&result);
    }
}

void RpcClient::localRequestFinished(TrVariantPtr response)
{
    int64_t tag = parseResponseTag(*response);
    RpcResponse result = parseResponseData(*response);
    QFutureInterface<RpcResponse> promise = myLocalRequests.take(tag);

    promise.setProgressRange(0, 1);
    promise.setProgressValue(1);
    promise.reportFinished(&result);
}

int64_t RpcClient::parseResponseTag(tr_variant& json)
{
    int64_t tag;

    if (!tr_variantDictFindInt(&json, TR_KEY_tag, &tag))
    {
        tag = -1;
    }

    return tag;
}

RpcResponse RpcClient::parseResponseData(tr_variant& json)
{
    RpcResponse ret;

    char const* result;

    if (tr_variantDictFindStr(&json, TR_KEY_result, &result, nullptr))
    {
        ret.result = QString::fromUtf8(result);
        ret.success = std::strcmp(result, "success") == 0;
    }

    tr_variant* args;

    if (tr_variantDictFindDict(&json, TR_KEY_arguments, &args))
    {
        ret.args = createVariant();
        *ret.args = *args;
        tr_variantInitBool(args, false);
    }

    return ret;
}
