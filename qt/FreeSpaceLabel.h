/*
 * This file Copyright (C) 2013-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QLabel>
#include <QString>
#include <QTimer>

#include "Macros.h"

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
    void setPath(QString const& folder);

private slots:
    void onTimer();

private:
    Session* session_ = {};
    QString path_;
    QTimer timer_;
};
