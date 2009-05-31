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

#ifndef DETAILS_DIALOG_H
#define DETAILS_DIALOG_H

#include <QDialog>
#include <QString>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QWidgetList>

#include "prefs.h"

class FileTreeView;
class QTreeView;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class QSpinBox;
class QTextBrowser;
class QTreeWidget;
class QTreeWidgetItem;
class Session;
class Torrent;
class TorrentModel;

class Details: public QDialog
{
        Q_OBJECT

    private slots:
        void onTorrentChanged( );
        void onTimer( );

    public:
        Details( Session&, TorrentModel&, QWidget * parent = 0 );
        ~Details( );
        void setIds( const QSet<int>& ids );

    private:
        QWidget * createActivityTab( );
        QWidget * createPeersTab( );
        QWidget * createTrackerTab( );
        QWidget * createInfoTab( );
        QWidget * createFilesTab( );
        QWidget * createOptionsTab( );

    private:
        QString trimToDesiredWidth( const QString& str );
        void enableWhenChecked( QCheckBox *, QWidget * );

    private:

        Session& mySession;
        TorrentModel& myModel; 
        QSet<int> myIds;
        QTimer myTimer;
        bool myHavePendingRefresh;

        QLabel * myStateLabel;
        QLabel * myProgressLabel;
        QLabel * myHaveLabel;
        QLabel * myDownloadedLabel;
        QLabel * myUploadedLabel;
        QLabel * myFailedLabel;
        QLabel * myRatioLabel;
        QLabel * mySwarmSpeedLabel;
        QLabel * myErrorLabel;
        QLabel * myAddedDateLabel;
        QLabel * myActivityLabel;

        QCheckBox * mySessionLimitCheck;
        QCheckBox * mySingleDownCheck;
        QCheckBox * mySingleUpCheck;
        QSpinBox * mySingleDownSpin;
        QSpinBox * mySingleUpSpin;
        QRadioButton * mySeedGlobalRadio;
        QRadioButton * mySeedForeverRadio;
        QRadioButton * mySeedCustomRadio;
        QDoubleSpinBox * mySeedCustomSpin;
        QSpinBox * myPeerLimitSpin;
        QComboBox * myBandwidthPriorityCombo;

        QLabel * myPiecesLabel;
        QLabel * myHashLabel;
        QLabel * myPrivacyLabel;
        QLabel * myCreatorLabel;
        QLabel * myDateCreatedLabel;
        QLabel * myDestinationLabel;
        QLabel * myTorrentFileLabel;
        QTextBrowser * myCommentBrowser;

        QLabel * myTrackerLabel;
        QLabel * myScrapeTimePrevLabel;
        QLabel * myScrapeTimeNextLabel;
        QLabel * myScrapeResponseLabel;
        QLabel * myAnnounceTimePrevLabel;
        QLabel * myAnnounceTimeNextLabel;
        QLabel * myAnnounceResponseLabel;
        QLabel * myAnnounceManualLabel;

        QLabel * mySeedersLabel;
        QLabel * myLeechersLabel;
        QLabel * myTimesCompletedLabel;
        QTreeWidget * myPeerTree;
        QMap<QString,QTreeWidgetItem*> myPeers;
        QWidgetList myWidgets;

        FileTreeView * myFileTreeView;

    private slots:
        void onBandwidthPriorityChanged( int );
        void onFilePriorityChanged( const QSet<int>& fileIndices, int );
        void onFileWantedChanged( const QSet<int>& fileIndices, bool );
        void onHonorsSessionLimitsToggled( bool );
        void onDownloadLimitedToggled( bool );
        void onDownloadLimitChanged( int );
        void onUploadLimitedToggled( bool );
        void onUploadLimitChanged( int );
        void onSeedUntilChanged( bool );
        void onSeedRatioLimitChanged( double );
        void onMaxPeersChanged( int );
        void refresh( );
};

#endif
