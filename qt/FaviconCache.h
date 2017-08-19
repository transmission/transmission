/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QMap>
#include <QString>
#include <QObject>
#include <QPixmap>

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
    QPixmap find(QUrl const& url);

    // returns a cached pixmap, or a NULL pixmap if there's no match in the cache
    QPixmap findFromHost(QString const& host);

    // this will emit a signal when (if) the icon becomes ready
    void add(QUrl const& url);

    static QString getHost(QUrl const& url);
    static QSize getIconSize();

signals:
    void pixmapReady(QString const& host);

private:
    QString getCacheDir();
    void ensureCacheDirHasBeenScanned();

private slots:
    void onRequestFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* myNAM;
    QMap<QString, QPixmap> myPixmaps;
};
