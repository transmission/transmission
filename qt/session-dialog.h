/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
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
    SessionDialog (Session& session, Prefs& prefs, QWidget * parent = 0);
    ~SessionDialog () {}

  private slots:
    void onAccepted ();
    void resensitize ();

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
