/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <set>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDataStream>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QMimeDatabase>
#include <QMimeType>
#include <QObject>
#include <QPixmapCache>
#include <QStyle>

#ifdef _WIN32
#include <QtWin>
#endif

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_formatter

#include "Utils.h"

/***
****
***/

namespace
{

bool isSlashChar(QChar const& c)
{
    return c == QLatin1Char('/') || c == QLatin1Char('\\');
}

#if 0
QIcon folderIcon()
{
    static QIcon icon;
    if (icon.isNull())
    {
        icon = QFileIconProvider().icon(QFileIconProvider::Folder);
    }

    return icon;
}

QIcon fileIcon()
{
    static QIcon icon;
    if (icon.isNull())
    {
        icon = QFileIconProvider().icon(QFileIconProvider::File);
    }

    return icon;
}

#ifdef _WIN32

void addAssociatedFileIcon(QFileInfo const& file_info, UINT icon_size, QIcon& icon)
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

std::unordered_map<QString, QIcon> icon_cache;

QIcon getMimeIcon(QString const& filename)
{
    // If the suffix doesn't match a mime type, treat it as a folder.
    // This heuristic is fast and yields good results for torrent names.
    static std::unordered_set<QString> suffixes;
    if (suffixes.empty())
    {
        for (auto const& type : QMimeDatabase().allMimeTypes())
        {
            auto const tmp = type.suffixes();
            suffixes.insert(tmp.begin(), tmp.end());
        }
    }

    QString const ext = QFileInfo(filename).suffix();
    if (suffixes.count(ext) == 0)
    {
        return folderIcon();
    }

    QIcon& icon = icon_cache[ext];
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
            icon = fileIcon();
        }
    }

    return icon;
}

#endif
#endif

} // namespace

#if 0
QIcon Utils::getFolderIcon()
{
    return folderIcon();
}

QIcon Utils::getFileIcon()
{
    return fileIcon();
}

QIcon Utils::guessMimeIcon(QString const& filename)
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

#endif

QIcon Utils::getIconFromIndex(QModelIndex const& index)
{
    QVariant const variant = index.data(Qt::DecorationRole);

    switch (variant.type())
    {
    case QVariant::Icon:
        return qvariant_cast<QIcon>(variant);

    case QVariant::Pixmap:
        return QIcon(qvariant_cast<QPixmap>(variant));

    default:
        return QIcon();
    }
}

bool Utils::isValidUtf8(char const* s)
{
    int n; // number of bytes in a UTF-8 sequence

    for (char const* c = s; *c != '\0'; c += n)
    {
        if ((*c & 0x80) == 0x00)
        {
            n = 1; // ASCII
        }
        else if ((*c & 0xc0) == 0x80)
        { // NOLINT(bugprone-branch-clone)
            return false; // not valid
        }
        else if ((*c & 0xe0) == 0xc0)
        {
            n = 2;
        }
        else if ((*c & 0xf0) == 0xe0)
        {
            n = 3;
        }
        else if ((*c & 0xf8) == 0xf0)
        {
            n = 4;
        }
        else if ((*c & 0xfc) == 0xf8)
        {
            n = 5;
        }
        else if ((*c & 0xfe) == 0xfc)
        {
            n = 6;
        }
        else
        {
            return false;
        }

        for (int m = 1; m < n; m++)
        {
            if ((c[m] & 0xc0) != 0x80)
            {
                return false;
            }
        }
    }

    return true;
}

QString Utils::removeTrailingDirSeparator(QString const& path)
{
    int i = path.size();

    while (i > 1 && isSlashChar(path[i - 1]))
    {
        --i;
    }

    return path.left(i);
}

int Utils::measureViewItem(QAbstractItemView* view, QString const& text)
{
    QStyleOptionViewItem option;
    option.initFrom(view);
    option.features = QStyleOptionViewItem::HasDisplay;
    option.text = text;
    option.textElideMode = Qt::ElideNone;
    option.font = view->font();

    return view->style()->sizeFromContents(QStyle::CT_ItemViewItem, &option, QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX), view).
        width();
}

int Utils::measureHeaderItem(QHeaderView* view, QString const& text)
{
    QStyleOptionHeader option;
    option.initFrom(view);
    option.text = text;
    option.sortIndicator = view->isSortIndicatorShown() ? QStyleOptionHeader::SortDown : QStyleOptionHeader::None;

    return view->style()->sizeFromContents(QStyle::CT_HeaderSection, &option, QSize(), view).width();
}

QColor Utils::getFadedColor(QColor const& color)
{
    QColor faded_color(color);
    faded_color.setAlpha(128);
    return faded_color;
}
