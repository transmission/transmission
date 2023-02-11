// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // size_t
#include <utility>

#include <QHash>
#include <QPointer>
#include <QRect>
#include <QString>

class QAbstractItemView;
class QColor;
class QHeaderView;
class QIcon;
class QModelIndex;

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)

namespace std
{

template<>
struct hash<QString>
{
    std::size_t operator()(QString const& s) const
    {
        return qHash(s);
    }
};

} // namespace std

#endif

class Utils
{
public:
    static QIcon getIconFromIndex(QModelIndex const& index);

    static QString removeTrailingDirSeparator(QString const& path);

    static void narrowRect(QRect& rect, int dx1, int dx2, Qt::LayoutDirection direction)
    {
        if (direction == Qt::RightToLeft)
        {
            qSwap(dx1, dx2);
        }

        rect.adjust(dx1, 0, -dx2, 0);
    }

    static int measureViewItem(QAbstractItemView const* view, QString const& text);
    static int measureHeaderItem(QHeaderView const* view, QString const& text);

    static QColor getFadedColor(QColor const& color);

    template<typename DialogT, typename... ArgsT>
    static void openDialog(QPointer<DialogT>& dialog, ArgsT&&... args)
    {
        if (dialog.isNull())
        {
            dialog = new DialogT(std::forward<ArgsT>(args)...); // NOLINT clang-analyzer-cplusplus.NewDelete
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        }
        else
        {
            dialog->raise();
            dialog->activateWindow();
        }
    }
};
