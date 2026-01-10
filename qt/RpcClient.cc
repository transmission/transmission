// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "RpcClient.h"

#include <QApplication>
#include <QAuthenticator>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVersionNumber>

#include <libtransmission/api-compat.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/transmission.h>
#include <libtransmission/version.h> // LONG_VERSION_STRING

#include "VariantHelpers.h"

using ::trqt::variant_helpers::dictFind;
namespace api_compat = libtransmission::api_compat;

namespace
{

char constexpr const* const RequestBodyKey{ "requestBody" };
char constexpr const* const RequestFutureinterfacePropertyKey{ "requestReplyFutureInterface" };

[[nodiscard]] int64_t nextId()
{
    static int64_t id = {};
    return id++;
}

[[nodiscard]] std::pair<tr_variant, int64_t> buildRequest(tr_quark const method, tr_variant* params)
{
    auto const id = nextId();

    auto req = tr_variant::Map{ 4U };
    req.try_emplace(TR_KEY_jsonrpc, tr_variant::unmanaged_string(JsonRpc::Version));
    req.try_emplace(TR_KEY_method, tr_variant::unmanaged_string(method));
    req.try_emplace(TR_KEY_id, id);
    if (params != nullptr)
    {
        req.try_emplace(TR_KEY_params, params->clone());
    }

    return { std::move(req), id };
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
    network_style_ = DefaultNetworkStyle;

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
}

RpcResponseFuture RpcClient::exec(tr_quark const method, tr_variant* args)
{
    auto [req, id] = buildRequest(method, args);

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
        api_compat::convert(req, network_style_);
        auto const json = tr_variant_serde::json().compact().to_string(req);
        auto const body = QByteArray::fromStdString(json);
        sendNetworkRequest(body, promise);
    }

    return promise.future();
}

void RpcClient::sendNetworkRequest(QByteArray const& body, QFutureInterface<RpcResponse> const& promise)
{
    auto req = QNetworkRequest{};
    req.setUrl(url_);
    req.setRawHeader("User-Agent", "Transmisson/" SHORT_VERSION_STRING);
    if (!session_id_.isEmpty())
    {
        req.setRawHeader(TR_RPC_SESSION_ID_HEADER, session_id_);
    }

    if (verbose_)
    {
        qInfo() << "sending POST " << qPrintable(url_.path());

        for (QByteArray const& name : req.rawHeaderList())
        {
            qInfo() << name.constData() << ": " << req.rawHeader(name).constData();
        }

        qInfo() << "Body:";
        qInfo() << body.constData();
    }

    if (QNetworkReply* reply = networkAccessManager()->post(req, body))
    {
        reply->setProperty(RequestBodyKey, body);
        reply->setProperty(RequestFutureinterfacePropertyKey, QVariant::fromValue(promise));

        connect(reply, &QNetworkReply::downloadProgress, this, &RpcClient::dataReadProgress);
        connect(reply, &QNetworkReply::uploadProgress, this, &RpcClient::dataSendProgress);
    }
}

void RpcClient::sendLocalRequest(tr_variant& req, QFutureInterface<RpcResponse> const& promise, int64_t const id)
{
    if (verbose_)
    {
        fmt::print("{:s}:{:d} sending req:\n{:s}\n", __FILE__, __LINE__, tr_variant_serde::json().to_string(req));
    }

    local_requests_.try_emplace(id, promise);
    tr_rpc_request_exec(
        session_,
        req,
        [this](tr_session* /*session*/, tr_variant&& response)
        {
            api_compat::convert_incoming_data(response);

            if (verbose_)
            {
                auto serde = tr_variant_serde::json();
                serde.compact();
                fmt::print("{:s}:{:d} got response:\n{:s}\n", __FILE__, __LINE__, serde.to_string(response));
            }

            // this callback is invoked in the libtransmission thread, so we don't want
            // to process the response here... let's push it over to the Qt thread.
            auto shared = std::make_shared<tr_variant>(std::move(response));
            QMetaObject::invokeMethod(this, "localRequestFinished", Qt::QueuedConnection, Q_ARG(TrVariantPtr, shared));
        });
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
    }

    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 409 &&
        reply->hasRawHeader(TR_RPC_SESSION_ID_HEADER))
    {
        // we got a 409 telling us our session id has expired.
        // update it and resubmit the request.

        auto version_str = QString::fromUtf8("unknown");

        if (reply->hasRawHeader(TR_RPC_RPC_VERSION_HEADER))
        {
            network_style_ = api_compat::Style::Tr5;

            version_str = QString::fromUtf8(reply->rawHeader(TR_RPC_RPC_VERSION_HEADER));
            if (QVersionNumber::fromString(version_str).majorVersion() > TrRpcVersionSemverMajor)
            {
                fmt::print(
                    stderr,
                    "Server '{:s}' RPC version is {:s}, which may be incompatible with our version {:s}.\n",
                    url_.toDisplayString().toStdString(),
                    version_str.toStdString(),
                    TrRpcVersionSemver);
            }
        }
        else
        {
            network_style_ = api_compat::Style::Tr4;
        }

        if (verbose_)
        {
            fmt::print(
                "Server '{:s}' RPC version is {:s}. Using style {:d}\n",
                url_.toDisplayString().toStdString(),
                version_str.toStdString(),
                static_cast<int>(network_style_));
        }

        session_id_ = reply->rawHeader(TR_RPC_SESSION_ID_HEADER);
        sendNetworkRequest(reply->property(RequestBodyKey).toByteArray(), promise);
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
        auto const json = reply->readAll().trimmed().toStdString();

        if (verbose_)
        {
            fmt::print("{:s}:{:d} got raw response:\n{:s}\n", __FILE__, __LINE__, json);
        }

        auto response = RpcResponse{};

        if (auto var = tr_variant_serde::json().parse(json))
        {
            api_compat::convert_incoming_data(*var);

            if (verbose_)
            {
                auto serde = tr_variant_serde::json();
                serde.compact();
                fmt::print("{:s}:{:d} compat response:\n{:s}\n", __FILE__, __LINE__, serde.to_string(*var));
            }

            response = parseResponseData(*var);
        }

        promise.setProgressValue(1);
        promise.reportFinished(&response);
    }
}

void RpcClient::localRequestFinished(TrVariantPtr response)
{
    if (auto node = local_requests_.extract(parseResponseId(*response)))
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
    return dictFind<int>(&response, TR_KEY_id).value_or(-1);
}

RpcResponse RpcClient::parseResponseData(tr_variant& response) const
{
    auto ret = RpcResponse{};

    ret.success = false;
    ret.errmsg = QStringLiteral("unknown error");

    if (auto* response_map = response.get_if<tr_variant::Map>())
    {
        if (auto* result = response_map->find_if<tr_variant::Map>(TR_KEY_result))
        {
            ret.success = true;
            ret.errmsg.clear();
            ret.args = std::make_shared<tr_variant>(std::move(*result));
        }

        if (auto* error_map = response_map->find_if<tr_variant::Map>(TR_KEY_error))
        {
            if (auto const errmsg = error_map->value_if<std::string_view>(TR_KEY_message))
            {
                ret.errmsg = QString::fromUtf8(std::data(*errmsg), std::size(*errmsg));
            }
        }
    }

    return ret;
}
