/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QApplication>
#include <QStyleOptionHeader>
#include <QStylePainter>

#include "TorrentView.h"

class TorrentView::HeaderWidget : public QWidget
{
public:
    HeaderWidget(TorrentView* parent) :
        QWidget(parent),
        text_()
    {
        setFont(qApp->font("QMiniFont"));
    }

    void setText(QString const& text)
    {
        text_ = text;
        update();
    }

    // QWidget
    QSize sizeHint() const override
    {
        QStyleOptionHeader option;
        option.rect = QRect(0, 0, 100, 100);

        QRect const labelRect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);

        return QSize(100, fontMetrics().height() + (option.rect.height() - labelRect.height()));
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
        emit static_cast<TorrentView*>(parent())->headerDoubleClicked();
    }

private:
    QString text_;
};

TorrentView::TorrentView(QWidget* parent) :
    QListView(parent),
    header_widget_(new HeaderWidget(this))
{
}

void TorrentView::setHeaderText(QString const& text)
{
    bool const headerVisible = !text.isEmpty();

    header_widget_->setText(text);
    header_widget_->setVisible(headerVisible);

    if (headerVisible)
    {
        adjustHeaderPosition();
    }

    setViewportMargins(0, headerVisible ? header_widget_->height() : 0, 0, 0);
}

void TorrentView::resizeEvent(QResizeEvent* event)
{
    QListView::resizeEvent(event);

    if (header_widget_->isVisible())
    {
        adjustHeaderPosition();
    }
}

void TorrentView::adjustHeaderPosition()
{
    QRect headerWidgetRect = contentsRect();
    headerWidgetRect.setWidth(viewport()->width());
    headerWidgetRect.setHeight(header_widget_->sizeHint().height());
    header_widget_->setGeometry(headerWidgetRect);
}
