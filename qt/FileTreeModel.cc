/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cassert>

#include <libtransmission/transmission.h> // priorities

#include "FileTreeItem.h"
#include "FileTreeModel.h"

namespace
{

class PathIteratorBase
{
protected:
    PathIteratorBase(QString const& path, int slash_index) :
        path_(path),
        slash_index_(slash_index)
    {
        token_.reserve(path.size() / 2);
    }

    QString const& path_;
    QString token_;
    int slash_index_;

    static QChar const SlashChar;
};

QChar const PathIteratorBase::SlashChar = QLatin1Char('/');

class ForwardPathIterator : public PathIteratorBase
{
public:
    explicit ForwardPathIterator(QString const& path) :
        PathIteratorBase(path, path.size() - 1)
    {
    }

    [[nodiscard]] bool hasNext() const
    {
        return slash_index_ > -1;
    }

    QString const& next()
    {
        int new_slash_index = path_.lastIndexOf(SlashChar, slash_index_);
        token_.truncate(0);
        token_ += path_.midRef(new_slash_index + 1, slash_index_ - new_slash_index);
        slash_index_ = new_slash_index - 1;
        return token_;
    }
};

class BackwardPathIterator : public PathIteratorBase
{
public:
    explicit BackwardPathIterator(QString const& path) :
        PathIteratorBase(path, 0)
    {
    }

    [[nodiscard]] bool hasNext() const
    {
        return slash_index_ < path_.size();
    }

    QString const& next()
    {
        int new_slash_index = path_.indexOf(SlashChar, slash_index_);

        if (new_slash_index == -1)
        {
            new_slash_index = path_.size();
        }

        token_.truncate(0);
        token_ += path_.midRef(slash_index_, new_slash_index - slash_index_);
        slash_index_ = new_slash_index + 1;
        return token_;
    }
};

} // namespace

FileTreeModel::FileTreeModel(QObject* parent, bool is_editable) :
    QAbstractItemModel(parent),
    root_item_(new FileTreeItem),
    is_editable_(is_editable)
{
}

FileTreeModel::~FileTreeModel()
{
    clear();

    delete root_item_;
}

void FileTreeModel::setEditable(bool is_editable)
{
    is_editable_ = is_editable;
}

FileTreeItem* FileTreeModel::itemFromIndex(QModelIndex const& index) const
{
    if (!index.isValid())
    {
        return nullptr;
    }

    assert(index.model() == this);
    return static_cast<FileTreeItem*>(index.internalPointer());
}

QModelIndexList FileTreeModel::getOrphanIndices(QModelIndexList const& indices) const
{
    QModelIndexList orphan_indices = indices;

    std::sort(orphan_indices.begin(), orphan_indices.end());

    for (QMutableListIterator<QModelIndex> it(orphan_indices); it.hasNext();)
    {
        QModelIndex walk = it.next();

        for (;;)
        {
            walk = parent(walk, walk.column());

            if (!walk.isValid())
            {
                break;
            }

            if (std::binary_search(orphan_indices.begin(), orphan_indices.end(), walk))
            {
                it.remove();
                break;
            }
        }
    }

    return orphan_indices;
}

QVariant FileTreeModel::data(QModelIndex const& index, int role) const
{
    if (index.isValid())
    {
        return itemFromIndex(index)->data(index.column(), role);
    }

    return {};
}

Qt::ItemFlags FileTreeModel::flags(QModelIndex const& index) const
{
    int i(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    if (is_editable_ && index.column() == COL_NAME)
    {
        i |= Qt::ItemIsEditable;
    }

    if (index.column() == COL_WANTED)
    {
        i |= Qt::ItemIsUserCheckable | Qt::ItemIsTristate;
    }

    return Qt::ItemFlags(i);
}

bool FileTreeModel::setData(QModelIndex const& index, QVariant const& newname, int role)
{
    if (role == Qt::EditRole)
    {
        FileTreeItem const* item = itemFromIndex(index);

        emit pathEdited(item->path(), newname.toString());
    }

    return false; // don't update the view until the session confirms the change
}

QVariant FileTreeModel::headerData(int column, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (column)
        {
        case COL_NAME:
            return tr("File");
            break;

        case COL_SIZE:
            return tr("Size");
            break;

        case COL_PROGRESS:
            return tr("Progress");
            break;

        case COL_WANTED:
            return tr("Download");
            break;

        case COL_PRIORITY:
            return tr("Priority");
            break;

        default:
            break;
        }
    }

    return {};
}

