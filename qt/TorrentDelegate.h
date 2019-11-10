/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <optional>

#include <QStyledItemDelegate>

class QStyle;
class QStyleOptionProgressBar;

class Torrent;

class TorrentDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit TorrentDelegate(QObject* parent = nullptr);
    virtual ~TorrentDelegate();

    // QAbstractItemDelegate
    QSize sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const override;
    void paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override;

protected:
    QSize margin(QStyle const& style) const;
    void setProgressBarPercentDone(QStyleOptionViewItem const& option, Torrent const&) const;
    QIcon& getWarningEmblem() const;

    // Our own overridables
    virtual QSize sizeHint(QStyleOptionViewItem const&, Torrent const&) const;
    virtual void drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const&) const;

    static QString statusString(Torrent const& tor);
    static QString progressString(Torrent const& tor);
    static QString shortStatusString(Torrent const& tor);
    static QString shortTransferString(Torrent const& tor);

protected:
    QStyleOptionProgressBar* myProgressBarStyle;

    static QColor blueBrush;
    static QColor greenBrush;
    static QColor silverBrush;
    static QColor blueBack;
    static QColor greenBack;
    static QColor silverBack;

private:
    mutable std::optional<int> myHeightHint;
    mutable QFont myHeightFont;
    mutable QIcon myWarningEmblem;
};
