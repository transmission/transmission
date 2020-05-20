/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cassert>

#include <QHeaderView>
#include <QMenu>
#include <QQueue>
#include <QResizeEvent>
#include <QSortFilterProxyModel>

#include <libtransmission/transmission.h> // priorities

#include "FileTreeDelegate.h"
#include "FileTreeItem.h"
#include "FileTreeModel.h"
#include "FileTreeView.h"
#include "Formatter.h"
#include "Utils.h"

#define PRIORITY_KEY "priority"

FileTreeView::FileTreeView(QWidget* parent, bool isEditable) :
    QTreeView(parent),
    model_(new FileTreeModel(this, isEditable)),
    proxy_(new QSortFilterProxyModel(this)),
    delegate_(new FileTreeDelegate(this))
{
    proxy_->setSourceModel(model_);
    proxy_->setSortRole(FileTreeModel::SortRole);
    proxy_->setSortCaseSensitivity(Qt::CaseInsensitive);

    setModel(proxy_);
    setItemDelegate(delegate_);
    sortByColumn(FileTreeModel::COL_NAME, Qt::AscendingOrder);

    connect(this, SIGNAL(clicked(QModelIndex)), this, SLOT(onClicked(QModelIndex)));

    connect(model_, SIGNAL(priorityChanged(QSet<int>, int)), this, SIGNAL(priorityChanged(QSet<int>, int)));

    connect(model_, SIGNAL(wantedChanged(QSet<int>, bool)), this, SIGNAL(wantedChanged(QSet<int>, bool)));

    connect(model_, SIGNAL(pathEdited(QString, QString)), this, SIGNAL(pathEdited(QString, QString)));

    connect(model_, SIGNAL(openRequested(QString)), this, SIGNAL(openRequested(QString)));
}

void FileTreeView::onClicked(QModelIndex const& proxyIndex)
{
    QModelIndex const modelIndex = proxy_->mapToSource(proxyIndex);

    if (modelIndex.column() == FileTreeModel::COL_WANTED)
    {
        model_->twiddleWanted(QModelIndexList() << modelIndex);
    }
    else if (modelIndex.column() == FileTreeModel::COL_PRIORITY)
    {
        model_->twiddlePriority(QModelIndexList() << modelIndex);
    }
}

void FileTreeView::resizeEvent(QResizeEvent* event)
{
    QTreeView::resizeEvent(event);

    // this is kind of a hack to get the last four columns be the
    // right size, and to have the filename column use whatever
    // space is left over...

    int left = event->size().width() - 1;

    for (int column = 0; column < FileTreeModel::NUM_COLUMNS; ++column)
    {
        if (column == FileTreeModel::COL_NAME)
        {
            continue;
        }

        int minWidth = 0;

        QStringList itemTexts;

        switch (column)
        {
        case FileTreeModel::COL_SIZE:
            for (int s = Formatter::B; s <= Formatter::TB; ++s)
            {
                itemTexts << QLatin1String("999.9 ") + Formatter::unitStr(Formatter::MEM, static_cast<Formatter::Size>(s));
            }

            break;

        case FileTreeModel::COL_PROGRESS:
            itemTexts << QLatin1String("  100%  ");
            break;

        case FileTreeModel::COL_WANTED:
            minWidth = 20;
            break;

        case FileTreeModel::COL_PRIORITY:
            itemTexts << FileTreeItem::tr("Low") << FileTreeItem::tr("Normal") << FileTreeItem::tr("High") <<
                FileTreeItem::tr("Mixed");
            break;
        }

        int itemWidth = 0;

        for (QString const& itemText : itemTexts)
        {
            itemWidth = std::max(itemWidth, Utils::measureViewItem(this, itemText));
        }

        QString const headerText = model_->headerData(column, Qt::Horizontal).toString();
        int headerWidth = Utils::measureHeaderItem(this->header(), headerText);

        int const width = std::max(minWidth, std::max(itemWidth, headerWidth));
        setColumnWidth(column, width);

        left -= width;
    }

    setColumnWidth(FileTreeModel::COL_NAME, std::max(left, 0));
}

void FileTreeView::keyPressEvent(QKeyEvent* event)
{
    if (state() != EditingState)
    {
        if (event->key() == Qt::Key_Space)
        {
            // handle using the keyboard to toggle the
            // wanted/unwanted state or the file priority

            Qt::KeyboardModifiers const modifiers = event->modifiers();

            if (modifiers == Qt::NoModifier)
            {
                model_->twiddleWanted(selectedSourceRows());
                return;
            }

            if (modifiers == Qt::ShiftModifier)
            {
                model_->twiddlePriority(selectedSourceRows());
                return;
            }
        }
    }

    QTreeView::keyPressEvent(event);
}

