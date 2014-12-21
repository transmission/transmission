/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef SESSION_DIALOG_H
#define SESSION_DIALOG_H

#include <QDialog>
#include <QWidgetList>

#include "ui_session-dialog.h"

class Prefs;
class Session;

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
    Session& mySession;
    Prefs& myPrefs;
    Ui::SessionDialog ui;
    QWidgetList myRemoteWidgets;
    QWidgetList myAuthWidgets;
};

#endif
