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

#ifndef PREFS_DIALOG_H
#define PREFS_DIALOG_H

#include <QDialog>
#include <QSet>
#include "prefs.h"

class QAbstractButton;
class QCheckBox;
class QString;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class QTime;
class QTimeEdit;
class QWidget;
class QPushButton;
class QMessageBox;
class QHttp;

class Prefs;
class Session;

class PrefsDialog: public QDialog
{
        Q_OBJECT

    private slots:
        void checkBoxToggled( bool checked );
        void spinBoxChanged( int value );
        void doubleSpinBoxChanged( double value );
        void spinBoxChangedIdle( );
        void timeChanged( const QTime& );
        void textChanged( const QString& );
        void updatePref( int key );
        void encryptionEdited( int );
        void altSpeedDaysEdited( int );
        void sessionUpdated( );
        void onWatchClicked( );
        void onWatchSelected( const QStringList& );
        void onDestinationClicked( );
        void onDestinationSelected( const QStringList& );
        void onPortTested( bool );
        void onPortTest( );

        void onUpdateBlocklistClicked( );
        void onUpdateBlocklistCancelled( );
        void onBlocklistDialogDestroyed( QObject * );
        void onBlocklistUpdated( int n );

    private:
        QDoubleSpinBox * doubleSpinBoxNew( int key, double low, double high, double step, int decimals );
        QCheckBox * checkBoxNew( const QString& text, int key );
        QSpinBox * spinBoxNew( int key, int low, int high, int step );
        QTimeEdit * timeEditNew( int key );
        QLineEdit * lineEditNew( int key, int mode = 0 );
        void enableBuddyWhenChecked( QCheckBox *, QWidget * );
        void updateBlocklistCheckBox( );

    public:
        PrefsDialog( Session&, Prefs&, QWidget * parent = 0 );
        ~PrefsDialog( );

    private:
        bool isAllowed( int key ) const;
        QWidget * createTorrentsTab( );
        QWidget * createPeersTab( );
        QWidget * createPrivacyTab( );
        QWidget * createSpeedTab( );
        QWidget * createWebTab( Session& );
        QWidget * createTrackerTab( );

    private:
        typedef QMap<int,QWidget*> key2widget_t;
        key2widget_t myWidgets;
        const bool myIsServer;
        Session& mySession;
        Prefs& myPrefs;
        QVBoxLayout * myLayout;
        QLabel * myPortLabel;
        QPushButton * myPortButton;
        QPushButton * myWatchButton;
        QPushButton * myDestinationButton;
        QWidgetList myWebWidgets;
        QWidgetList myWebAuthWidgets;
        QWidgetList myWebWhitelistWidgets;
        QWidgetList myProxyWidgets;
        QWidgetList myProxyAuthWidgets;
        QWidgetList mySchedWidgets;
        QWidgetList myBlockWidgets;
        QWidgetList myUnsupportedWhenRemote;

        int myBlocklistHttpTag;
        QHttp * myBlocklistHttp;
        QMessageBox * myBlocklistDialog;
};

#endif
