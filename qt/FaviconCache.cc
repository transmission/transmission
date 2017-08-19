/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

#include "FaviconCache.h"

/***
****
***/

FaviconCache::FaviconCache()
{
    myNAM = new QNetworkAccessManager();
    connect(myNAM, SIGNAL(finished(QNetworkReply*)), this, SLOT(onRequestFinished(QNetworkReply*)));
}

FaviconCache::~FaviconCache()
{
    delete myNAM;
}

/***
****
***/

QString FaviconCache::getCacheDir()
{
    QString const base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    return QDir(base).absoluteFilePath(QLatin1String("favicons"));
}

void FaviconCache::ensureCacheDirHasBeenScanned()
{
    static bool hasBeenScanned = false;

    if (!hasBeenScanned)
    {
        hasBeenScanned = true;

        QDir cacheDir(getCacheDir());
        cacheDir.mkpath(cacheDir.absolutePath());

        QStringList files = cacheDir.entryList(QDir::Files | QDir::Readable);

        for (QString const& file : files)
        {
            QPixmap pixmap;
            pixmap.load(cacheDir.absoluteFilePath(file));

            if (!pixmap.isNull())
            {
                myPixmaps.insert(file, pixmap);
            }
        }
    }
}

QString FaviconCache::getHost(QUrl const& url)
{
    QString host = url.host();
    int const first_dot = host.indexOf(QLatin1Char('.'));
    int const last_dot = host.lastIndexOf(QLatin1Char('.'));

    if (first_dot != -1 && last_dot != -1 && first_dot != last_dot)
    {
        host.remove(0, first_dot + 1);
    }

    return host;
}

QSize FaviconCache::getIconSize()
{
    return QSize(16, 16);
}

QPixmap FaviconCache::find(QUrl const& url)
{
    return findFromHost(getHost(url));
}

QPixmap FaviconCache::findFromHost(QString const& host)
{
    ensureCacheDirHasBeenScanned();

    return myPixmaps[host];
}

void FaviconCache::add(QUrl const& url)
{
    ensureCacheDirHasBeenScanned();

    QString const host = getHost(url);

    if (!myPixmaps.contains(host))
    {
        // add a placholder s.t. we only ping the server once per session
        myPixmaps.insert(host, QPixmap());

        // try to download the favicon
        QString const path = QLatin1String("http://") + host + QLatin1String("/favicon.");
        QStringList suffixes;
        suffixes << QLatin1String("ico") << QLatin1String("png") << QLatin1String("gif") << QLatin1String("jpg");

        for (QString const& suffix : suffixes)
        {
            myNAM->get(QNetworkRequest(path + suffix));
        }
    }
}

void FaviconCache::onRequestFinished(QNetworkReply* reply)
{
    QString const host = reply->url().host();

    QPixmap pixmap;

    QByteArray const content = reply->readAll();

    if (reply->error() == QNetworkReply::NoError)
    {
        pixmap.loadFromData(content);
    }

    if (!pixmap.isNull())
    {
        // save it in memory...
        myPixmaps.insert(host, pixmap);

        // save it on disk...
        QDir cacheDir(getCacheDir());
        cacheDir.mkpath(cacheDir.absolutePath());
        QFile file(cacheDir.absoluteFilePath(host));
        file.open(QIODevice::WriteOnly);
        file.write(content);
        file.close();

        // notify listeners
        emit pixmapReady(host);
    }
}
