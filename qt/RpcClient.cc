// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#include <fmt/format.h>

#include "RpcClient.h"

#include <QApplication>
#include <QAuthenticator>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <libtransmission/transmission.h>

#include <libtransmission/api-compat.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/version.h> // LONG_VERSION_STRING

#include "VariantHelpers.h"

using ::trqt::variant_helpers::dictAdd;
using ::trqt::variant_helpers::dictFind;
using ::trqt::variant_helpers::variantInit;

namespace
{

char constexpr const* const RequestDataPropertyKey{ "requestData" };
char constexpr const* const RequestFutureinterfacePropertyKey{ "requestReplyFutureInterface" };

TrVariantPtr createVariant()
{
    return std::make_shared<tr_variant>();
}

} // namespace

RpcClient::RpcClient(QObject* parent)
    : QObject{ parent }
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
    url_is_loopback_ = QHostAddress{ url_.host() }.isLoopback();
    request_.reset();
}

RpcResponseFuture RpcClient::exec(tr_quark const method, tr_variant* params)
{
    auto const id = getNextId();

    auto map = tr_variant::Map{ 4U };
    map.try_emplace(TR_KEY_id, id);
    map.try_emplace(TR_KEY_jsonrpc, tr_variant::unmanaged_string(JsonRpc::Version));
    map.try_emplace(TR_KEY_method, tr_variant::unmanaged_string(method));
    if (params != nullptr) // if args were passed in, use them
    {
        auto& tgt = map.try_emplace(TR_KEY_params, tr_variant::Map{}).first;
        std::swap(tgt, *params); // TODO(ckerr): tr_variant::Map::extract() and insert()?
    }

    auto req = tr_variant{ std::move(map) };
    req = libtransmission::api_compat::convert_outgoing_data(req);
    return sendRequest(std::make_shared<tr_variant>(std::move(req)), id);
}

int64_t RpcClient::getNextId()
{
    return next_id_++;
}

void RpcClient::sendNetworkRequest(TrVariantPtr req, QFutureInterface<RpcResponse> const& promise)
{
    if (!request_)
    {
        QNetworkRequest request;
        request.setUrl(url_);
        request.setRawHeader(
            "User-Agent",
            (QApplication::applicationName() + QLatin1Char('/') + QString::fromUtf8(LONG_VERSION_STRING)).toUtf8());
        request.setRawHeader("Content-Type", "application/json; charset=UTF-8");
        if (!session_id_.isEmpty())
        {
            request.setRawHeader(TR_RPC_SESSION_ID_HEADER, session_id_.toUtf8());
        }

        request_ = request;
    }

    auto const json_data = QByteArray::fromStdString(tr_variant_serde::json().compact().to_string(*req));
    QNetworkReply* reply = networkAccessManager()->post(*request_, json_data);
    reply->setProperty(RequestDataPropertyKey, QVariant::fromValue(req));
    reply->setProperty(RequestFutureinterfacePropertyKey, QVariant::fromValue(promise));

    connect(reply, &QNetworkReply::downloadProgress, this, &RpcClient::dataReadProgress);
    connect(reply, &QNetworkReply::uploadProgress, this, &RpcClient::dataSendProgress);

    if (verbose_)
    {
        qInfo() << "sending POST " << qPrintable(url_.path());

        for (QByteArray const& b : request_->rawHeaderList())
        {
            qInfo() << b.constData() << ": " << request_->rawHeader(b).constData();
        }

        qInfo() << "Body:";
        qInfo() << json_data.constData();
    }
}