void FileTreeView::mouseDoubleClickEvent(QMouseEvent* event)
{
    QModelIndex const index = currentIndex();

    if (!index.isValid() || index.column() == FileTreeModel::COL_WANTED || index.column() == FileTreeModel::COL_PRIORITY)
    {
        return;
    }

    if (openSelectedItem())
    {
        return;
    }

    QTreeView::mouseDoubleClickEvent(event);
}

void FileTreeView::contextMenuEvent(QContextMenuEvent* event)
{
    QModelIndex const rootIndex = model_->index(0, 0);

    if (!rootIndex.isValid())
    {
        return;
    }

    if (context_menu_ == nullptr)
    {
        initContextMenu();
    }

    context_menu_->popup(event->globalPos());
}

void FileTreeView::update(FileList const& files, bool updateFields)
{
    bool const modelWasEmpty = proxy_->rowCount() == 0;

    for (TorrentFile const& file : files)
    {
        model_->addFile(file.index, file.filename, file.wanted, file.priority, file.size, file.have, updateFields);
    }

    if (modelWasEmpty)
    {
        // expand up until the item with more than one expandable child
        for (QModelIndex index = proxy_->index(0, 0); index.isValid();)
        {
            QModelIndex const oldIndex = index;

            expand(oldIndex);

            index = QModelIndex();

            for (int i = 0, count = proxy_->rowCount(oldIndex); i < count; ++i)
            {
                QModelIndex const newIndex = proxy_->index(i, 0, oldIndex);

                if (proxy_->rowCount(newIndex) == 0)
                {
                    continue;
                }

                if (index.isValid())
                {
                    index = QModelIndex();
                    break;
                }

                index = newIndex;
            }
        }
    }

    proxy_->sort(header()->sortIndicatorSection(), header()->sortIndicatorOrder());
}

void FileTreeView::clear()
{
    model_->clear();
}

void FileTreeView::setEditable(bool editable)
{
    model_->setEditable(editable);
}

bool FileTreeView::edit(QModelIndex const& index, EditTrigger trigger, QEvent* event)
{
    if (selectionModel()->selectedRows().size() != 1)
    {
        return false;
    }

    QModelIndex const nameIndex = index.sibling(index.row(), FileTreeModel::COL_NAME);

    if (editTriggers().testFlag(trigger))
    {
        selectionModel()->setCurrentIndex(nameIndex, QItemSelectionModel::NoUpdate);
    }

    return QTreeView::edit(nameIndex, trigger, event);
}

void FileTreeView::checkSelectedItems()
{
    model_->setWanted(selectedSourceRows(), true);
}

void FileTreeView::uncheckSelectedItems()
{
    model_->setWanted(selectedSourceRows(), false);
}

void FileTreeView::onlyCheckSelectedItems()
{
    QModelIndex const rootIndex = model_->index(0, 0);

    if (!rootIndex.isValid())
    {
        return;
    }

    QModelIndexList wantedIndices = selectedSourceRows();
    model_->setWanted(wantedIndices, true);

    std::sort(wantedIndices.begin(), wantedIndices.end());

    QSet<QModelIndex> wantedIndicesParents;

    for (QModelIndex const& i : wantedIndices)
    {
        for (QModelIndex p = i.parent(); p.isValid(); p = p.parent())
        {
            wantedIndicesParents.insert(p);
        }
    }

    QQueue<QModelIndex> parentsQueue;
    parentsQueue.enqueue(rootIndex);
    QModelIndexList unwantedIndices;

    while (!parentsQueue.isEmpty())
    {
        QModelIndex const parentIndex = parentsQueue.dequeue();

        if (std::binary_search(wantedIndices.begin(), wantedIndices.end(), parentIndex))
        {
            continue;
        }

        for (int i = 0, count = model_->rowCount(parentIndex); i < count; ++i)
        {
            QModelIndex const childIndex = parentIndex.child(i, 0);
            int const childCheckState = childIndex.data(FileTreeModel::WantedRole).toInt();

            if (childCheckState == Qt::Unchecked || std::binary_search(wantedIndices.begin(), wantedIndices.end(), childIndex))
            {
                continue;
            }

            if (childCheckState == Qt::Checked && childIndex.data(FileTreeModel::FileIndexRole).toInt() >= 0)
            {
                unwantedIndices << childIndex;
            }
            else
            {
                if (!wantedIndicesParents.contains(childIndex))
                {
                    unwantedIndices << childIndex;
                }
                else
                {
                    parentsQueue.enqueue(childIndex);
                }
            }
        }
    }

    model_->setWanted(unwantedIndices, false);
}

