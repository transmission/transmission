// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>

#include <QStyledItemDelegate>

#include "NativeIcon.h"

class QStyle;
class QStyleOptionProgressBar;

class Torrent;

class TorrentDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit TorrentDelegate(QObject* parent = nullptr);
    ~TorrentDelegate() override = default;
    TorrentDelegate& operator=(TorrentDelegate&&) = delete;
    TorrentDelegate& operator=(TorrentDelegate const&) = delete;
    TorrentDelegate(TorrentDelegate&&) = delete;
    TorrentDelegate(TorrentDelegate const&) = delete;

    // QAbstractItemDelegate
    QSize sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const override;
    void paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override;

protected:
    QSize margin(QStyle const& style) const;
    void set_progress_bar_percent_done(QStyleOptionViewItem const& option, Torrent const& tor) const;
    QIcon warning_emblem() const
    {
        return warning_emblem_;
    }

    // Our own overridables
    virtual QSize size_hint(QStyleOptionViewItem const& option, Torrent const& tor) const;
    virtual void draw_torrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor) const;

    static QString status_string(Torrent const& tor);
    static QString progress_string(Torrent const& tor);
    static QString short_status_string(Torrent const& tor);
    static QString short_transfer_string(Torrent const& tor);

    static inline QColor const BlueBack{ "lightgrey" };
    static inline QColor const BlueBrush{ "steelblue" };
    static inline QColor const GreenBack{ "darkseagreen" };
    static inline QColor const GreenBrush{ "forestgreen" };
    static inline QColor const SilverBack{ "grey" };
    static inline QColor const SilverBrush{ "silver" };

    mutable QStyleOptionProgressBar progress_bar_style_;

private:
    QIcon const warning_emblem_ = icons::icon(icons::Type::TorrentErrorEmblem);
    mutable std::optional<int> height_hint_;
    mutable QFont height_font_;
};
