/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

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

namespace
{
  void
  destroyVariant (tr_variant * json)
  {
    tr_variantFree (json);
    tr_free (json);
  }

  TrVariantPtr
  createVariant ()
  {
    return TrVariantPtr (tr_new0 (tr_variant, 1), &destroyVariant);
  }
}

RpcClient::RpcClient (QObject * parent):
  QObject (parent),
  mySession (nullptr),
  myNAM (nullptr)
{
  qRegisterMetaType<TrVariantPtr> ("TrVariantPtr");

  connect (this, SIGNAL (responseReceived (TrVariantPtr)),
           this, SLOT (parseResponse (TrVariantPtr)));
}

void
RpcClient::stop ()
{
  mySession = nullptr;
  mySessionId.clear ();
  myUrl.clear ();

  if (myNAM != nullptr)
    {
      myNAM->deleteLater ();
      myNAM = nullptr;
    }
}

void
RpcClient::start (tr_session * session)
{
  mySession = session;
}

void
RpcClient::start (const QUrl& url)
{
  myUrl = url;
}

bool
RpcClient::isLocal () const
{
  if (mySession != 0)
    return true;

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
  if (myUrl.host () == QLatin1String ("127.0.0.1") ||
      myUrl.host ().compare (QLatin1String ("localhost"), Qt::CaseInsensitive) == 0)
    return true;
#else
  if (QHostAddress (myUrl.host ()).isLoopback ())
    return true;
#endif

  return false;
}

const QUrl&
RpcClient::url () const
{
  return myUrl;
}

void
RpcClient::exec (tr_quark method, tr_variant * args, int64_t tag)
{
  exec (tr_quark_get_string (method, nullptr), args, tag);
}

void
RpcClient::exec (const char* method, tr_variant * args, int64_t tag)
{
  TrVariantPtr json = createVariant ();
  tr_variantInitDict (json.get (), 3);
  tr_variantDictAddStr (json.get (), TR_KEY_method, method);
  if (tag >= 0)
    tr_variantDictAddInt (json.get (), TR_KEY_tag, tag);
  if (args != nullptr)
    tr_variantDictSteal (json.get (), TR_KEY_arguments, args);

  sendRequest (json);
}

void
RpcClient::sendRequest (TrVariantPtr json)
{
  if (mySession != nullptr)
    {
      tr_rpc_request_exec_json (mySession, json.get (), localSessionCallback, this);
    }
  else if (!myUrl.isEmpty ())
    {
      QNetworkRequest request;
      request.setUrl (myUrl);
      request.setRawHeader ("User-Agent", (qApp->applicationName () + QLatin1Char ('/') + QString::fromUtf8 (LONG_VERSION_STRING)).toUtf8 ());
      request.setRawHeader ("Content-Type", "application/json; charset=UTF-8");

      if (!mySessionId.isEmpty ())
        request.setRawHeader (TR_RPC_SESSION_ID_HEADER, mySessionId.toUtf8 ());

      size_t rawJsonDataLength;
      char * rawJsonData = tr_variantToStr (json.get (), TR_VARIANT_FMT_JSON_LEAN, &rawJsonDataLength);
      QByteArray jsonData (rawJsonData, rawJsonDataLength);
      tr_free (rawJsonData);

      QNetworkReply * reply = networkAccessManager ()->post (request, jsonData);
      reply->setProperty (REQUEST_DATA_PROPERTY_KEY, QVariant::fromValue (json));
      connect (reply, SIGNAL (downloadProgress (qint64, qint64)), this, SIGNAL (dataReadProgress ()));
      connect (reply, SIGNAL (uploadProgress (qint64, qint64)), this, SIGNAL (dataSendProgress ()));
      connect (reply, SIGNAL (error (QNetworkReply::NetworkError)), this, SIGNAL (error (QNetworkReply::NetworkError)));

#ifdef DEBUG_HTTP
      std::cerr << "sending " << "POST " << qPrintable (myUrl.path ()) << std::endl;
      for (const QByteArray& b: request.rawHeaderList ())
        std::cerr << b.constData ()
                  << ": "
                  << request.rawHeader (b).constData ()
                  << std::endl;
      std::cerr << "Body:\n" << jsonData.constData () << std::endl;
#endif
    }
}

QNetworkAccessManager *
RpcClient::networkAccessManager ()
{
  if (myNAM == 0)
    {
      myNAM = new QNetworkAccessManager ();

      connect (myNAM, SIGNAL (finished (QNetworkReply *)),
               this, SLOT (onFinished (QNetworkReply *)));

      connect (myNAM, SIGNAL (authenticationRequired (QNetworkReply *,QAuthenticator *)),
               this, SIGNAL (httpAuthenticationRequired ()));
    }

  return myNAM;
}

void
RpcClient::localSessionCallback (tr_session * s, tr_variant * response, void * vself)
{
  Q_UNUSED (s);

  RpcClient * self = static_cast<RpcClient*> (vself);

  TrVariantPtr json = createVariant ();
  *json = *response;
  tr_variantInitBool (response, false);

  // this callback is invoked in the libtransmission thread, so we don't want
  // to process the response here... let's push it over to the Qt thread.
  self->responseReceived (json);
}

void
RpcClient::onFinished (QNetworkReply * reply)
{
#ifdef DEBUG_HTTP
  std::cerr << "http response header: " << std::endl;
  for (const QByteArray& b: reply->rawHeaderList ())
    std::cerr << b.constData ()
              << ": "
              << reply->rawHeader (b).constData ()
              << std::endl;
  std::cerr << "json:\n" << reply->peek (reply->bytesAvailable ()).constData () << std::endl;
#endif

  if (reply->attribute (QNetworkRequest::HttpStatusCodeAttribute).toInt () == 409 &&
      reply->hasRawHeader (TR_RPC_SESSION_ID_HEADER))
    {
      // we got a 409 telling us our session id has expired.
      // update it and resubmit the request.
      mySessionId = QString::fromUtf8 (reply->rawHeader (TR_RPC_SESSION_ID_HEADER));
      sendRequest (reply->property (REQUEST_DATA_PROPERTY_KEY).value<TrVariantPtr> ());
    }
  else if (reply->error () != QNetworkReply::NoError)
    {
      emit errorMessage (reply->errorString ());
    }
  else
    {
      const QByteArray jsonData = reply->readAll ().trimmed ();

      TrVariantPtr json = createVariant ();
      if (tr_variantFromJson (json.get (), jsonData.constData (), jsonData.size ()) == 0)
        parseResponse (json);

      emit error (QNetworkReply::NoError);
    }

  reply->deleteLater ();
}

void
RpcClient::parseResponse (TrVariantPtr json)
{
  int64_t tag;
  if (!tr_variantDictFindInt (json.get (), TR_KEY_tag, &tag))
    tag = -1;

  const char * result;
  if (!tr_variantDictFindStr (json.get (), TR_KEY_result, &result, nullptr))
    result = nullptr;

  tr_variant * args;
  if (!tr_variantDictFindDict (json.get (), TR_KEY_arguments, &args))
    args = nullptr;

  emit executed (tag, result == nullptr ? QString () : QString::fromUtf8 (result), args);
}
