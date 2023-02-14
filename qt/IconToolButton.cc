// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QStyle>
#include <QStyleOption>
#include <QStyleOptionToolButton>
#include <QStylePainter>

#include "IconToolButton.h"

IconToolButton::IconToolButton(QWidget* parent)
    : QToolButton(parent)
{
}

QSize IconToolButton::sizeHint() const
{
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.features = QStyleOptionToolButton::None;
    option.toolButtonStyle = Qt::ToolButtonIconOnly;
    QSize const size = style()->sizeFromContents(QStyle::CT_ToolButton, &option, iconSize(), this);

    return size.expandedTo(iconSize() + QSize(4, 4));
}

void IconToolButton::paintEvent(QPaintEvent* /*event*/)
{
    QStylePainter painter(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.features = QStyleOptionToolButton::None;
    option.toolButtonStyle = Qt::ToolButtonIconOnly;
    painter.drawComplexControl(QStyle::CC_ToolButton, option);
}
