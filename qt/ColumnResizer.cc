/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QEvent>
#include <QGridLayout>
#include <QTimer>

#include "ColumnResizer.h"

namespace
{

int itemColumnSpan(QGridLayout* layout, QLayoutItem const* item)
{
    for (int i = 0, count = layout->count(); i < count; ++i)
    {
        if (layout->itemAt(i) != item)
        {
            continue;
        }

        int row = {};
        int column = {};
        int row_span = {};
        int column_span = {};
        layout->getItemPosition(i, &row, &column, &row_span, &column_span);
        return column_span;
    }

    return 0;
}

} // namespace

ColumnResizer::ColumnResizer(QObject* parent) :
    QObject(parent),
    timer_(new QTimer(this))
{
    timer_->setSingleShot(true);
    connect(timer_, SIGNAL(timeout()), SLOT(update()));
}

void ColumnResizer::addLayout(QGridLayout* layout)
{
    layouts_ << layout;
    scheduleUpdate();
}

bool ColumnResizer::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::Resize)
    {
        scheduleUpdate();
    }

    return QObject::eventFilter(object, event);
}

void ColumnResizer::update()
{
    int max_width = 0;

    for (QGridLayout* const layout : layouts_)
    {
        for (int i = 0, count = layout->rowCount(); i < count; ++i)
        {
            QLayoutItem* item = layout->itemAtPosition(i, 0);

            if (item == nullptr || itemColumnSpan(layout, item) > 1)
            {
                continue;
            }

            max_width = qMax(max_width, item->sizeHint().width());
        }
    }

    for (QGridLayout* const layout : layouts_)
    {
        layout->setColumnMinimumWidth(0, max_width);
    }
}

void ColumnResizer::scheduleUpdate()
{
    timer_->start(0);
}
