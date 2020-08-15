/*
 * This file Copyright (C) 2014-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <QFuture>
#include <QFutureInterface>
#include <QHash>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "Macros.h"

class QByteArray;
class QNetworkAccessManager;

using TrVariantPtr = std::shared_ptr<tr_variant>;
Q_DECLARE_METATYPE(TrVariantPtr)

extern "C"
{
struct evbuffer;
struct tr_session;
}

struct RpcResponse
{
    QString result;
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
    TR_DISABLE_COPY_MOVE(RpcClient)

public:
    RpcClient(QObject* parent = nullptr);

    void stop();
    void start(tr_session* session);
    void start(QUrl const& url);

    bool isLocal() const;
    QUrl const& url() const;

    RpcResponseFuture exec(tr_quark method, tr_variant* args);
    RpcResponseFuture exec(std::string_view method, tr_variant* args);

signals:
    void httpAuthenticationRequired();
    void dataReadProgress();
    void dataSendProgress();
    void networkResponse(QNetworkReply::NetworkError code, QString const& message);

private slots:
    void networkRequestFinished(QNetworkReply* reply);
    void localRequestFinished(TrVariantPtr response);

private:
    RpcResponseFuture sendRequest(TrVariantPtr json);
    QNetworkAccessManager* networkAccessManager();
    int64_t getNextTag();

    void sendNetworkRequest(TrVariantPtr json, QFutureInterface<RpcResponse> const& promise);
    void sendLocalRequest(TrVariantPtr json, QFutureInterface<RpcResponse> const& promise, int64_t tag);
    int64_t parseResponseTag(tr_variant& response);
    RpcResponse parseResponseData(tr_variant& response);

    static void localSessionCallback(tr_session* s, tr_variant* response, void* vself) noexcept;

    std::optional<QNetworkRequest> request_;

    tr_session* session_ = {};
    QString session_id_;
    QUrl url_;
    QNetworkAccessManager* nam_ = {};
    QHash<int64_t, QFutureInterface<RpcResponse>> local_requests_;
    int64_t next_tag_ = {};
};
