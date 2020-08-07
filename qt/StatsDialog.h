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
#include "ui_StatsDialog.h"

class QTimer;

class Session;

class StatsDialog : public BaseDialog
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(StatsDialog)

public:
    StatsDialog(Session&, QWidget* parent = nullptr);

    // QWidget
    void setVisible(bool visible) override;

private slots:
    void updateStats();

private:
    Session& session_;

    Ui::StatsDialog ui_ = {};

    QTimer* timer_ = {};
};
