/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_SESSION_DIALOG_H
#define QTR_SESSION_DIALOG_H

#include <QWidgetList>

#include "BaseDialog.h"

#include "ui_SessionDialog.h"

class Prefs;
class Session;

class SessionDialog: public BaseDialog
{
    Q_OBJECT

  public:
    SessionDialog (Session& session, Prefs& prefs, QWidget * parent = nullptr);
    virtual ~SessionDialog () {}

  public slots:
    // QDialog
    virtual void accept ();

  private slots:
    void resensitize ();

  private:
    Session& mySession;
    Prefs& myPrefs;

    Ui::SessionDialog ui;

    QWidgetList myRemoteWidgets;
    QWidgetList myAuthWidgets;
};

#endif // QTR_SESSION_DIALOG_H