QModelIndex FileTreeModel::index(int row, int column, QModelIndex const& parent) const
{
    QModelIndex i;

    if (hasIndex(row, column, parent))
    {
        FileTreeItem* parent_item;

        if (!parent.isValid())
        {
            parent_item = root_item_;
        }
        else
        {
            parent_item = itemFromIndex(parent);
        }

        FileTreeItem* child_item = parent_item->child(row);

        if (child_item != nullptr)
        {
            i = createIndex(row, column, child_item);
        }
    }

    return i;
}

QModelIndex FileTreeModel::parent(QModelIndex const& child) const
{
    return parent(child, 0); // QAbstractItemModel::parent() wants col 0
}

QModelIndex FileTreeModel::parent(QModelIndex const& child, int column) const
{
    QModelIndex parent;

    if (child.isValid())
    {
        parent = indexOf(itemFromIndex(child)->parent(), column);
    }

    return parent;
}

int FileTreeModel::rowCount(QModelIndex const& parent) const
{
    FileTreeItem const* parent_item = parent.isValid() ?
        itemFromIndex(parent) :
        root_item_;

    return parent_item->childCount();
}

int FileTreeModel::columnCount(QModelIndex const& parent) const
{
    Q_UNUSED(parent)

    return NUM_COLUMNS;
}

QModelIndex FileTreeModel::indexOf(FileTreeItem* item, int column) const
{
    if (item == nullptr || item == root_item_)
    {
        return QModelIndex();
    }

    return createIndex(item->row(), column, item);
}

void FileTreeModel::clearSubtree(QModelIndex const& top)
{
    size_t i = rowCount(top);

    while (i > 0)
    {
        clearSubtree(index(--i, 0, top));
    }

    FileTreeItem* const item = itemFromIndex(top);

    if (item == nullptr)
    {
        return;
    }

    if (item->fileIndex() != -1)
    {
        index_cache_.remove(item->fileIndex());
    }

    delete item;
}

void FileTreeModel::clear()
{
    beginResetModel();
    clearSubtree(QModelIndex());
    endResetModel();

    assert(index_cache_.isEmpty());
}

FileTreeItem* FileTreeModel::findItemForFileIndex(int file_index) const
{
    return index_cache_.value(file_index, nullptr);
}

void FileTreeModel::addFile(int file_index, QString const& filename, bool wanted, int priority, uint64_t total_size,
    uint64_t have, bool update_fields)
{
    FileTreeItem* item;

    item = findItemForFileIndex(file_index);

    if (item != nullptr) // this file is already in the tree, we've added this
    {
        QModelIndex index_with_changed_parents;
        ForwardPathIterator filename_it(filename);

        while (filename_it.hasNext())
        {
            QString const& token = filename_it.next();
            std::pair<int, int> const changed = item->update(token, wanted, priority, have, update_fields);

            if (changed.first >= 0)
            {
                emit dataChanged(indexOf(item, changed.first), indexOf(item, changed.second));

                if (!index_with_changed_parents.isValid() && changed.first <= COL_PRIORITY && changed.second >= COL_SIZE)
                {
                    index_with_changed_parents = indexOf(item, 0);
                }
            }

            item = item->parent();
        }

        assert(item == root_item_);

        if (index_with_changed_parents.isValid())
        {
            emitParentsChanged(index_with_changed_parents, COL_SIZE, COL_PRIORITY);
        }
    }
    else // we haven't build the FileTreeItems for these tokens yet
    {
        bool added = false;

        item = root_item_;
        BackwardPathIterator filename_it(filename);

        while (filename_it.hasNext())
        {
            QString const& token = filename_it.next();
            FileTreeItem* child(item->child(token));

            if (child == nullptr)
            {
                added = true;
                QModelIndex parent_index(indexOf(item, 0));
                int const n(item->childCount());

                beginInsertRows(parent_index, n, n);

                if (!filename_it.hasNext())
                {
                    child = new FileTreeItem(token, file_index, total_size);
                }
                else
                {
                    child = new FileTreeItem(token);
                }

                item->appendChild(child);
                endInsertRows();
            }

            item = child;
        }

        if (item != root_item_)
        {
            assert(item->fileIndex() == file_index);
            assert(item->totalSize() == total_size);

            index_cache_[file_index] = item;

            std::pair<int, int> const changed = item->update(item->name(), wanted, priority, have, added || update_fields);

            if (changed.first >= 0)
            {
                emit dataChanged(indexOf(item, changed.first), indexOf(item, changed.second));
            }
        }
    }
}

