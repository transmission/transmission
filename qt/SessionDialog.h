// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QWidgetList>

#include <libtransmission/tr-macros.h>

#include "BaseDialog.h"
#include "ui_SessionDialog.h"

class Prefs;
class Session;

class SessionDialog : public BaseDialog
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(SessionDialog)

public:
    SessionDialog(Session& session, Prefs& prefs, QWidget* parent = nullptr);

public slots:
    // QDialog
    void accept() override;

private slots:
    void resensitize() const;

private:
    Session& session_;
    Prefs& prefs_;

    Ui::SessionDialog ui_ = {};

    QWidgetList remote_widgets_;
    QWidgetList auth_widgets_;
};
