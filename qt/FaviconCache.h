// This file Copyright (C) 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <unordered_map>
#include <vector>

#include <QObject>
#include <QPixmap>
#include <QString>

#include <libtransmission/tr-macros.h>

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

    // returns a cached pixmap, or a nullptr pixmap if there's no match in the cache
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
