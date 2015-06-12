/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FILTER_BAR_LINE_EDIT_H
#define QTR_FILTER_BAR_LINE_EDIT_H

#include <QLineEdit>

class QToolButton;

class FilterBarLineEdit: public QLineEdit
{
    Q_OBJECT

  public:
    FilterBarLineEdit (QWidget * parent = nullptr);

  protected:
    // QWidget
    virtual void resizeEvent (QResizeEvent * event);

  private slots:
    void updateClearButtonVisibility ();

  private:
    QToolButton * myClearButton;
};

#endif // QTR_FILTER_BAR_LINE_EDIT_H
