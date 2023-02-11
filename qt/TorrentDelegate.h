// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>

#include <QStyledItemDelegate>

#include <libtransmission/tr-macros.h>

class QStyle;
class QStyleOptionProgressBar;

class Torrent;

class TorrentDelegate : public QStyledItemDelegate
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TorrentDelegate)

public:
    explicit TorrentDelegate(QObject* parent = nullptr);

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

    QColor const BlueBack{ "lightgrey" };
    QColor const BlueBrush{ "steelblue" };
    QColor const GreenBack{ "darkseagreen" };
    QColor const GreenBrush{ "forestgreen" };
    QColor const SilverBack{ "grey" };
    QColor const SilverBrush{ "silver" };

    mutable QStyleOptionProgressBar progress_bar_style_ = {};

private:
    mutable std::optional<int> height_hint_;
    mutable QFont height_font_;
    mutable QIcon warning_emblem_;
};
