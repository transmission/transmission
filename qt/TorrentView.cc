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
    explicit HeaderWidget(TorrentView* parent) :
        QWidget(parent)
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

        return QSize(100, fontMetrics().height() + (option.rect.height() - label_rect.height()));
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
    QListView::resizeEvent(event);

    if (header_widget_->isVisible())
    {
        adjustHeaderPosition();
    }
}

void TorrentView::adjustHeaderPosition()
{
    QRect header_widget_rect = contentsRect();
    header_widget_rect.setWidth(viewport()->width());
    header_widget_rect.setHeight(header_widget_->sizeHint().height());
    header_widget_->setGeometry(header_widget_rect);
}
