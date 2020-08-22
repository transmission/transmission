/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <unordered_map>
#include <unordered_set>

#include <QString>
#include <QObject>
#include <QPixmap>

#include "Macros.h"
#include "Utils.h" // std::hash<QString>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

class FaviconCache : public QObject
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FaviconCache)

public:
    FaviconCache();

    using Key = QString;
    using Keys = std::unordered_set<Key>;

    // returns a cached pixmap, or a NULL pixmap if there's no match in the cache
    QPixmap find(Key const& key);
    QPixmap find(QUrl const& url) { return find(getKey(url)); }

    static Key getKey(QString const& display_name);

    // This will emit a signal when (if) the icon becomes ready.
    Key add(QUrl const& url);

    static QString getDisplayName(Key const& key);
    static QSize getIconSize();

signals:
    void pixmapReady(Key const& key);

private slots:
    void onRequestFinished(QNetworkReply* reply);

private:
    static Key getKey(QUrl const& url);
    QString getCacheDir();
    void ensureCacheDirHasBeenScanned();

    QNetworkAccessManager* nam_ = {};
    std::unordered_map<Key, QPixmap> pixmaps_;
};
