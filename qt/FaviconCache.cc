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

namespace
{

QPixmap scale(QPixmap pixmap)
{
    return pixmap.scaled(FaviconCache::getIconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

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
                myPixmaps[file] = scale(pixmap);
            }
        }
    }
}

/***
****
***/

QString FaviconCache::getDisplayName(QString const& key)
{
    auto name = key;
    name[0] = name.at(0).toTitleCase();
    return name;
}

QString FaviconCache::getKey(QUrl const& url)
{
    auto host = url.host();

    // remove tld
    auto const suffix = url.topLevelDomain();
    host.truncate(host.size() - suffix.size());

    // remove subdomain
    auto const pos = host.indexOf(QLatin1Char('.'));
    return pos < 0 ? host : host.remove(0, pos + 1);
}

QString FaviconCache::getKey(QString const& displayName)
{
    return displayName.toLower();
}

QSize FaviconCache::getIconSize()
{
    return QSize(16, 16);
}

QPixmap FaviconCache::find(QString const& key)
{
    ensureCacheDirHasBeenScanned();

    return myPixmaps[key];
}

QString FaviconCache::add(QUrl const& url)
{
    ensureCacheDirHasBeenScanned();

    QString const key = getKey(url);

    if (myPixmaps.count(key) == 0)
    {
        // add a placholder s.t. we only ping the server once per session
        myPixmaps[key] = QPixmap();

        // try to download the favicon
        QString const path = QLatin1String("http://") + url.host() + QLatin1String("/favicon.");
        QStringList suffixes;
        suffixes << QLatin1String("ico") << QLatin1String("png") << QLatin1String("gif") << QLatin1String("jpg");

        for (QString const& suffix : suffixes)
        {
            myNAM->get(QNetworkRequest(path + suffix));
        }
    }

    return key;
}

void FaviconCache::onRequestFinished(QNetworkReply* reply)
{
    auto const key = getKey(reply->url());

    QPixmap pixmap;

    QByteArray const content = reply->readAll();

    if (reply->error() == QNetworkReply::NoError)
    {
        pixmap.loadFromData(content);
    }

    if (!pixmap.isNull())
    {
        // save it in memory...
        myPixmaps[key] = scale(pixmap);

        // save it on disk...
        QDir cacheDir(getCacheDir());
        cacheDir.mkpath(cacheDir.absolutePath());
        QFile file(cacheDir.absoluteFilePath(key));
        file.open(QIODevice::WriteOnly);
        file.write(content);
        file.close();

        // notify listeners
        emit pixmapReady(key);
    }
}
