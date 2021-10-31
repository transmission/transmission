/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QPointer>

#include "BaseDialog.h"
#include "Macros.h"
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
