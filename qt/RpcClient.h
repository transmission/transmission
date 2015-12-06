/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_RPC_CLIENT_H
#define QTR_RPC_CLIENT_H

#include <memory>

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

    void exec (tr_quark method, tr_variant * args, int64_t tag = -1);
    void exec (const char* method, tr_variant * args, int64_t tag = -1);

  signals:
    void httpAuthenticationRequired ();
    void dataReadProgress ();
    void dataSendProgress ();
    void error (QNetworkReply::NetworkError code);
    void errorMessage (const QString& message);
    void executed (int64_t tag, const QString& result, tr_variant * args);

    // private
    void responseReceived (TrVariantPtr json);

  private:
    void sendRequest (TrVariantPtr json);
    QNetworkAccessManager * networkAccessManager ();

    static void localSessionCallback (tr_session * s, tr_variant * response, void * vself);

  private slots:
    void onFinished (QNetworkReply * reply);
    void parseResponse (TrVariantPtr json);

  private:
    tr_session * mySession;
    QString mySessionId;
    QUrl myUrl;
    QNetworkAccessManager * myNAM;
};

#endif // QTR_RPC_CLIENT_H
