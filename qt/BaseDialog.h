/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

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
