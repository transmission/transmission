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

class QStyle;
class QStyleOptionProgressBar;

class Torrent;

class TorrentDelegate: public QStyledItemDelegate
{
    Q_OBJECT

  public:
    explicit TorrentDelegate (QObject * parent = nullptr);
    virtual ~TorrentDelegate ();

    // QAbstractItemDelegate
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    virtual void paint(QPainter * painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;

  protected:
    QSize margin (const QStyle& style) const;
    void setProgressBarPercentDone (const QStyleOptionViewItem& option, const Torrent&) const;

    // Our own overridables
    virtual QSize sizeHint (const QStyleOptionViewItem&, const Torrent&) const;
    virtual void drawTorrent (QPainter * painter, const QStyleOptionViewItem& option, const Torrent&) const;

    static QString statusString (const Torrent& tor);
    static QString progressString (const Torrent& tor);
    static QString shortStatusString (const Torrent& tor);
    static QString shortTransferString (const Torrent& tor);

  protected:
    QStyleOptionProgressBar * myProgressBarStyle;

    static QColor blueBrush;
    static QColor greenBrush;
    static QColor silverBrush;
    static QColor blueBack;
    static QColor greenBack;
    static QColor silverBack;
};

#endif // QTR_TORRENT_DELEGATE_H
