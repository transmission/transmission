/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#pragma once

#include <memory>

#include <QFuture>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

class QByteArray;
class QNetworkAccessManager;

typedef std::shared_ptr<tr_variant> TrVariantPtr;
Q_DECLARE_METATYPE (TrVariantPtr)

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

Q_DECLARE_METATYPE (QFutureInterface<RpcResponse>);

class RpcClient: public QObject
{
    Q_OBJECT

  public:
    RpcClient (QObject * parent = nullptr);
    virtual ~RpcClient () {}

    void stop ();
    void start (tr_session * session);
    void start (const QUrl& url);

    bool isLocal () const;
    const QUrl& url () const;

    QFuture<RpcResponse> exec (tr_quark method, tr_variant * args);
    QFuture<RpcResponse> exec (const char* method, tr_variant * args);

  signals:
    void httpAuthenticationRequired ();
    void dataReadProgress ();
    void dataSendProgress ();
    void networkResponse (QNetworkReply::NetworkError code, const QString& message);

  private:
    QFuture<RpcResponse> sendRequest (TrVariantPtr json);
    QNetworkAccessManager * networkAccessManager ();
    int64_t getNextTag ();

    void sendRequestNetwork (TrVariantPtr json, const QFutureInterface<RpcResponse> & promise);
    int64_t parseResponseTag (TrVariantPtr response);
    RpcResponse parseResponseData (TrVariantPtr response);

    static void localSessionCallback (tr_session * s, tr_variant * response, void * vself);

  private slots:
    void requestFinishedNetwork (QNetworkReply * reply);
    void requestFinishedLocal (TrVariantPtr response);

  private:
    tr_session * mySession;
    QString mySessionId;
    QUrl myUrl;
    QNetworkAccessManager * myNAM;
    QHash<int64_t, QFutureInterface<RpcResponse>> myLocalRequests;
    int64_t myNextTag;
};

