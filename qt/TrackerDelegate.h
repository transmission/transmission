/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QItemDelegate>

#include "Macros.h"

class QStyle;

class Session;
struct TrackerInfo;

class TrackerDelegate : public QItemDelegate
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TrackerDelegate)

public:
    explicit TrackerDelegate(QObject* parent = nullptr) :
        QItemDelegate(parent)
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
