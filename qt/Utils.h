// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <utility>

#include <QPointer>
#include <QRect>
#include <QSpinBox>
#include <QString>

class QAbstractItemView;
class QColor;
class QHeaderView;
class QIcon;
class QModelIndex;

class Utils
{
public:
    static QIcon get_icon_from_index(QModelIndex const& index);

    static QString remove_trailing_dir_separator(QString const& path);

    static void narrow_rect(QRect& rect, int dx1, int dx2, Qt::LayoutDirection direction)
    {
        if (direction == Qt::RightToLeft)
        {
            qSwap(dx1, dx2);
        }

        rect.adjust(dx1, 0, -dx2, 0);
    }

    static int measure_view_item(QAbstractItemView const* view, QString const& text);
    static int measure_header_item(QHeaderView const* view, QString const& text);

    static QColor get_faded_color(QColor const& color);

    template<typename DialogT, typename... ArgsT>
    static void open_dialog(QPointer<DialogT>& dialog, ArgsT&&... args)
    {
        if (dialog.isNull())
        {
            dialog = new DialogT{ std::forward<ArgsT>(args)... }; // NOLINT clang-analyzer-cplusplus.NewDelete
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        }
        else
        {
            dialog->raise();
            dialog->activateWindow();
        }
    }

    static void update_spin_box_format(QSpinBox* spinBox, char const* context, char const* format, QString const& placeholder);
};
