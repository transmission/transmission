// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QItemDelegate>

#include <libtransmission/tr-macros.h>

class QStyle;

class Session;
struct TrackerInfo;

class TrackerDelegate : public QItemDelegate
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TrackerDelegate)

public:
    explicit TrackerDelegate(QObject* parent = nullptr)
        : QItemDelegate(parent)
    {
    }

    void setShowMore(bool b);

    // QAbstractItemDelegate
    QSize sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const override;
    void paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override;

protected:
    QString getText(TrackerInfo const&) const;

    QSize sizeHint(QStyleOptionViewItem const&, TrackerInfo const&) const;
    void drawTracker(QPainter*, QStyleOptionViewItem const&, TrackerInfo const&) const;

private:
    bool show_more_ = false;
};
