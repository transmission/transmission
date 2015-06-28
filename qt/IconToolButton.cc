/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QStyle>
#include <QStyleOption>
#include <QStyleOptionToolButton>
#include <QStylePainter>

#include "IconToolButton.h"

IconToolButton::IconToolButton (QWidget * parent):
  QToolButton (parent)
{
}

QSize
IconToolButton::sizeHint () const
{
  QStyleOptionToolButton option;
  initStyleOption (&option);
  option.features = QStyleOptionToolButton::None;
  option.toolButtonStyle = Qt::ToolButtonIconOnly;
  const QSize size = style ()->sizeFromContents (QStyle::CT_ToolButton, &option, iconSize (), this);

  return size.expandedTo (iconSize () + QSize (8, 8));
}

void IconToolButton::paintEvent (QPaintEvent * /*event*/)
{
  QStylePainter painter(this);
  QStyleOptionToolButton option;
  initStyleOption (&option);
  option.features = QStyleOptionToolButton::None;
  option.toolButtonStyle = Qt::ToolButtonIconOnly;
  painter.drawComplexControl(QStyle::CC_ToolButton, option);
}
