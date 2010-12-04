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
        void spinBoxEditingFinished( );
        void timeEditingFinished( );
        void lineEditingFinished( );
        void refreshPref( int key );
        void encryptionEdited( int );
        void altSpeedDaysEdited( int );
        void sessionUpdated( );
        void onWatchClicked( );
        void onScriptClicked( );
        void onIncompleteClicked( );
        void onDestinationClicked( );
        void onLocationSelected( const QString&, int key );
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
        void updateBlocklistLabel( );

    public:
        PrefsDialog( Session&, Prefs&, QWidget * parent = 0 );
        ~PrefsDialog( );

    private:
        void setPref( int key, const QVariant& v );
        bool isAllowed( int key ) const;
        QWidget * createTorrentsTab( );
        QWidget * createSpeedTab( );
        QWidget * createPrivacyTab( );
        QWidget * createNetworkTab( );
        QWidget * createDesktopTab( );
        QWidget * createWebTab( Session& );

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
        QPushButton * myTorrentDoneScriptButton;
        QCheckBox * myTorrentDoneScriptCheckbox;
        QCheckBox * myIncompleteCheckbox;
        QPushButton * myIncompleteButton;
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
        QLabel * myBlocklistLabel;
};

#endif