void FileTreeModel::emitParentsChanged(QModelIndex const& index, int first_column, int last_column,
    QSet<QModelIndex>* visited_parent_indices)
{
    assert(first_column <= last_column);

    QModelIndex walk = index;

    for (;;)
    {
        walk = parent(walk, first_column);

        if (!walk.isValid())
        {
            break;
        }

        if (visited_parent_indices != nullptr)
        {
            if (visited_parent_indices->contains(walk))
            {
                break;
            }

            visited_parent_indices->insert(walk);
        }

        emit dataChanged(walk, walk.sibling(walk.row(), last_column));
    }
}

void FileTreeModel::emitSubtreeChanged(QModelIndex const& idx, int first_column, int last_column)
{
    assert(first_column <= last_column);

    int const child_count = rowCount(idx);

    if (child_count == 0)
    {
        return;
    }

    // tell everyone that this item changed
    emit dataChanged(index(0, first_column, idx), index(child_count - 1, last_column, idx));

    // walk the subitems
    for (int i = 0; i < child_count; ++i)
    {
        emitSubtreeChanged(index(i, 0, idx), first_column, last_column);
    }
}

void FileTreeModel::twiddleWanted(QModelIndexList const& indices)
{
    QMap<bool, QModelIndexList> wanted_indices;

    for (QModelIndex const& i : getOrphanIndices(indices))
    {
        FileTreeItem const* const item = itemFromIndex(i);
        wanted_indices[item->isSubtreeWanted() != Qt::Checked] << i;
    }

    for (int i = 0; i <= 1; ++i)
    {
        if (wanted_indices.contains(i))
        {
            setWanted(wanted_indices[i], i != 0);
        }
    }
}

void FileTreeModel::twiddlePriority(QModelIndexList const& indices)
{
    QMap<int, QModelIndexList> priority_indices;

    for (QModelIndex const& i : getOrphanIndices(indices))
    {
        FileTreeItem const* const item = itemFromIndex(i);
        int priority = item->priority();

        // ... -> normal -> high -> low -> normal -> ...; mixed -> normal
        if (priority == FileTreeItem::NORMAL)
        {
            priority = TR_PRI_HIGH;
        }
        else if (priority == FileTreeItem::HIGH)
        {
            priority = TR_PRI_LOW;
        }
        else
        {
            priority = TR_PRI_NORMAL;
        }

        priority_indices[priority] << i;
    }

    for (int i = TR_PRI_LOW; i <= TR_PRI_HIGH; ++i)
    {
        if (priority_indices.contains(i))
        {
            setPriority(priority_indices[i], i);
        }
    }
}

void FileTreeModel::setWanted(QModelIndexList const& indices, bool wanted)
{
    if (indices.isEmpty())
    {
        return;
    }

    QModelIndexList const orphan_indices = getOrphanIndices(indices);

    QSet<int> file_ids;

    for (QModelIndex const& i : orphan_indices)
    {
        FileTreeItem* const item = itemFromIndex(i);
        item->setSubtreeWanted(wanted, file_ids);

        emit dataChanged(i, i);
        emitSubtreeChanged(i, COL_WANTED, COL_WANTED);
    }

    // emit parent changes separately to avoid multiple updates for same items
    QSet<QModelIndex> parent_indices;

    for (QModelIndex const& i : orphan_indices)
    {
        emitParentsChanged(i, COL_SIZE, COL_WANTED, &parent_indices);
    }

    if (!file_ids.isEmpty())
    {
        emit wantedChanged(file_ids, wanted);
    }
}

void FileTreeModel::setPriority(QModelIndexList const& indices, int priority)
{
    if (indices.isEmpty())
    {
        return;
    }

    QModelIndexList const orphan_indices = getOrphanIndices(indices);

    QSet<int> file_ids;

    for (QModelIndex const& i : orphan_indices)
    {
        FileTreeItem* const item = itemFromIndex(i);
        item->setSubtreePriority(priority, file_ids);

        emit dataChanged(i, i);
        emitSubtreeChanged(i, COL_PRIORITY, COL_PRIORITY);
    }

    // emit parent changes separately to avoid multiple updates for same items
    QSet<QModelIndex> parent_indices;

    for (QModelIndex const& i : orphan_indices)
    {
        emitParentsChanged(i, COL_PRIORITY, COL_PRIORITY, &parent_indices);
    }

    if (!file_ids.isEmpty())
    {
        emit priorityChanged(file_ids, priority);
    }
}

bool FileTreeModel::openFile(QModelIndex const& index)
{
    if (!index.isValid())
    {
        return false;
    }

    FileTreeItem const* const item = itemFromIndex(index);

    if (item->fileIndex() < 0 || !item->isComplete())
    {
        return false;
    }

    emit openRequested(item->path());
    return true;
}
