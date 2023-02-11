// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QDialog>

class BaseDialog : public QDialog
{
public:
    BaseDialog(QWidget* parent = nullptr, Qt::WindowFlags flags = {})
        : QDialog(parent, flags)
    {
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    }
};
