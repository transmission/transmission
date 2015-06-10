/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_DELEGATE_H
#define QTR_TORRENT_DELEGATE_H

#include <QStyledItemDelegate>
#include <QSize>

class QStyleOptionProgressBar;
class QStyleOptionViewItem;
class QStyle;
class Session;
class Torrent;

class TorrentDelegate: public QStyledItemDelegate
{
    Q_OBJECT

  public:
    static QColor blueBrush, greenBrush, silverBrush;
    static QColor blueBack,  greenBack, silverBack;

  protected:
    QStyleOptionProgressBar * myProgressBarStyle;

  protected:
    QString statusString (const Torrent& tor) const;
    QString progressString (const Torrent& tor) const;
    QString shortStatusString (const Torrent& tor) const;
    QString shortTransferString (const Torrent& tor) const;

  protected:
    QSize margin (const QStyle& style) const;
    virtual QSize sizeHint (const QStyleOptionViewItem&, const Torrent&) const;
    virtual void setProgressBarPercentDone (const QStyleOptionViewItem& option, const Torrent&) const;
    virtual void drawTorrent (QPainter* painter, const QStyleOptionViewItem& option, const Torrent&) const;

  public:
    explicit TorrentDelegate (QObject * parent=0);
    virtual ~TorrentDelegate ();

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
};

#endif // QTR_TORRENT_DELEGATE_H
