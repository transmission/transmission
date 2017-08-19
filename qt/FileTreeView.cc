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
    myModel(new FileTreeModel(this, isEditable)),
    myProxy(new QSortFilterProxyModel(this)),
    myDelegate(new FileTreeDelegate(this))
{
    myProxy->setSourceModel(myModel);
    myProxy->setSortRole(FileTreeModel::SortRole);
    myProxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    setModel(myProxy);
    setItemDelegate(myDelegate);
    sortByColumn(FileTreeModel::COL_NAME, Qt::AscendingOrder);

    connect(this, SIGNAL(clicked(QModelIndex)), this, SLOT(onClicked(QModelIndex)));

    connect(myModel, SIGNAL(priorityChanged(QSet<int>, int)), this, SIGNAL(priorityChanged(QSet<int>, int)));

    connect(myModel, SIGNAL(wantedChanged(QSet<int>, bool)), this, SIGNAL(wantedChanged(QSet<int>, bool)));

    connect(myModel, SIGNAL(pathEdited(QString, QString)), this, SIGNAL(pathEdited(QString, QString)));

    connect(myModel, SIGNAL(openRequested(QString)), this, SIGNAL(openRequested(QString)));
}

void FileTreeView::onClicked(QModelIndex const& proxyIndex)
{
    QModelIndex const modelIndex = myProxy->mapToSource(proxyIndex);

    if (modelIndex.column() == FileTreeModel::COL_WANTED)
    {
        myModel->twiddleWanted(QModelIndexList() << modelIndex);
    }
    else if (modelIndex.column() == FileTreeModel::COL_PRIORITY)
    {
        myModel->twiddlePriority(QModelIndexList() << modelIndex);
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

        QString const headerText = myModel->headerData(column, Qt::Horizontal).toString();
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
                myModel->twiddleWanted(selectedSourceRows());
                return;
            }

            if (modifiers == Qt::ShiftModifier)
            {
                myModel->twiddlePriority(selectedSourceRows());
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
    QModelIndex const rootIndex = myModel->index(0, 0);

    if (!rootIndex.isValid())
    {
        return;
    }

    if (myContextMenu == nullptr)
    {
        initContextMenu();
    }

    myContextMenu->popup(event->globalPos());
}

void FileTreeView::update(FileList const& files, bool updateFields)
{
    bool const modelWasEmpty = myProxy->rowCount() == 0;

    for (TorrentFile const& file : files)
    {
        myModel->addFile(file.index, file.filename, file.wanted, file.priority, file.size, file.have, updateFields);
    }

    if (modelWasEmpty)
    {
        // expand up until the item with more than one expandable child
        for (QModelIndex index = myProxy->index(0, 0); index.isValid();)
        {
            QModelIndex const oldIndex = index;

            expand(oldIndex);

            index = QModelIndex();

            for (int i = 0, count = myProxy->rowCount(oldIndex); i < count; ++i)
            {
                QModelIndex const newIndex = myProxy->index(i, 0, oldIndex);

                if (myProxy->rowCount(newIndex) == 0)
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

    myProxy->sort(header()->sortIndicatorSection(), header()->sortIndicatorOrder());
}

void FileTreeView::clear()
{
    myModel->clear();
}

void FileTreeView::setEditable(bool editable)
{
    myModel->setEditable(editable);
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
    myModel->setWanted(selectedSourceRows(), true);
}

void FileTreeView::uncheckSelectedItems()
{
    myModel->setWanted(selectedSourceRows(), false);
}

void FileTreeView::onlyCheckSelectedItems()
{
    QModelIndex const rootIndex = myModel->index(0, 0);

    if (!rootIndex.isValid())
    {
        return;
    }

    QModelIndexList wantedIndices = selectedSourceRows();
    myModel->setWanted(wantedIndices, true);

    qSort(wantedIndices);

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

        if (qBinaryFind(wantedIndices, parentIndex) != wantedIndices.end())
        {
            continue;
        }

        for (int i = 0, count = myModel->rowCount(parentIndex); i < count; ++i)
        {
            QModelIndex const childIndex = parentIndex.child(i, 0);
            int const childCheckState = childIndex.data(FileTreeModel::WantedRole).toInt();

            if (childCheckState == Qt::Unchecked || qBinaryFind(wantedIndices, childIndex) != wantedIndices.end())
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

    myModel->setWanted(unwantedIndices, false);
}

void FileTreeView::setSelectedItemsPriority()
{
    QAction* action = qobject_cast<QAction*>(sender());
    assert(action != nullptr);
    myModel->setPriority(selectedSourceRows(), action->property(PRIORITY_KEY).toInt());
}

bool FileTreeView::openSelectedItem()
{
    return myModel->openFile(myProxy->mapToSource(currentIndex()));
}

void FileTreeView::renameSelectedItem()
{
    QTreeView::edit(currentIndex());
}

void FileTreeView::refreshContextMenuActionsSensitivity()
{
    assert(myContextMenu != nullptr);

    QModelIndexList const selectedRows = selectionModel()->selectedRows();
    Qt::CheckState const checkState = getCumulativeCheckState(selectedRows);

    bool const haveSelection = !selectedRows.isEmpty();
    bool const haveSingleSelection = selectedRows.size() == 1;
    bool const haveUnchecked = checkState == Qt::Unchecked || checkState == Qt::PartiallyChecked;
    bool const haveChecked = checkState == Qt::Checked || checkState == Qt::PartiallyChecked;

    myCheckSelectedAction->setEnabled(haveUnchecked);
    myUncheckSelectedAction->setEnabled(haveChecked);
    myOnlyCheckSelectedAction->setEnabled(haveSelection);
    myPriorityMenu->setEnabled(haveSelection);
    myOpenAction->setEnabled(haveSingleSelection && selectedRows.first().data(FileTreeModel::FileIndexRole).toInt() >= 0 &&
        selectedRows.first().data(FileTreeModel::CompleteRole).toBool());
    myRenameAction->setEnabled(haveSingleSelection);
}

void FileTreeView::initContextMenu()
{
    myContextMenu = new QMenu(this);

    myCheckSelectedAction = myContextMenu->addAction(tr("Check Selected"), this, SLOT(checkSelectedItems()));
    myUncheckSelectedAction = myContextMenu->addAction(tr("Uncheck Selected"), this, SLOT(uncheckSelectedItems()));
    myOnlyCheckSelectedAction = myContextMenu->addAction(tr("Only Check Selected"), this, SLOT(onlyCheckSelectedItems()));

    myContextMenu->addSeparator();

    myPriorityMenu = myContextMenu->addMenu(tr("Priority"));
    myHighPriorityAction = myPriorityMenu->addAction(FileTreeItem::tr("High"), this, SLOT(setSelectedItemsPriority()));
    myNormalPriorityAction = myPriorityMenu->addAction(FileTreeItem::tr("Normal"), this, SLOT(setSelectedItemsPriority()));
    myLowPriorityAction = myPriorityMenu->addAction(FileTreeItem::tr("Low"), this, SLOT(setSelectedItemsPriority()));

    myHighPriorityAction->setProperty(PRIORITY_KEY, TR_PRI_HIGH);
    myNormalPriorityAction->setProperty(PRIORITY_KEY, TR_PRI_NORMAL);
    myLowPriorityAction->setProperty(PRIORITY_KEY, TR_PRI_LOW);

    myContextMenu->addSeparator();

    myOpenAction = myContextMenu->addAction(tr("Open"), this, SLOT(openSelectedItem()));
    myRenameAction = myContextMenu->addAction(tr("Rename..."), this, SLOT(renameSelectedItem()));

    connect(myContextMenu, SIGNAL(aboutToShow()), SLOT(refreshContextMenuActionsSensitivity()));
}

QModelIndexList FileTreeView::selectedSourceRows(int column) const
{
    QModelIndexList indices;

    for (QModelIndex const& i : selectionModel()->selectedRows(column))
    {
        indices << myProxy->mapToSource(i);
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
