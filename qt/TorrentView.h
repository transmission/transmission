/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QListView>

class TorrentView : public QListView
{
    Q_OBJECT

public:
    TorrentView(QWidget* parent = nullptr);

public slots:
    void setHeaderText(QString const& text);

signals:
    void headerDoubleClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    class HeaderWidget;

private:
    void adjustHeaderPosition();

private:
    HeaderWidget* const myHeaderWidget;
};
