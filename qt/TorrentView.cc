// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QStyleOptionHeader>
#include <QStylePainter>

#include "TorrentView.h"

class TorrentView::HeaderWidget : public QWidget
{
public:
    explicit HeaderWidget(TorrentView* parent)
        : QWidget{ parent }
    {
        setFont(QApplication::font("QMiniFont"));
    }

    void set_text(QString const& text)
    {
        text_ = text;
        update();
    }

    // QWidget
    [[nodiscard]] QSize sizeHint() const override
    {
        QStyleOptionHeader option;
        option.rect = QRect{ 0, 0, 100, 100 };

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

        QStylePainter painter{ this };
        painter.drawControl(QStyle::CE_HeaderSection, option);

        option.rect = style()->subElementRect(QStyle::SE_HeaderLabel, &option, this);
        painter.drawItemText(option.rect, Qt::AlignCenter, option.palette, true, text_, QPalette::ButtonText);
    }

    void mouseDoubleClickEvent(QMouseEvent* /*event*/) override
    {
        emit dynamic_cast<TorrentView*>(parent())->header_double_clicked();
    }

private:
    QString text_;
};

TorrentView::TorrentView(QWidget* parent)
    : QListView{ parent }
    , header_widget_{ new HeaderWidget{ this } }
{
}

void TorrentView::set_header_text(QString const& text)
{
    bool const header_visible = !text.isEmpty();

    header_widget_->set_text(text);
    header_widget_->setVisible(header_visible);

    if (header_visible)
    {
        adjust_header_position();
    }

    setViewportMargins(0, header_visible ? header_widget_->height() : 0, 0, 0);
}

void TorrentView::resizeEvent(QResizeEvent* event)
{
    QListView::resizeEvent(event);

    if (header_widget_->isVisible())
    {
        adjust_header_position();
    }
}

void TorrentView::adjust_header_position()
{
    QRect header_widget_rect = contentsRect();
    header_widget_rect.setWidth(viewport()->width());
    header_widget_rect.setHeight(header_widget_->sizeHint().height());
    header_widget_->setGeometry(header_widget_rect);
}
