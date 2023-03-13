// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QToolButton>

#include <libtransmission/tr-macros.h>

class IconToolButton : public QToolButton
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(IconToolButton)

public:
    explicit IconToolButton(QWidget* parent = nullptr);

    // QWidget
    QSize sizeHint() const override;

protected:
    // QWidget
    void paintEvent(QPaintEvent* event) override;
};
