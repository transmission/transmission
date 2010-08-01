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
