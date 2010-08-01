/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id$
 */

#ifndef STATS_DIALOG_H
#define STATS_DIALOG_H

#include <QDialog>

class Session;
class QLabel;
class QTimer;

class StatsDialog: public QDialog
{
        Q_OBJECT

    signals:
        void accepted( );

    public slots:
        void updateStats( );

    private slots:
        void onTimer( );

    public:
        StatsDialog( Session&, QWidget * parent = 0 );
        ~StatsDialog( );
        virtual void setVisible( bool visible );

    private:
        Session & mySession;
        QTimer * myTimer;
        QLabel * myCurrentUp;
        QLabel * myCurrentDown;
        QLabel * myCurrentRatio;
        QLabel * myCurrentDuration;
        QLabel * myStartCount;
        QLabel * myTotalUp;
        QLabel * myTotalDown;
        QLabel * myTotalRatio;
        QLabel * myTotalDuration;
};

#endif
