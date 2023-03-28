// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QStyleOptionHeader>
#include <QStylePainter>
#include <QHeaderView>

#include "TorrentModel.h"
#include "TorrentView.h"
#include "TorrentDelegate.h"
#include "ProgressbarDelegate.h"

class TorrentView::HeaderWidget : public QWidget
{
public:
    explicit HeaderWidget(TorrentView* parent)
        : QWidget(parent)
    {
        setFont(QApplication::font("QMiniFont"));
    }

    void setText(QString const& text)
    {
        text_ = text;
        update();
    }

    // QWidget
    [[nodiscard]] QSize sizeHint() const override
    {
        QStyleOptionHeader option;
        option.rect = QRect(0, 0, 100, 100);

        QRect const label_rect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);

        return { 100, fontMetrics().height() + (option.rect.height() - label_rect.height()) };
    }

protected:
    // QWidget
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QStyleOptionHeader option;
        option.initFrom(this);
        option.state = QStyle::State_Enabled;
        option.position = QStyleOptionHeader::OnlyOneSection;

        QStylePainter painter(this);
        painter.drawControl(QStyle::CE_HeaderSection, option);

        option.rect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);
        painter.drawItemText(option.rect, Qt::AlignCenter, option.palette, true, text_, QPalette::ButtonText);
    }

    void mouseDoubleClickEvent(QMouseEvent* /*event*/) override
    {
        emit dynamic_cast<TorrentView*>(parent())->headerDoubleClicked();
    }

private:
    QString text_;
};

TorrentView::TorrentView(QWidget* parent)
    : QTableView(parent)
    , header_widget_(new HeaderWidget(this))
{
    setShowGrid(false);
    verticalHeader()->hide();
    horizontalHeader()->hide();
    setSortingEnabled(true);
    setWordWrap(false);
    setTextElideMode(Qt::ElideMiddle);
    setSelectionBehavior(SelectionBehavior::SelectRows);
    horizontalHeader()->setSectionsMovable(true);
    horizontalHeader()->setStretchLastSection(true);

    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->setCascadingSectionResizes(true);
}

void TorrentView::setHeaderText(QString const& text)
{
    bool const header_visible = !text.isEmpty();

    header_widget_->setText(text);
    header_widget_->setVisible(header_visible);

    if (header_visible)
    {
        adjustHeaderPosition();
    }

    setViewportMargins(0, header_visible ? header_widget_->height() : 0, 0, 0);
}

void TorrentView::resizeEvent(QResizeEvent* event)
{
    QTableView::resizeEvent(event);

    if (header_widget_->isVisible())
    {
        adjustHeaderPosition();
    }

    if (auto* delegate = dynamic_cast<TorrentDelegate*>(this->itemDelegate()))
    {
        setColumnWidth(0, this->width() - style()->pixelMetric(QStyle::PM_DefaultFrameWidth));
    }

    this->resizeRowsToContents();
}

void TorrentView::adjustHeaderPosition()
{
    QRect header_widget_rect = contentsRect();
    header_widget_rect.setWidth(viewport()->width());
    header_widget_rect.setHeight(header_widget_->sizeHint().height());
    header_widget_->setGeometry(header_widget_rect);
}

void TorrentView::setCompactView(bool active)
{
    if (active)
    {
        horizontalHeader()->show();
    }
    else
    {
        horizontalHeader()->hide();

        for (int i = 1; i < model()->columnCount(); i++)
        {
            hideColumn(i);
        }

        setColumnWidth(0, this->width() - style()->pixelMetric(QStyle::PM_DefaultFrameWidth));
    }
}

void TorrentView::setColumns(QString const& columns)
{
    if (auto* delegate = dynamic_cast<TorrentDelegate*>(this->itemDelegate())) // not using compact view
    {
        for (int i = 1; i < model()->columnCount(); i++)
        {
            hideColumn(i);
        }

        setColumnWidth(0, this->width() - style()->pixelMetric(QStyle::PM_DefaultFrameWidth));
    }
    else
    {
        this->setItemDelegateForColumn(TorrentModel::Columns::COL_PROGRESS, new ProgressbarDelegate);

        for (int i = 0; i < model()->columnCount(); i++)
        {
            columns[i] == QStringLiteral("1") ? showColumn(i) : hideColumn(i);
        }
    }

    resizeColumnsToContents();
}
