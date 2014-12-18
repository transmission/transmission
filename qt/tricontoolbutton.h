/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_ICON_TOOL_BUTTON_H
#define QTR_ICON_TOOL_BUTTON_H

#include <QToolButton>

class TrIconToolButton: public QToolButton
{
    Q_OBJECT

  public:
    TrIconToolButton (QWidget * parent = nullptr);

  protected:
    virtual void paintEvent (QPaintEvent * event);
};

#endif // QTR_ICON_TOOL_BUTTON_H
