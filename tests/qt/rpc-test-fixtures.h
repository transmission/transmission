// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include <QByteArray>
#include <QFutureInterface>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVariant>
#include <QVector>

#include "RpcClient.h"

template<typename String>
[[nodiscard]] QByteArray toQBA(String const& str)
{
    auto const sv = std::string_view{ str };
    return { sv.data(),
             static_cast<
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                 int
#else
                 qsizetype
#endif
                 >(sv.size()) };
}

class FakeReply final : public QNetworkReply
{
public:
    [[nodiscard]] static FakeReply* newPostReply(QUrl const& url, QObject* parent = nullptr)
    {
        return newPostReply(QNetworkRequest{ url }, parent);
    }

    [[nodiscard]] static FakeReply* newPostReply(QNetworkRequest const& req, QObject* parent = nullptr)
    {
        auto reply = new FakeReply{ QNetworkAccessManager::PostOperation, req, parent };

        // networkRequestFinished expects these properties to exist.
        auto promise = QFutureInterface<RpcResponse>{};
        promise.reportStarted();
        reply->setProperty("requestReplyFutureInterface", QVariant::fromValue(promise));
        reply->setProperty("requestBody", QByteArray{ "{}" });

        return reply;
    }

    explicit FakeReply(QNetworkAccessManager::Operation op, QNetworkRequest const& req, QObject* parent = nullptr)
        : QNetworkReply{ parent }
    {
        setOperation(op);
        setRequest(req);
        setUrl(req.url());
        open(QIODevice::ReadOnly);
    }

    void setHttpStatus(int const code)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, code);
    }

    template<typename StringA, typename StringB>
    void addRawHeader(StringA const& name, StringB const& value)
    {
        setRawHeader(toQBA(name), toQBA(value));
    }

    void abort() override
    {
    }

protected:
    qint64 readData(char* /*data*/, qint64 /*maxSize*/) override
    {
        return 0;
    }
};

class FakeNetworkAccessManager final : public QNetworkAccessManager
{
public:
    int create_count = 0;
    QNetworkAccessManager::Operation last_operation = QNetworkAccessManager::UnknownOperation;
    QNetworkRequest last_request;
    QByteArray last_body;
    QVector<QNetworkAccessManager::Operation> operations;
    QVector<QNetworkRequest> requests;
    QVector<QByteArray> request_bodies;

protected:
    QNetworkReply* createRequest(Operation op, QNetworkRequest const& req, QIODevice* outgoing_data) override
    {
        ++create_count;
        last_operation = op;
        last_request = req;
        operations.push_back(op);
        requests.push_back(req);

        if (outgoing_data != nullptr)
        {
            last_body = outgoing_data->readAll();
            outgoing_data->seek(0);
            request_bodies.push_back(last_body);
        }
        else
        {
            request_bodies.push_back({});
        }

        return new FakeReply{ op, req, this };
    }
};
