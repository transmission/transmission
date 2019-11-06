/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cctype> // isxdigit()

#include <QPointer>
#include <QRect>
#include <QString>

class QAbstractItemView;
class QColor;
class QHeaderView;
class QIcon;
class QModelIndex;

class Utils
{
public:
    static QIcon getFileIcon();
    static QIcon getFolderIcon();
    static QIcon guessMimeIcon(QString const& filename);
    static QIcon getIconFromIndex(QModelIndex const& index);

    // Test if string is UTF-8 or not
    static bool isValidUtf8(char const* s);

    static QString removeTrailingDirSeparator(QString const& path);

    static void narrowRect(QRect& rect, int dx1, int dx2, Qt::LayoutDirection direction)
    {
        if (direction == Qt::RightToLeft)
        {
            qSwap(dx1, dx2);
        }

        rect.adjust(dx1, 0, -dx2, 0);
    }

    static int measureViewItem(QAbstractItemView* view, QString const& text);
    static int measureHeaderItem(QHeaderView* view, QString const& text);

    static QColor getFadedColor(QColor const& color);

    template<typename DialogT, typename... ArgsT>
    static void openDialog(QPointer<DialogT>& dialog, ArgsT&& ... args)
    {
        if (dialog.isNull())
        {
            dialog = new DialogT(std::forward<ArgsT>(args)...);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        }
        else
        {
            dialog->raise();
            dialog->activateWindow();
        }
    }

    ///
    /// URLs
    ///

    static bool isMagnetLink(QString const& s)
    {
        return s.startsWith(QString::fromUtf8("magnet:?"));
    }

    static bool isHexHashcode(QString const& s)
    {
        if (s.length() != 40)
        {
            return false;
        }

        for (QChar const ch : s)
        {
            if (!isxdigit(ch.unicode()))
            {
                return false;
            }
        }

        return true;
    }

    static bool isUriWithSupportedScheme(QString const& s)
    {
        static QString const ftp = QString::fromUtf8("ftp://");
        static QString const http = QString::fromUtf8("http://");
        static QString const https = QString::fromUtf8("https://");
        return s.startsWith(http) || s.startsWith(https) || s.startsWith(ftp);
    }
};