void RpcClient::sendLocalRequest(TrVariantPtr req, QFutureInterface<RpcResponse> const& promise, int64_t const id)
{
    if (verbose_)
    {
        fmt::print("{:s}:{:d} sending req:\n{:s}\n", __FILE__, __LINE__, tr_variant_serde::json().to_string(*req));
    }

    local_requests_.try_emplace(id, promise);
    tr_rpc_request_exec(
        session_,
        *req,
        [this](tr_session* /*sesson*/, tr_variant&& response)
        {
            if (verbose_)
            {
                fmt::print("{:s}:{:d} got response:\n{:s}\n", __FILE__, __LINE__, tr_variant_serde::json().to_string(response));
            }

            TrVariantPtr const resp = createVariant();
            *resp = std::move(response);

            // this callback is invoked in the libtransmission thread, so we don't want
            // to process the response here... let's push it over to the Qt thread.
            QMetaObject::invokeMethod(this, "localRequestFinished", Qt::QueuedConnection, Q_ARG(TrVariantPtr, resp));
        });
}

RpcResponseFuture RpcClient::sendRequest(std::shared_ptr<tr_variant> req, int64_t const id)
{
    auto promise = QFutureInterface<RpcResponse>{};
    promise.setExpectedResultCount(1);
    promise.setProgressRange(0, 1);
    promise.setProgressValue(0);
    promise.reportStarted();

    if (session_ != nullptr)
    {
        sendLocalRequest(req, promise, id);
    }
    else if (!url_.isEmpty())
    {
        sendNetworkRequest(req, promise);
    }

    return promise.future();
}

QNetworkAccessManager* RpcClient::networkAccessManager()
{
    if (nam_ == nullptr)
    {
        nam_ = new QNetworkAccessManager{};

        connect(nam_, &QNetworkAccessManager::finished, this, &RpcClient::networkRequestFinished);

        connect(nam_, &QNetworkAccessManager::authenticationRequired, this, &RpcClient::httpAuthenticationRequired);
    }

    return nam_;
}

void RpcClient::networkRequestFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    auto promise = reply->property(RequestFutureinterfacePropertyKey).value<QFutureInterface<RpcResponse>>();

    if (verbose_)
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
        auto const json_data = reply->readAll().trimmed().toStdString();
        auto const json = createVariant();
        auto result = RpcResponse{};

        if (auto top = tr_variant_serde::json().parse(json_data))
        {
            std::swap(*json, *top);
            result = parseResponseData(*json);
        }

        promise.setProgressValue(1);
        promise.reportFinished(&result);
    }
}

void RpcClient::localRequestFinished(TrVariantPtr response)
{
    if (auto node = local_requests_.extract(parseResponseId(*response)); node)
    {
        auto const result = parseResponseData(*response);

        auto& promise = node.mapped();
        promise.setProgressRange(0, 1);
        promise.setProgressValue(1);
        promise.reportFinished(&result);
    }
}

int64_t RpcClient::parseResponseId(tr_variant& response) const
{
    return dictFind<int>(&response, TR_KEY_tag).value_or(-1);
}

RpcResponse RpcClient::parseResponseData(tr_variant& response) const
{
    auto ret = RpcResponse{};

    if (verbose_)
    {
        fmt::print("{:s}:{:d} raw response:\n{:s}\n", __FILE__, __LINE__, tr_variant_serde::json().to_string(response));
    }

    response = libtransmission::api_compat::convert_incoming_data(response);

    if (verbose_)
    {
        fmt::print("{:s}:{:d} converted response:\n{:s}\n", __FILE__, __LINE__, tr_variant_serde::json().to_string(response));
    }

    if (auto* top = response.get_if<tr_variant::Map>())
    {
        ret.success = true;

        if (auto* error = top->find_if<tr_variant::Map>(TR_KEY_error))
        {
            ret.success = false;

            if (auto const* errmsg = error->find_if<std::string_view>(TR_KEY_message))
            {
                ret.errmsg = QString::fromUtf8(std::data(*errmsg), std::size(*errmsg));
            }
        }

        if (tr_variant* result = tr_variantDictFind(&response, TR_KEY_result))
        {
            ret.result = createVariant();
            std::swap(*ret.result, *result); // TODO(ckerr): tr_variant::Map::extract() & insert()?
            variantInit(result, false);
        }
    }

    return ret;
}
