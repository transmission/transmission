/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_STATS_DIALOG_H
#define QTR_STATS_DIALOG_H

#include <QDialog>

#include "ui_StatsDialog.h"

class Session;
class QTimer;

class StatsDialog: public QDialog
{
    Q_OBJECT

  private slots:
    void updateStats ();

  public:
    StatsDialog (Session&, QWidget * parent = 0);
    ~StatsDialog ();
    virtual void setVisible (bool visible);

  private:
    Session & mySession;
    QTimer * myTimer;
    Ui::StatsDialog ui;
};

#endif // QTR_STATS_DIALOG_H
