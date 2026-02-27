// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QLabel>
#include <QString>
#include <QTimer>

class Session;
struct tr_variant;

class FreeSpaceLabel : public QLabel
{
    Q_OBJECT

public:
    explicit FreeSpaceLabel(QWidget* parent = nullptr);
    ~FreeSpaceLabel() override = default;
    FreeSpaceLabel(FreeSpaceLabel&&) = delete;
    FreeSpaceLabel(FreeSpaceLabel const&) = delete;
    FreeSpaceLabel& operator=(FreeSpaceLabel&&) = delete;
    FreeSpaceLabel& operator=(FreeSpaceLabel const&) = delete;

    void set_session(Session& session);
    void set_path(QString const& path);

private slots:
    void on_timer();

private:
    Session* session_ = {};
    QString path_;
    QTimer timer_;
};
