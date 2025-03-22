// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QTimer>

#include "BaseDialog.h"
#include "ui_StatsDialog.h"

class Session;

class StatsDialog : public BaseDialog
{
    Q_OBJECT

public:
    explicit StatsDialog(Session&, QWidget* parent = nullptr);
    StatsDialog(StatsDialog&&) = delete;
    StatsDialog(StatsDialog const&) = delete;
    StatsDialog& operator=(StatsDialog&&) = delete;
    StatsDialog& operator=(StatsDialog const&) = delete;

    // QWidget
    void setVisible(bool visible) override;

private slots:
    void updateStats();

private:
    Session& session_;

    Ui::StatsDialog ui_ = {};

    QTimer timer_;
};
