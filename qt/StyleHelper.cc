// This file Copyright © 2017-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QPainter>
#include <QStyleOptionProgressBar>

#include "StyleHelper.h"

QIcon::Mode StyleHelper::getIconMode(QStyle::State const& state)
{
    if (!state.testFlag(QStyle::State_Enabled))
    {
        return QIcon::Disabled;
    }

    if (state.testFlag(QStyle::State_Selected))
    {
        return QIcon::Selected;
    }

    return QIcon::Normal;
}

void StyleHelper::drawProgressBar(QStyle const& style, QPainter& painter, QStyleOptionProgressBar const& option)
{
    // Workaround for https://bugreports.qt.io/browse/QTBUG-67830

#ifdef Q_OS_DARWIN
    auto my_option = option;
    my_option.rect.translate(-option.rect.topLeft());

    painter.save();
    painter.translate(option.rect.topLeft());
    style.drawControl(QStyle::CE_ProgressBar, &my_option, &painter);
    painter.restore();
#else
    style.drawControl(QStyle::CE_ProgressBar, &option, &painter);
#endif
}
