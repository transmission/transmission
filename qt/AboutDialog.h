// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QPointer>

#include <libtransmission/tr-macros.h>

#include "BaseDialog.h"
#include "ui_AboutDialog.h"

class LicenseDialog;
class Session;

class AboutDialog : public BaseDialog
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(AboutDialog)

public:
    explicit AboutDialog(Session& session, QWidget* parent = nullptr);

private slots:
    void showCredits();
    void showLicense();

private:
    Ui::AboutDialog ui_{};

    QPointer<LicenseDialog> license_dialog_;
};
