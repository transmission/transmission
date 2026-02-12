// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int64_t
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <QFuture>
#include <QFutureInterface>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

#include "lib/base/quark.h"
#include "lib/base/variant.h"

#include "lib/transmission/transmission.h"
#include "lib/transmission/api-compat.h"

class QNetworkAccessManager;

using TrVariantPtr = std::shared_ptr<tr_variant>;
Q_DECLARE_METATYPE(TrVariantPtr)

extern "C"
{
    struct tr_session;
}

struct RpcResponse
{
    QString errmsg;
    TrVariantPtr args;
    bool success = false;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
};

Q_DECLARE_METATYPE(QFutureInterface<RpcResponse>)

// The response future -- the RPC engine returns one for each request made.
using RpcResponseFuture = QFuture<RpcResponse>;

class RpcClient : public QObject
{
    Q_OBJECT

public:
    explicit RpcClient(QNetworkAccessManager& nam, QObject* parent = nullptr);
    RpcClient(RpcClient&&) = delete;
    RpcClient(RpcClient const&) = delete;
    RpcClient& operator=(RpcClient&&) = delete;
    RpcClient& operator=(RpcClient const&) = delete;

    [[nodiscard]] constexpr auto const& url() const noexcept
    {
        return url_;
    }

    [[nodiscard]] constexpr auto isLocal() const noexcept
    {
        return session_ != nullptr || url_is_loopback_;
    }

    void stop();
    void start(tr_session* session);
    void start(QUrl const& url);

    RpcResponseFuture exec(tr_quark method, tr_variant* args);

signals:
    void httpAuthenticationRequired();
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(QNetworkReply::NetworkError code, QString const& message);

private slots:
    void networkRequestFinished(QNetworkReply* reply);
    void localRequestFinished(TrVariantPtr response);

private:
    QByteArray const SessionIdHeaderName = QByteArray{ TrRpcSessionIdHeader.data(),
                                                       static_cast<qsizetype>(TrRpcSessionIdHeader.size()) };
    QByteArray const VersionHeaderName = QByteArray{ TrRpcVersionHeader.data(),
                                                     static_cast<qsizetype>(TrRpcVersionHeader.size()) };

    void connectNetworkAccessManager();

    void sendNetworkRequest(QByteArray const& body, QFutureInterface<RpcResponse> const& promise);
    void sendLocalRequest(tr_variant& req, QFutureInterface<RpcResponse> const& promise, int64_t id);
    [[nodiscard]] int64_t parseResponseId(tr_variant& response) const;
    [[nodiscard]] RpcResponse parseResponseData(tr_variant& response) const;

    tr::api_compat::Style network_style_ = tr::api_compat::default_style();
    tr_session* session_ = {};
    QByteArray session_id_;
    QUrl url_;
    QNetworkAccessManager* const nam_;
    std::unordered_map<int64_t, QFutureInterface<RpcResponse>> local_requests_;
    bool const verbose_ = qEnvironmentVariableIsSet("TR_RPC_VERBOSE");
    bool url_is_loopback_ = false;
};
