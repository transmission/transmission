// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <unordered_map>

#include <QObject>
#include <QPixmap>
#include <QString>

#include "Utils.h" // std::hash<QString>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

class FaviconCache : public QObject
{
    Q_OBJECT

public:
    FaviconCache();

    // This will emit a signal when (if) the icon becomes ready.
    void add(QString const& sitename, QString const& url);

    // returns a cached pixmap, or a nullptr pixmap if there's no match in the cache
    QPixmap find(QString const& sitename);

    static QString getDisplayName(QString const& sitename);
    static QSize getIconSize();

signals:
    void pixmapReady(QString const& sitename);

private slots:
    void onRequestFinished(QNetworkReply* reply);

private:
    void ensureCacheDirHasBeenScanned();

    QNetworkAccessManager* nam_ = {};
    std::unordered_map<QString /*sitename*/, QPixmap> pixmaps_;
};
