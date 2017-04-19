/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "BaseDialog.h"

#include "ui_LicenseDialog.h"

class LicenseDialog : public BaseDialog
{
    Q_OBJECT

public:
    LicenseDialog(QWidget* parent = nullptr);

    virtual ~LicenseDialog()
    {
    }

private:
    Ui::LicenseDialog ui;
};
