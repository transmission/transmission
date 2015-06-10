/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
 #include <QDesktopServices>
#else
 #include <QStandardPaths>
#endif

#include "FaviconCache.h"

/***
****
***/

FaviconCache::FaviconCache ()
{
  myNAM = new QNetworkAccessManager ();
  connect (myNAM, SIGNAL(finished(QNetworkReply*)), this, SLOT(onRequestFinished(QNetworkReply*)));
}

FaviconCache::~FaviconCache ()
{
  delete myNAM;
}

/***
****
***/

QString
FaviconCache::getCacheDir ()
{
  const QString base =
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    QDesktopServices::storageLocation (QDesktopServices::CacheLocation);
#else
    QStandardPaths::writableLocation (QStandardPaths::CacheLocation);
#endif

  return QDir(base).absoluteFilePath (QLatin1String ("favicons"));
}

void
FaviconCache::ensureCacheDirHasBeenScanned ()
{
  static bool hasBeenScanned = false;

  if (!hasBeenScanned)
    {
      hasBeenScanned = true;

      QDir cacheDir (getCacheDir ());
      cacheDir.mkpath (cacheDir.absolutePath ());

      QStringList files = cacheDir.entryList (QDir::Files|QDir::Readable);
      for (const QString& file: files)
        {
          QPixmap pixmap;
          pixmap.load (cacheDir.absoluteFilePath (file));
          if (!pixmap.isNull ())
            myPixmaps.insert (file, pixmap);
        }
    }
}

QString
FaviconCache::getHost (const QUrl& url)
{
  QString host = url.host ();
  const int first_dot = host.indexOf (QLatin1Char ('.'));
  const int last_dot = host.lastIndexOf (QLatin1Char ('.'));

  if ((first_dot != -1) && (last_dot != -1) &&  (first_dot != last_dot))
    host.remove (0, first_dot + 1);

  return host;
}

QSize
FaviconCache::getIconSize ()
{
  return QSize (16, 16);
}

QPixmap
FaviconCache::find (const QUrl& url)
{
  return findFromHost (getHost (url));
}

QPixmap
FaviconCache::findFromHost (const QString& host)
{
  ensureCacheDirHasBeenScanned ();

  const QPixmap pixmap = myPixmaps[host];
  const QSize rightSize = getIconSize ();
  return pixmap.isNull () || pixmap.size () == rightSize ? pixmap : pixmap.scaled (rightSize);
}

void
FaviconCache::add (const QUrl& url)
{
  ensureCacheDirHasBeenScanned ();

  const QString host = getHost (url);

  if (!myPixmaps.contains (host))
    {
      // add a placholder s.t. we only ping the server once per session
      myPixmaps.insert (host, QPixmap ());

      // try to download the favicon
      const QString path = QLatin1String ("http://") + host + QLatin1String ("/favicon.");
      QStringList suffixes;
      suffixes << QLatin1String ("ico") << QLatin1String ("png") << QLatin1String ("gif") << QLatin1String ("jpg");
      for (const QString& suffix: suffixes)
        myNAM->get (QNetworkRequest (path + suffix));
    }
}

void
FaviconCache::onRequestFinished (QNetworkReply * reply)
{
  const QString host = reply->url().host();

  QPixmap pixmap;

  const QByteArray content = reply->readAll ();
  if (!reply->error ())
    pixmap.loadFromData (content);

  if (!pixmap.isNull ())
    {
      // save it in memory...
      myPixmaps.insert (host, pixmap);

      // save it on disk...
      QDir cacheDir (getCacheDir ());
      cacheDir.mkpath (cacheDir.absolutePath ());
      QFile file (cacheDir.absoluteFilePath (host));
      file.open (QIODevice::WriteOnly);
      file.write (content);
      file.close ();

      // notify listeners
      emit pixmapReady (host);
    }
}
