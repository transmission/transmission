/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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
