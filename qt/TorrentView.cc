// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QHeaderView>
#include <QScrollBar>
#include <QStyleOptionHeader>
#include <QStylePainter>

#include "Prefs.h"
#include "ProgressbarDelegate.h"
#include "TorrentModel.h"
#include "TorrentView.h"
#include "TorrentDelegate.h"
#include "Filters.h"

class TorrentView::HeaderWidget : public QWidget
{
public:
    explicit HeaderWidget(TorrentView* parent)
        : QWidget{ parent }
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
        emit dynamic_cast<TorrentView*>(parent())->headerDoubleClicked();
    }

private:
    QString text_;
};

TorrentView::TorrentView(QWidget* parent)
    : QTableView{ parent }
    , header_widget_{ new HeaderWidget{ this } }
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

    // only look at the currently visible items so columns do not become too wide when there are long out-of-view items
    horizontalHeader()->setResizeContentsPrecision(0);

    connect(horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, &TorrentView::sortChanged);

    window()->installEventFilter(this);
}

void TorrentView::linkPrefs(Prefs* prefs)
{
    prefs_ = prefs;
    connect(prefs, &Prefs::changed, this, &TorrentView::onPrefChanged);

    setColumnsState(QByteArray::fromBase64(prefs_->getString(Prefs::COMPACT_COLUMNS_STATE).toUtf8()));
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
        int const actual_width = verticalScrollBar()->isVisible() ?
            this->width() - style()->pixelMetric(QStyle::PM_ScrollBarExtent) :
            this->width();

        setColumnWidth(0, actual_width - style()->pixelMetric(QStyle::PM_DefaultFrameWidth));
    }

    resizeRowsToContents();
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
        horizontalHeader()->restoreState(column_state_);
    }
    else
    {
        column_state_ = horizontalHeader()->saveState();

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
}

void TorrentView::setColumnsState(QByteArray const& columns_state)
{
    column_state_ = columns_state;
    horizontalHeader()->restoreState(columns_state);
}

// applying a change made via clicking on the little arrow in a column header to the state in View > Sort by X
void TorrentView::sortChanged(int logicalIndex, Qt::SortOrder order)
{
    bool const reversed = order == Qt::DescendingOrder;

    switch (logicalIndex)
    {
    case TorrentModel::COL_QUEUE_POSITION:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_QUEUE));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_SIZE:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_SIZE));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_ADDED_ON:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_AGE));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_ID:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_ID));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_ACTIVITY:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_ACTIVITY));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_STATUS:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_STATE));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_PROGRESS:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_PROGRESS));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_RATIO:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_RATIO));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_ETA:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_ETA));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    case TorrentModel::COL_NAME:
        prefs_->set(Prefs::SORT_MODE, SortMode(SortMode::SORT_BY_NAME));
        prefs_->set(Prefs::SORT_REVERSED, reversed);
        break;

    default:
        break;
    }
}

// applying a change made via clicking View > Sort by X to the state of the little arrow in the column header
void TorrentView::onPrefChanged(int key)
{
    if (key == Prefs::SORT_MODE || key == Prefs::SORT_REVERSED)
    {
        auto reversed = prefs_->getBool(Prefs::SORT_REVERSED) ? Qt::DescendingOrder : Qt::AscendingOrder;

        switch (prefs_->get<SortMode>(Prefs::SORT_MODE).mode())
        {
        case SortMode::SORT_BY_QUEUE:
            sortByColumn(TorrentModel::COL_QUEUE_POSITION, reversed);
            break;

        case SortMode::SORT_BY_SIZE:
            sortByColumn(TorrentModel::COL_SIZE, reversed);
            break;

        case SortMode::SORT_BY_AGE:
            sortByColumn(TorrentModel::COL_ADDED_ON, reversed);
            break;

        case SortMode::SORT_BY_ID:
            sortByColumn(TorrentModel::COL_ID, reversed);
            break;

        case SortMode::SORT_BY_ACTIVITY:
            sortByColumn(TorrentModel::COL_ACTIVITY, reversed);
            break;

        case SortMode::SORT_BY_STATE:
            sortByColumn(TorrentModel::COL_STATUS, reversed);
            break;

        case SortMode::SORT_BY_PROGRESS:
            sortByColumn(TorrentModel::COL_PROGRESS, reversed);
            break;

        case SortMode::SORT_BY_RATIO:
            sortByColumn(TorrentModel::COL_RATIO, reversed);
            break;

        case SortMode::SORT_BY_ETA:
            sortByColumn(TorrentModel::COL_ETA, reversed);
            break;

        case SortMode::SORT_BY_NAME:
            sortByColumn(TorrentModel::COL_NAME, reversed);

        default:
            break;
        }
    }
}

bool TorrentView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == this->window() && event->type() == QEvent::Close)
    {
        prefs_->set(Prefs::COMPACT_COLUMNS_STATE, horizontalHeader()->saveState().toBase64());
    }

    return QAbstractItemView::eventFilter(watched, event);
}
