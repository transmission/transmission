/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TRACKER_DELEGATE_H
#define QTR_TRACKER_DELEGATE_H

#include <QItemDelegate>
#include <QSize>

class QPainter;
class QStyleOptionViewItem;
class QStyle;

class Session;
struct TrackerInfo;

class TrackerDelegate: public QItemDelegate
{
    Q_OBJECT

  public:
    TrackerDelegate (QObject * parent=0): QItemDelegate(parent), myShowMore(false) {}
    virtual ~TrackerDelegate () {}

  public:
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;

  public:
    void setShowMore (bool b);

  protected:
    QString getText (const TrackerInfo&) const;
    QSize margin (const QStyle& style) const;
    virtual QSize sizeHint (const QStyleOptionViewItem&, const TrackerInfo&) const;
    void drawTracker (QPainter*, const QStyleOptionViewItem&, const TrackerInfo&) const;

  private:
    bool myShowMore;
};

#endif // QTR_TRACKER_DELEGATE_H
