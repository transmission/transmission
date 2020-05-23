/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QObject>
#include <QSet>

class QGridLayout;
class QTimer;

class ColumnResizer : public QObject
{
    Q_OBJECT

public:
    ColumnResizer(QObject* parent = nullptr);

    void addLayout(QGridLayout* layout);

    // QObject
    bool eventFilter(QObject* object, QEvent* event) override;

public slots:
    void update();

private:
    void scheduleUpdate();

private:
    QTimer* myTimer;
    QSet<QGridLayout*> myLayouts;
};
