/*
 * This file Copyright (C) 2014-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "RpcClient.h"

#include <cstring>

#include <QApplication>
#include <QAuthenticator>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <libtransmission/rpcimpl.h>
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_free
#include <libtransmission/version.h> // LONG_VERSION_STRING

#include "VariantHelpers.h"

using ::trqt::variant_helpers::dictAdd;
using ::trqt::variant_helpers::dictFind;
using ::trqt::variant_helpers::variantInit;

namespace
{

char const constexpr* const RequestDataPropertyKey { "requestData" };
char const constexpr* const RequestFutureinterfacePropertyKey { "requestReplyFutureInterface" };

bool const Verbose = tr_env_key_exists("TR_RPC_VERBOSE");

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
    QObject(parent)
{
    qRegisterMetaType<TrVariantPtr>("TrVariantPtr");
}

void RpcClient::stop()
{
    session_ = nullptr;
    session_id_.clear();
    url_.clear();
    request_.reset();

    if (nam_ != nullptr)
    {
        nam_->deleteLater();
        nam_ = nullptr;
    }
}

void RpcClient::start(tr_session* session)
{
    session_ = session;
}

void RpcClient::start(QUrl const& url)
{
    url_ = url;
    request_.reset();
}

bool RpcClient::isLocal() const
{
    if (session_ != nullptr)
    {
        return true;
    }

    if (QHostAddress(url_.host()).isLoopback())
    {
        return true;
    }

    return false;
}

QUrl const& RpcClient::url() const
{
    return url_;
}

RpcResponseFuture RpcClient::exec(tr_quark method, tr_variant* args)
{
    auto len = size_t{};
    auto const* str = tr_quark_get_string(method, &len);
    return exec(std::string_view(str, len), args);
}

RpcResponseFuture RpcClient::exec(std::string_view method, tr_variant* args)
{
    TrVariantPtr json = createVariant();
    tr_variantInitDict(json.get(), 3);
    dictAdd(json.get(), TR_KEY_method, method);

    if (args != nullptr)
    {
        tr_variantDictSteal(json.get(), TR_KEY_arguments, args);
    }

    return sendRequest(json);
}

int64_t RpcClient::getNextTag()
{
    return next_tag_++;
}

void RpcClient::sendNetworkRequest(TrVariantPtr json, QFutureInterface<RpcResponse> const& promise)
{
    if (!request_)
    {
        QNetworkRequest request;
        request.setUrl(url_);
        request.setRawHeader("User-Agent", (QApplication::applicationName() + QLatin1Char('/') + QString::fromUtf8(
            LONG_VERSION_STRING)).toUtf8());
        request.setRawHeader("Content-Type", "application/json; charset=UTF-8");
        if (!session_id_.isEmpty())
        {
            request.setRawHeader(TR_RPC_SESSION_ID_HEADER, session_id_.toUtf8());
        }

        request_ = request;
    }

    size_t raw_json_data_length;
    auto* raw_json_data = tr_variantToStr(json.get(), TR_VARIANT_FMT_JSON_LEAN, &raw_json_data_length);
    QByteArray json_data(raw_json_data, raw_json_data_length);
    tr_free(raw_json_data);

    QNetworkReply* reply = networkAccessManager()->post(*request_, json_data);
    reply->setProperty(RequestDataPropertyKey, QVariant::fromValue(json));
    reply->setProperty(RequestFutureinterfacePropertyKey, QVariant::fromValue(promise));

    connect(reply, &QNetworkReply::downloadProgress, this, &RpcClient::dataReadProgress);
    connect(reply, &QNetworkReply::uploadProgress, this, &RpcClient::dataSendProgress);

    if (Verbose)
    {
        qInfo() << "sending" << "POST" << qPrintable(url_.path());

        for (QByteArray const& b : request_->rawHeaderList())
        {
            qInfo() << b.constData() << ": " << request_->rawHeader(b).constData();
        }

        qInfo() << "Body:";
        qInfo() << json_data.constData();
    }
}

void RpcClient::sendLocalRequest(TrVariantPtr json, QFutureInterface<RpcResponse> const& promise, int64_t tag)
{
    local_requests_.insert(tag, promise);
    tr_rpc_request_exec_json(session_, json.get(), localSessionCallback, this);
}

RpcResponseFuture RpcClient::sendRequest(TrVariantPtr json)
{
    int64_t tag = getNextTag();
    dictAdd(json.get(), TR_KEY_tag, tag);

    QFutureInterface<RpcResponse> promise;
    promise.setExpectedResultCount(1);
    promise.setProgressRange(0, 1);
    promise.setProgressValue(0);
    promise.reportStarted();

    if (session_ != nullptr)
    {
        sendLocalRequest(json, promise, tag);
    }
    else if (!url_.isEmpty())
    {
        sendNetworkRequest(json, promise);
    }

    return promise.future();
}

QNetworkAccessManager* RpcClient::networkAccessManager()
{
    if (nam_ == nullptr)
    {
        nam_ = new QNetworkAccessManager();

        connect(nam_, &QNetworkAccessManager::finished, this, &RpcClient::networkRequestFinished);

        connect(nam_, &QNetworkAccessManager::authenticationRequired, this, &RpcClient::httpAuthenticationRequired);
    }

    return nam_;
}

void RpcClient::localSessionCallback(tr_session* s, tr_variant* response, void* vself) noexcept
{
    Q_UNUSED(s)

    auto* self = static_cast<RpcClient*>(vself);

    TrVariantPtr json = createVariant();
    *json = *response;
    variantInit(response, false);

    // this callback is invoked in the libtransmission thread, so we don't want
    // to process the response here... let's push it over to the Qt thread.
    QMetaObject::invokeMethod(self, "localRequestFinished", Qt::QueuedConnection, Q_ARG(TrVariantPtr, json));
}

void RpcClient::networkRequestFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    auto promise = reply->property(RequestFutureinterfacePropertyKey).
        value<QFutureInterface<RpcResponse>>();

    if (Verbose)
    {
        qInfo() << "http response header:";

        for (QByteArray const& b : reply->rawHeaderList())
        {
            qInfo() << b.constData() << ": " << reply->rawHeader(b).constData();
        }

        qInfo() << "json:";
        qInfo() << reply->peek(reply->bytesAvailable()).constData();
    }

    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 409 &&
        reply->hasRawHeader(TR_RPC_SESSION_ID_HEADER))
    {
        // we got a 409 telling us our session id has expired.
        // update it and resubmit the request.
        session_id_ = QString::fromUtf8(reply->rawHeader(TR_RPC_SESSION_ID_HEADER));
        request_.reset();

        sendNetworkRequest(reply->property(RequestDataPropertyKey).value<TrVariantPtr>(), promise);
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

        QByteArray const json_data = reply->readAll().trimmed();
        TrVariantPtr json = createVariant();

        if (tr_variantFromJson(json.get(), json_data.constData(), json_data.size()) == 0)
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
    QFutureInterface<RpcResponse> promise = local_requests_.take(tag);

    promise.setProgressRange(0, 1);
    promise.setProgressValue(1);
    promise.reportFinished(&result);
}

int64_t RpcClient::parseResponseTag(tr_variant& json) const
{
    auto const tag = dictFind<int>(&json, TR_KEY_tag);
    return tag ? *tag : -1;
}

RpcResponse RpcClient::parseResponseData(tr_variant& json) const
{
    RpcResponse ret;

    auto const result = dictFind<QString>(&json, TR_KEY_result);
    if (result)
    {
        ret.result = *result;
        ret.success = *result == QStringLiteral("success");
    }

    tr_variant* args;

    if (tr_variantDictFindDict(&json, TR_KEY_arguments, &args))
    {
        ret.args = createVariant();
        *ret.args = *args;
        variantInit(args, false);
    }

    return ret;
}
