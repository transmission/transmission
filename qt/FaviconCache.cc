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

FaviconCache::FaviconCache() :
    nam_(new QNetworkAccessManager(this))
{
    connect(nam_, SIGNAL(finished(QNetworkReply*)), this, SLOT(onRequestFinished(QNetworkReply*)));
}

/***
****
***/

QString FaviconCache::getCacheDir()
{
    QString const base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    return QDir(base).absoluteFilePath(QStringLiteral("favicons"));
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
    static bool has_been_scanned = false;

    if (!has_been_scanned)
    {
        has_been_scanned = true;

        QDir cache_dir(getCacheDir());
        cache_dir.mkpath(cache_dir.absolutePath());

        QStringList files = cache_dir.entryList(QDir::Files | QDir::Readable);

        for (QString const& file : files)
        {
            QPixmap pixmap;
            pixmap.load(cache_dir.absoluteFilePath(file));

            if (!pixmap.isNull())
            {
                pixmaps_[file] = scale(pixmap);
            }
        }
    }
}

/***
****
***/

QString FaviconCache::getDisplayName(Key const& key)
{
    auto name = key;
    name[0] = name.at(0).toTitleCase();
    return name;
}

FaviconCache::Key FaviconCache::getKey(QUrl const& url)
{
    auto host = url.host();

    // remove tld
    auto const suffix = url.topLevelDomain();
    host.truncate(host.size() - suffix.size());

    // remove subdomain
    auto const pos = host.indexOf(QLatin1Char('.'));
    return pos < 0 ? host : host.remove(0, pos + 1);
}

FaviconCache::Key FaviconCache::getKey(QString const& displayName)
{
    return displayName.toLower();
}

QSize FaviconCache::getIconSize()
{
    return QSize(16, 16);
}

QPixmap FaviconCache::find(Key const& key)
{
    ensureCacheDirHasBeenScanned();

    return pixmaps_[key];
}

FaviconCache::Key FaviconCache::add(QString const& url_str)
{
    ensureCacheDirHasBeenScanned();

    // find or add this url's key
    auto k_it = keys_.find(url_str);
    if (k_it != keys_.end())
    {
        return k_it->second;
    }
    auto const url = QUrl { url_str };
    auto const key = getKey(url);
    keys_.insert({ url_str, key });

    // Try to download a favicon if we don't have one.
    // Add a placeholder to prevent repeat downloads.
    if (pixmaps_.try_emplace(key).second)
    {
        QString const path = QStringLiteral("http://") + url.host() + QStringLiteral("/favicon.");
        nam_->get(QNetworkRequest(path + QStringLiteral("gif")));
        nam_->get(QNetworkRequest(path + QStringLiteral("ico")));
        nam_->get(QNetworkRequest(path + QStringLiteral("jpg")));
        nam_->get(QNetworkRequest(path + QStringLiteral("png")));
        nam_->get(QNetworkRequest(path + QStringLiteral("svg")));
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
        pixmaps_[key] = scale(pixmap);

        // save it on disk...
        QDir cache_dir(getCacheDir());
        cache_dir.mkpath(cache_dir.absolutePath());
        QFile file(cache_dir.absoluteFilePath(key));
        file.open(QIODevice::WriteOnly);
        file.write(content);
        file.close();

        // notify listeners
        emit pixmapReady(key);
    }

    reply->deleteLater();
}
