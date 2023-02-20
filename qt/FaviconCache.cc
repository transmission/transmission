// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>

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
    : nam_(new QNetworkAccessManager(this))
{
    connect(nam_, &QNetworkAccessManager::finished, this, &FaviconCache::onRequestFinished);
}

/***
****
***/

namespace
{

QPixmap scale(QPixmap const& pixmap)
{
    return pixmap.scaled(FaviconCache::getIconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QString getCacheDir()
{
    auto const base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(base).absoluteFilePath(QStringLiteral("favicons"));
}

QString getScrapedFile()
{
    auto const base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(base).absoluteFilePath(QStringLiteral("favicons-scraped.txt"));
}

void markSiteAsScraped(QString const& sitename)
{
    auto skip_file = QFile(getScrapedFile());
    if (skip_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
    {
        skip_file.write(sitename.toUtf8());
        skip_file.write("\n");
    }
}

} // namespace

void FaviconCache::ensureCacheDirHasBeenScanned()
{
    static bool has_been_scanned = false;
    if (has_been_scanned)
    {
        return;
    }

    has_been_scanned = true;

    // remember which hosts we've asked for a favicon so that we
    // don't re-ask them every time we start a new session
    if (auto skip_file = QFile(getScrapedFile()); skip_file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        while (!skip_file.atEnd())
        {
            auto const sitename = QString::fromUtf8(skip_file.readLine()).trimmed();
            pixmaps_.try_emplace(sitename);
        }
    }

    // load the cached favicons
    auto cache_dir = QDir(getCacheDir());
    cache_dir.mkpath(cache_dir.absolutePath());
    for (auto const& sitename : cache_dir.entryList(QDir::Files | QDir::Readable))
    {
        QPixmap const pixmap(cache_dir.absoluteFilePath(sitename));
        if (!pixmap.isNull())
        {
            pixmaps_[sitename] = scale(pixmap);
        }
    }
}

/***
****
***/

QString FaviconCache::getDisplayName(QString const& sitename)
{
    auto name = sitename;
    if (!name.isEmpty())
    {
        name.front() = name.front().toTitleCase();
    }
    return name;
}

QSize FaviconCache::getIconSize()
{
    return { 16, 16 };
}

QPixmap FaviconCache::find(QString const& sitename)
{
    ensureCacheDirHasBeenScanned();

    return pixmaps_[sitename];
}

void FaviconCache::add(QString const& sitename, QString const& url_str)
{
    ensureCacheDirHasBeenScanned();

    // Try to download a favicon if we don't have one.
    // Add a placeholder to prevent repeat downloads.
    if (auto const already_had_it = !pixmaps_.try_emplace(sitename).second; already_had_it)
    {
        return;
    }

    markSiteAsScraped(sitename);

    auto const scrape = [this, sitename](auto const host)
    {
        auto const schemes = std::array<QString, 2>{
            QStringLiteral("http"),
            QStringLiteral("https"),
        };
        auto const suffixes = std::array<QString, 5>{
            QStringLiteral("gif"), //
            QStringLiteral("ico"), //
            QStringLiteral("jpg"), //
            QStringLiteral("png"), //
            QStringLiteral("svg"), //
        };
        for (auto const& scheme : schemes)
        {
            for (auto const& suffix : suffixes)
            {
                auto const path = QStringLiteral("%1://%2/favicon.%3").arg(scheme).arg(host).arg(suffix);
                auto request = QNetworkRequest(path);
                request.setAttribute(QNetworkRequest::UserMax, sitename);
                nam_->get(request);
            }
        }
    };

    // scrape tracker.domain.com
    auto const host = QUrl(url_str).host();
    scrape(host);

    if (auto const idx = host.indexOf(sitename); idx != -1)
    {
        // scrape domain.com
        auto const root = host.mid(idx);
        if (root != host)
        {
            scrape(root);
        }

        // scrape www.domain.com
        if (auto const www = QStringLiteral("www.") + root; www != host)
        {
            scrape(www);
        }
    }
}

void FaviconCache::onRequestFinished(QNetworkReply* reply)
{
    auto const content = reply->readAll();
    auto pixmap = QPixmap{};

    if (reply->error() == QNetworkReply::NoError)
    {
        pixmap.loadFromData(content);
    }

    if (!pixmap.isNull())
    {
        auto sitename = reply->request().attribute(QNetworkRequest::UserMax).toString();

        // save it in memory...
        pixmaps_[sitename] = scale(pixmap);

        // save it on disk...
        QDir const cache_dir(getCacheDir());
        cache_dir.mkpath(cache_dir.absolutePath());
        QFile file(cache_dir.absoluteFilePath(sitename));
        file.open(QIODevice::WriteOnly);
        file.write(content);
        file.close();

        // notify listeners
        emit pixmapReady(sitename);
    }

    reply->deleteLater();
}
