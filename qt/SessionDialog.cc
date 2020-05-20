/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "Prefs.h"
#include "Session.h"
#include "SessionDialog.h"

/***
****
***/

void SessionDialog::accept()
{
    prefs_.set(Prefs::SESSION_IS_REMOTE, ui.remoteSessionRadio->isChecked());
    prefs_.set(Prefs::SESSION_REMOTE_HOST, ui.hostEdit->text());
    prefs_.set(Prefs::SESSION_REMOTE_PORT, ui.portSpin->value());
    prefs_.set(Prefs::SESSION_REMOTE_AUTH, ui.authCheck->isChecked());
    prefs_.set(Prefs::SESSION_REMOTE_USERNAME, ui.usernameEdit->text());
    prefs_.set(Prefs::SESSION_REMOTE_PASSWORD, ui.passwordEdit->text());
    session_.restart();
    BaseDialog::accept();
}

void SessionDialog::resensitize()
{
    bool const isRemote = ui.remoteSessionRadio->isChecked();
    bool const useAuth = ui.authCheck->isChecked();

    for (QWidget* const w : remote_widgets_)
    {
        w->setEnabled(isRemote);
    }

    for (QWidget* const w : auth_widgets_)
    {
        w->setEnabled(isRemote && useAuth);
    }
}

/***
****
***/

SessionDialog::SessionDialog(Session& session, Prefs& prefs, QWidget* parent) :
    BaseDialog(parent),
    session_(session),
    prefs_(prefs)
{
    ui.setupUi(this);

    ui.localSessionRadio->setChecked(!prefs.get<bool>(Prefs::SESSION_IS_REMOTE));
    connect(ui.localSessionRadio, SIGNAL(toggled(bool)), this, SLOT(resensitize()));

    ui.remoteSessionRadio->setChecked(prefs.get<bool>(Prefs::SESSION_IS_REMOTE));
    connect(ui.remoteSessionRadio, SIGNAL(toggled(bool)), this, SLOT(resensitize()));

    ui.hostEdit->setText(prefs.get<QString>(Prefs::SESSION_REMOTE_HOST));
    remote_widgets_ << ui.hostLabel << ui.hostEdit;

    ui.portSpin->setValue(prefs.get<int>(Prefs::SESSION_REMOTE_PORT));
    remote_widgets_ << ui.portLabel << ui.portSpin;

    ui.authCheck->setChecked(prefs.get<bool>(Prefs::SESSION_REMOTE_AUTH));
    connect(ui.authCheck, SIGNAL(toggled(bool)), this, SLOT(resensitize()));
    remote_widgets_ << ui.authCheck;

    ui.usernameEdit->setText(prefs.get<QString>(Prefs::SESSION_REMOTE_USERNAME));
    auth_widgets_ << ui.usernameLabel << ui.usernameEdit;

    ui.passwordEdit->setText(prefs.get<QString>(Prefs::SESSION_REMOTE_PASSWORD));
    auth_widgets_ << ui.passwordLabel << ui.passwordEdit;

    resensitize();
}