void FileTreeView::setSelectedItemsPriority()
{
    auto* action = qobject_cast<QAction*>(sender());
    assert(action != nullptr);
    model_->setPriority(selectedSourceRows(), action->property(PRIORITY_KEY).toInt());
}

bool FileTreeView::openSelectedItem()
{
    return model_->openFile(proxy_->mapToSource(currentIndex()));
}

void FileTreeView::renameSelectedItem()
{
    QTreeView::edit(currentIndex());
}

void FileTreeView::refreshContextMenuActionsSensitivity()
{
    assert(context_menu_ != nullptr);

    QModelIndexList const selectedRows = selectionModel()->selectedRows();
    Qt::CheckState const checkState = getCumulativeCheckState(selectedRows);

    bool const haveSelection = !selectedRows.isEmpty();
    bool const haveSingleSelection = selectedRows.size() == 1;
    bool const haveUnchecked = checkState == Qt::Unchecked || checkState == Qt::PartiallyChecked;
    bool const haveChecked = checkState == Qt::Checked || checkState == Qt::PartiallyChecked;

    check_selected_action_->setEnabled(haveUnchecked);
    uncheck_selected_action_->setEnabled(haveChecked);
    only_check_selected_action_->setEnabled(haveSelection);
    priority_menu_->setEnabled(haveSelection);
    open_action_->setEnabled(haveSingleSelection && selectedRows.first().data(FileTreeModel::FileIndexRole).toInt() >= 0 &&
        selectedRows.first().data(FileTreeModel::CompleteRole).toBool());
    rename_action_->setEnabled(haveSingleSelection);
}

void FileTreeView::initContextMenu()
{
    context_menu_ = new QMenu(this);

    check_selected_action_ = context_menu_->addAction(tr("Check Selected"), this, SLOT(checkSelectedItems()));
    uncheck_selected_action_ = context_menu_->addAction(tr("Uncheck Selected"), this, SLOT(uncheckSelectedItems()));
    only_check_selected_action_ = context_menu_->addAction(tr("Only Check Selected"), this, SLOT(onlyCheckSelectedItems()));

    context_menu_->addSeparator();

    priority_menu_ = context_menu_->addMenu(tr("Priority"));
    high_priority_action_ = priority_menu_->addAction(FileTreeItem::tr("High"), this, SLOT(setSelectedItemsPriority()));
    normal_priority_action_ = priority_menu_->addAction(FileTreeItem::tr("Normal"), this, SLOT(setSelectedItemsPriority()));
    low_priority_action_ = priority_menu_->addAction(FileTreeItem::tr("Low"), this, SLOT(setSelectedItemsPriority()));

    high_priority_action_->setProperty(PRIORITY_KEY, TR_PRI_HIGH);
    normal_priority_action_->setProperty(PRIORITY_KEY, TR_PRI_NORMAL);
    low_priority_action_->setProperty(PRIORITY_KEY, TR_PRI_LOW);

    context_menu_->addSeparator();

    open_action_ = context_menu_->addAction(tr("Open"), this, SLOT(openSelectedItem()));
    rename_action_ = context_menu_->addAction(tr("Rename..."), this, SLOT(renameSelectedItem()));

    connect(context_menu_, SIGNAL(aboutToShow()), SLOT(refreshContextMenuActionsSensitivity()));
}

QModelIndexList FileTreeView::selectedSourceRows(int column) const
{
    QModelIndexList indices;

    for (QModelIndex const& i : selectionModel()->selectedRows(column))
    {
        indices << proxy_->mapToSource(i);
    }

    return indices;
}

Qt::CheckState FileTreeView::getCumulativeCheckState(QModelIndexList const& indices)
{
    bool haveChecked = false, haveUnchecked = false;

    for (QModelIndex const& i : indices)
    {
        switch (i.data(FileTreeModel::WantedRole).toInt())
        {
        case Qt::Checked:
            haveChecked = true;
            break;

        case Qt::Unchecked:
            haveUnchecked = true;
            break;

        case Qt::PartiallyChecked:
            return Qt::PartiallyChecked;
        }

        if (haveChecked && haveUnchecked)
        {
            return Qt::PartiallyChecked;
        }
    }

    return haveChecked ? Qt::Checked : Qt::Unchecked;
}
