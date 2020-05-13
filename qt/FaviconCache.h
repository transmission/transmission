/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <unordered_map>

#include <QString>
#include <QObject>
#include <QPixmap>

#include <Utils.h> // std::hash<QString>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

class FaviconCache : public QObject
{
    Q_OBJECT

public:
    FaviconCache();
    virtual ~FaviconCache();

    // returns a cached pixmap, or a NULL pixmap if there's no match in the cache
    QPixmap find(QString const& key);
    QPixmap find(QUrl const& url) { return find(getKey(url)); }

    // This will emit a signal when (if) the icon becomes ready.
    // Returns the key.
    QString add(QUrl const& url);

    static QString getDisplayName(QString const& key);
    static QString getKey(QUrl const& url);
    static QString getKey(QString const& displayName);
    static QSize getIconSize();

signals:
    void pixmapReady(QString const& key);

private:
    QString getCacheDir();
    void ensureCacheDirHasBeenScanned();

private slots:
    void onRequestFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* myNAM;
    std::unordered_map<QString, QPixmap> myPixmaps;
};
