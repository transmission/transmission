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
#include <QPainter>
#include <QStyle>

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
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static auto& singleton = *new IconCache();
    return singleton;
}

IconCache::IconCache() :
    folder_icon_(QFileIconProvider().icon(QFileIconProvider::Folder)),
    file_icon_(QFileIconProvider().icon(QFileIconProvider::File))
{
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

QIcon IconCache::getMimeTypeIcon(QString const& mime_type_name, size_t file_count) const
{
    auto const is_folder = file_count > 1;
    auto& icon = (is_folder ? name_to_emblem_icon_ : name_to_icon_)[mime_type_name];

    if (!icon.isNull())
    {
        return icon;
    }

    if (!is_folder)
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

    auto const base_icon = folder_icon_;
    auto const type_icon = getMimeTypeIcon(mime_type_name, 1);
    for (auto const& base_size : base_icon.availableSizes())
    {
        auto const type_size = QSize(base_size * 0.5);
        auto const type_rect = QRect((base_size.width() - type_size.width()) * 0.8,
            (base_size.height() - type_size.height()) * 0.8,
            type_size.width(),
            type_size.height());
        auto base_pixmap = QPixmap(base_icon.pixmap(base_size));
        QPainter painter(&base_pixmap);
        auto type_pixmap = QPixmap(type_icon.pixmap(type_size));
        painter.drawPixmap(type_rect, type_pixmap, type_pixmap.rect());
        icon.addPixmap(base_pixmap);
    }

    return icon;
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
            icon = {};
        }
    }

    return icon;
}

#endif
