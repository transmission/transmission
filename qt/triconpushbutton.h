/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_IconPushButton_H
#define QTR_IconPushButton_H

#include <QPushButton>

class QIcon;

class TrIconPushButton: public QPushButton
{
    Q_OBJECT

  public:
    TrIconPushButton (QWidget * parent = 0);
    TrIconPushButton (const QIcon&, QWidget * parent = 0);
    virtual ~TrIconPushButton () {}
    QSize sizeHint () const;

  protected:
    void paintEvent (QPaintEvent * event);
};

#endif // QTR_IconPushButton_H
