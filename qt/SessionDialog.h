// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QWidgetList>

#include "BaseDialog.h"
#include "ui_SessionDialog.h"

class Prefs;
class Session;

class SessionDialog : public BaseDialog
{
    Q_OBJECT

public:
    SessionDialog(Session& session, Prefs& prefs, QWidget* parent = nullptr);
    SessionDialog(SessionDialog&&) = delete;
    SessionDialog(SessionDialog const&) = delete;
    SessionDialog& operator=(SessionDialog&&) = delete;
    SessionDialog& operator=(SessionDialog const&) = delete;

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
