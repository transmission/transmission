/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef SESSION_DIALOG_H
#define SESSION_DIALOG_H

#include <QDialog>
#include <QWidgetList>

class Prefs;
class Session;
class QCheckBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

class SessionDialog: public QDialog
{
        Q_OBJECT

    public:
        SessionDialog( Session& session, Prefs& prefs, QWidget * parent = 0 );
        ~SessionDialog( ) { }

    private slots:
        void onAccepted( );
        void resensitize( );

    private:
        QCheckBox * myAuthCheckBox;
        QRadioButton * myRemoteRadioButton;
        QLineEdit * myHostLineEdit;
        QSpinBox * myPortSpinBox;
        QLineEdit * myUsernameLineEdit;
        QLineEdit * myPasswordLineEdit;
        QCheckBox * myAutomaticCheckBox;

    private:
        Session& mySession;
        Prefs& myPrefs;
        QWidgetList myRemoteWidgets;
        QWidgetList myAuthWidgets;
};

#endif
