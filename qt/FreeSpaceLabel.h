// This file Copyright © 2013-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QLabel>
#include <QString>
#include <QTimer>

#include <libtransmission/tr-macros.h>

class Session;

extern "C"
{
    struct tr_variant;
}

class FreeSpaceLabel : public QLabel
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FreeSpaceLabel)

public:
    explicit FreeSpaceLabel(QWidget* parent = nullptr);

    void setSession(Session& session);
    void setPath(QString const& path);

private slots:
    void onTimer();

private:
    Session* session_ = {};
    QString path_;
    QTimer timer_;
};
