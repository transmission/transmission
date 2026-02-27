// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QListView>

class TorrentView : public QListView
{
    Q_OBJECT

public:
    explicit TorrentView(QWidget* parent = nullptr);
    ~TorrentView() override = default;
    TorrentView(TorrentView&&) = delete;
    TorrentView(TorrentView const&) = delete;
    TorrentView& operator=(TorrentView&&) = delete;
    TorrentView& operator=(TorrentView const&) = delete;

public slots:
    void set_header_text(QString const& text);

signals:
    void header_double_clicked();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    class HeaderWidget;

    void adjust_header_position();

    HeaderWidget* const header_widget_ = {};
};
