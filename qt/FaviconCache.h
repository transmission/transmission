/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <unordered_map>
#include <vector>

#include <QObject>
#include <QPixmap>
#include <QString>

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
    using Keys = std::vector<Key>;

    // returns a cached pixmap, or a NULL pixmap if there's no match in the cache
    QPixmap find(Key const& key);

    static Key getKey(QString const& display_name);

    // This will emit a signal when (if) the icon becomes ready.
    Key add(QString const& url);

    static QString getDisplayName(Key const& key);
    static QSize getIconSize();

signals:
    void pixmapReady(Key const& key);

private slots:
    void onRequestFinished(QNetworkReply* reply);

private:
    static Key getKey(QUrl const& url);
    void ensureCacheDirHasBeenScanned();

    QNetworkAccessManager* nam_ = {};
    std::unordered_map<Key, QPixmap> pixmaps_;
    std::unordered_map<QString, Key> keys_;
};
