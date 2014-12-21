/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QStyleOption>
#include <QStyleOptionToolButton>
#include <QStylePainter>

#include "tricontoolbutton.h"

TrIconToolButton::TrIconToolButton (QWidget * parent):
  QToolButton (parent)
{
}

void TrIconToolButton::paintEvent (QPaintEvent * /*event*/)
{
  QStylePainter painter(this);
  QStyleOptionToolButton option;
  initStyleOption (&option);
  option.features &= ~QStyleOptionToolButton::HasMenu;
  painter.drawComplexControl(QStyle::CC_ToolButton, option);
}
