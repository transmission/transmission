/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef STATS_DIALOG_H
#define STATS_DIALOG_H

#include <QDialog>

#include "ui_stats-dialog.h"

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

#endif
