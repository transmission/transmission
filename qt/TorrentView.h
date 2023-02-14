// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QListView>

#include <libtransmission/tr-macros.h>

class TorrentView : public QListView
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TorrentView)

public:
    explicit TorrentView(QWidget* parent = nullptr);

public slots:
    void setHeaderText(QString const& text);

signals:
    void headerDoubleClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    class HeaderWidget;

    void adjustHeaderPosition();

    HeaderWidget* const header_widget_ = {};
};
