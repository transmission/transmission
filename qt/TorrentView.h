// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QTableView>

#include <libtransmission/tr-macros.h>

class TorrentView : public QTableView
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TorrentView)

public:
    explicit TorrentView(QWidget* parent = nullptr);
    void linkPrefs(Prefs* prefs);

public slots:
    void setHeaderText(QString const& text);
    void setCompactView(bool active);
    void setColumns(QString const& columns);
    void setColumnsState(QByteArray const& columns_state);
    void sortChanged(int logicalIndex, Qt::SortOrder order);

signals:
    void headerDoubleClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onPrefChanged(int key);

private:
    class HeaderWidget;

    void adjustHeaderPosition();

    Prefs* prefs_;
    QByteArray column_state_ = {};
    HeaderWidget* const header_widget_ = {};
};
