/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "IconCache.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QIcon>
#include <QObject>

#ifdef _WIN32
#include <QPixmapCache>
#include <QtWin>
#else
#include <QMimeDatabase>
#include <QMimeType>
#endif

#include <libtransmission/transmission.h>

/***
****
***/

IconCache& IconCache::get()
{
    static auto& singleton = *new IconCache();
    return singleton;
}

IconCache::IconCache() :
    folder_icon_(QFileIconProvider().icon(QFileIconProvider::Folder)),
    file_icon_(QFileIconProvider().icon(QFileIconProvider::Folder))
{
}

QIcon IconCache::guessMimeIcon(QString const& filename) const
{
#ifdef _WIN32

    QIcon icon;

    if (!filename.isEmpty())
    {
        QFileInfo const file_info(filename);

        addAssociatedFileIcon(file_info, SHGFI_SMALLICON, icon);
        addAssociatedFileIcon(file_info, 0, icon);
        addAssociatedFileIcon(file_info, SHGFI_LARGEICON, icon);
    }

    return icon;

#else

    return getMimeIcon(filename);

#endif
}

/***
****
***/

#ifdef _WIN32

void IconCache::addAssociatedFileIcon(QFileInfo const& file_info, UINT icon_size, QIcon& icon) const
{
    QString const pixmap_cache_key = QStringLiteral("tr_file_ext_") + QString::number(icon_size) + QLatin1Char('_') +
        file_info.suffix();

    QPixmap pixmap;

    if (!QPixmapCache::find(pixmap_cache_key, &pixmap))
    {
        auto const filename = file_info.fileName().toStdWString();

        SHFILEINFO shell_file_info;

        if (::SHGetFileInfoW(filename.data(), FILE_ATTRIBUTE_NORMAL, &shell_file_info,
            sizeof(shell_file_info), SHGFI_ICON | icon_size | SHGFI_USEFILEATTRIBUTES) != 0)
        {
            if (shell_file_info.hIcon != nullptr)
            {
                pixmap = QtWin::fromHICON(shell_file_info.hIcon);
                ::DestroyIcon(shell_file_info.hIcon);
            }
        }

        QPixmapCache::insert(pixmap_cache_key, pixmap);
    }

    if (!pixmap.isNull())
    {
        icon.addPixmap(pixmap);
    }
}

#else

QIcon IconCache::getMimeIcon(QString const& filename) const
{
    // If the suffix doesn't match a mime type, treat it as a folder.
    // This heuristic is fast and yields good results for torrent names.
    if (suffixes_.empty())
    {
        for (auto const& type : QMimeDatabase().allMimeTypes())
        {
            auto const tmp = type.suffixes();
            suffixes_.insert(tmp.begin(), tmp.end());
        }
    }

    auto const ext = QFileInfo(filename).suffix();
    if (suffixes_.count(ext) == 0)
    {
        return folder_icon_;
    }

    QIcon& icon = icon_cache_[ext];
    if (icon.isNull()) // cache miss
    {
        QMimeDatabase mime_db;
        QMimeType type = mime_db.mimeTypeForFile(filename, QMimeDatabase::MatchExtension);
        if (icon.isNull())
        {
            icon = QIcon::fromTheme(type.iconName());
        }

        if (icon.isNull())
        {
            icon = QIcon::fromTheme(type.genericIconName());
        }

        if (icon.isNull())
        {
            icon = file_icon_;
        }
    }

    return icon;
}

#endif
