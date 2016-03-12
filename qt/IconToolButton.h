/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_ICON_TOOL_BUTTON_H
#define QTR_ICON_TOOL_BUTTON_H

#include <QToolButton>

class IconToolButton: public QToolButton
{
    Q_OBJECT

  public:
    IconToolButton (QWidget * parent = nullptr);

    // QWidget
    QSize sizeHint () const override;

  protected:
    // QWidget
    void paintEvent (QPaintEvent * event) override;
};

#endif // QTR_ICON_TOOL_BUTTON_H
