/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_COLUMN_RESIZER_H
#define QTR_COLUMN_RESIZER_H

#include <QObject>
#include <QSet>

class QGridLayout;
class QTimer;

class ColumnResizer: public QObject
{
    Q_OBJECT

  public:
    ColumnResizer (QObject * parent = nullptr);

    void addLayout (QGridLayout * layout);

    // QObject
    virtual bool eventFilter (QObject * object, QEvent * event);

  public slots:
    void update ();

  private:
    void scheduleUpdate ();

  private:
    QTimer * myTimer;
    QSet<QGridLayout *> myLayouts;
};

#endif // QTR_COLUMN_RESIZER_H
