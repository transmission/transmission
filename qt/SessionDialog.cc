// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Prefs.h"
#include "Session.h"
#include "SessionDialog.h"

/***
****
***/

void SessionDialog::accept()
{
    prefs_.set(TR_KEY_remote_session_enabled, ui_.remoteSessionRadio->isChecked());
    prefs_.set(TR_KEY_remote_session_host, ui_.hostEdit->text());
    prefs_.set(TR_KEY_remote_session_port, ui_.portSpin->value());
    prefs_.set(TR_KEY_remote_session_requires_authentication, ui_.authCheck->isChecked());
    prefs_.set(TR_KEY_remote_session_username, ui_.usernameEdit->text());
    prefs_.set(TR_KEY_remote_session_password, ui_.passwordEdit->text());
    prefs_.set(TR_KEY_remote_session_url_base_path, ui_.rpcEdit->text());
    session_.restart();
    BaseDialog::accept();
}

void SessionDialog::resensitize() const
{
    bool const is_remote = ui_.remoteSessionRadio->isChecked();
    bool const use_auth = ui_.authCheck->isChecked();

    for (QWidget* const w : remote_widgets_)
    {
        w->setEnabled(is_remote);
    }

    for (QWidget* const w : auth_widgets_)
    {
        w->setEnabled(is_remote && use_auth);
    }
}

/***
****
***/

SessionDialog::SessionDialog(Session& session, Prefs& prefs, QWidget* parent)
    : BaseDialog{ parent }
    , session_{ session }
    , prefs_{ prefs }
{
    ui_.setupUi(this);

    ui_.localSessionRadio->setChecked(!prefs.get<bool>(TR_KEY_remote_session_enabled));
    connect(ui_.localSessionRadio, &QAbstractButton::toggled, this, &SessionDialog::resensitize);

    ui_.remoteSessionRadio->setChecked(prefs.get<bool>(TR_KEY_remote_session_enabled));
    connect(ui_.remoteSessionRadio, &QAbstractButton::toggled, this, &SessionDialog::resensitize);

    ui_.hostEdit->setText(prefs.get<QString>(TR_KEY_remote_session_host));
    remote_widgets_ << ui_.hostLabel << ui_.hostEdit;

    ui_.portSpin->setValue(prefs.get<int>(TR_KEY_remote_session_port));
    remote_widgets_ << ui_.portLabel << ui_.portSpin;

    ui_.authCheck->setChecked(prefs.get<bool>(TR_KEY_remote_session_requires_authentication));
    connect(ui_.authCheck, &QAbstractButton::toggled, this, &SessionDialog::resensitize);
    remote_widgets_ << ui_.authCheck;

    ui_.usernameEdit->setText(prefs.get<QString>(TR_KEY_remote_session_username));
    auth_widgets_ << ui_.usernameLabel << ui_.usernameEdit;

    ui_.passwordEdit->setText(prefs.get<QString>(TR_KEY_remote_session_password));
    auth_widgets_ << ui_.passwordLabel << ui_.passwordEdit;

    ui_.rpcEdit->setText(prefs.get<QString>(TR_KEY_remote_session_url_base_path));
    remote_widgets_ << ui_.rpcLabel << ui_.rpcEdit;

    resensitize();
}
