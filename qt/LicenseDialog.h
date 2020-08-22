/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "BaseDialog.h"
#include "Macros.h"
#include "ui_LicenseDialog.h"

class LicenseDialog : public BaseDialog
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(LicenseDialog)

public:
    LicenseDialog(QWidget* parent = nullptr);

private:
    Ui::LicenseDialog ui_ = {};
};
