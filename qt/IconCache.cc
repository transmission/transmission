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
#include <QMimeDatabase>
#include <QObject>
#include <QPainter>
#include <QStyle>

#ifdef _WIN32
#include <QPixmapCache>
#include <QtWin>
#endif

#include <libtransmission/transmission.h>

/***
****
***/

IconCache& IconCache::get()
{
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static auto& singleton = *new IconCache();
    return singleton;
}

QIcon IconCache::guessMimeIcon(QString const& filename, QIcon fallback) const
{
    QIcon icon;

#ifdef _WIN32

    if (!filename.isEmpty())
    {
        QFileInfo const file_info(filename);

        addAssociatedFileIcon(file_info, SHGFI_SMALLICON, icon);
        addAssociatedFileIcon(file_info, 0, icon);
        addAssociatedFileIcon(file_info, SHGFI_LARGEICON, icon);
    }

#else

    icon = getMimeIcon(filename);

#endif

    if (icon.isNull())
    {
        icon = fallback;
    }

    return icon;
}

QIcon IconCache::getMimeTypeIcon(QString const& mime_type_name, bool multifile) const
{
    auto& icon = (multifile ? name_to_emblem_icon_ : name_to_icon_)[mime_type_name];

    if (!icon.isNull())
    {
        return icon;
    }

    if (!multifile)
    {
        QMimeDatabase mime_db;
        auto const type = mime_db.mimeTypeForName(mime_type_name);
        icon = QIcon::fromTheme(type.iconName());

        if (icon.isNull())
        {
            icon = QIcon::fromTheme(type.genericIconName());
        }

        if (icon.isNull())
        {
            icon = file_icon_;
        }

        return icon;
    }

    auto const mime_icon = getMimeTypeIcon(mime_type_name, false);
    for (auto const& size : { QSize(24, 24), QSize(32, 32), QSize(48, 48) })
    {
        // upper left corner
        auto const folder_size = size / 2;
        auto const folder_rect = QRect(QPoint(), folder_size);

        // fullsize
        auto const mime_size = size;
        auto const mime_rect = QRect(QPoint(), mime_size);

        // build the icon
        auto pixmap = QPixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHints(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(folder_rect, folder_icon_.pixmap(folder_size));
        painter.drawPixmap(mime_rect, mime_icon.pixmap(mime_size));
        icon.addPixmap(pixmap);
    }

    return icon;
}

/***
****
***/

#ifdef _WIN32

void IconCache::addAssociatedFileIcon(QFileInfo const& file_info, unsigned int icon_size, QIcon& icon) const
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
        return {};
    }

    QIcon& icon = ext_to_icon_[ext];
    if (icon.isNull()) // cache miss
    {
        QMimeDatabase mime_db;
        auto const type = mime_db.mimeTypeForFile(filename, QMimeDatabase::MatchExtension);
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
            icon = {};
        }
    }

    return icon;
}

#endif
