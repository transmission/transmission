// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QComboBox>

#include <libtransmission/tr-macros.h>

class FilterBarComboBox : public QComboBox
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FilterBarComboBox)

public:
    enum
    {
        CountRole = Qt::UserRole + 1,
        CountStringRole,
        UserRole
    };

    explicit FilterBarComboBox(QWidget* parent = nullptr);

    // QWidget
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    // QWidget
    void paintEvent(QPaintEvent* e) override;

private:
    QSize calculateSize(QSize const& text_size, QSize const& count_size) const;
};
