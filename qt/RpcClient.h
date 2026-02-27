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

#include <libtransmission/transmission.h>

#include <libtransmission/api-compat.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

class QNetworkAccessManager;

using TrVariantPtr = std::shared_ptr<tr_variant>;
Q_DECLARE_METATYPE(TrVariantPtr)

struct tr_session;

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
    ~RpcClient() override = default;
    RpcClient(RpcClient&&) = delete;
    RpcClient(RpcClient const&) = delete;
    RpcClient& operator=(RpcClient&&) = delete;
    RpcClient& operator=(RpcClient const&) = delete;

    [[nodiscard]] constexpr auto const& url() const noexcept
    {
        return url_;
    }

    [[nodiscard]] constexpr auto is_local() const noexcept
    {
        return session_ != nullptr || url_is_loopback_;
    }

    void stop();
    void start(tr_session* session);
    void start(QUrl const& url);

    RpcResponseFuture exec(tr_quark method, tr_variant* args);

signals:
    void http_authentication_required();
    void data_read_progress();
    void data_send_progress();
    void network_response(QNetworkReply::NetworkError code, QString const& message);

private slots:
    void network_request_finished(QNetworkReply* reply);
    void local_request_finished(TrVariantPtr response);

private:
    static inline QByteArray const SessionIdHeaderName = { TrRpcSessionIdHeader.data(),
                                                           static_cast<qsizetype>(TrRpcSessionIdHeader.size()) };
    static inline QByteArray const VersionHeaderName = { TrRpcVersionHeader.data(),
                                                         static_cast<qsizetype>(TrRpcVersionHeader.size()) };

    void connect_network_access_manager();

    void send_network_request(QByteArray const& body, QFutureInterface<RpcResponse> const& promise);
    void send_local_request(tr_variant& req, QFutureInterface<RpcResponse> const& promise, int64_t id);
    [[nodiscard]] int64_t parse_response_id(tr_variant& response) const;
    [[nodiscard]] RpcResponse parse_response_data(tr_variant& response) const;

    tr::api_compat::Style network_style_ = tr::api_compat::default_style();
    tr_session* session_ = {};
    QByteArray session_id_;
    QUrl url_;
    QNetworkAccessManager* const nam_;
    std::unordered_map<int64_t, QFutureInterface<RpcResponse>> local_requests_;
    bool const verbose_ = qEnvironmentVariableIsSet("TR_RPC_VERBOSE");
    bool url_is_loopback_ = false;
};
