/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QComboBox>

class FilterBarComboBox : public QComboBox
{
    Q_OBJECT

public:
    enum
    {
        CountRole = Qt::UserRole + 1,
        CountStringRole,
        UserRole
    };

public:
    FilterBarComboBox(QWidget* parent = nullptr);

    // QWidget
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    // QWidget
    void paintEvent(QPaintEvent* e) override;

private:
    QSize calculateSize(QSize const& textSize, QSize const& countSize) const;
};
