/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cassert>
#include <set>

#include <QApplication>
#include <QStyle>

#include <libtransmission/transmission.h> // priorities

#include "FileTreeItem.h"
#include "FileTreeModel.h"
#include "Formatter.h"
#include "IconCache.h"

QHash<QString, int> const& FileTreeItem::getMyChildRows()
{
    int const n = childCount();

    // ensure that all the rows are hashed
    while (first_unhashed_row_ < n)
    {
        child_rows_.insert(children_[first_unhashed_row_]->name(), first_unhashed_row_);
        ++first_unhashed_row_;
    }

    return child_rows_;
}

FileTreeItem::~FileTreeItem()
{
    assert(children_.isEmpty());

    if (parent_ != nullptr)
    {
        int const pos = row();
        assert(pos >= 0 && "couldn't find child in parent's lookup");
        parent_->children_.removeAt(pos);
        parent_->child_rows_.remove(name());
        parent_->first_unhashed_row_ = pos;
    }
}

void FileTreeItem::appendChild(FileTreeItem* child)
{
    int const n = childCount();
    child->parent_ = this;
    children_.append(child);
    first_unhashed_row_ = n;
}

FileTreeItem* FileTreeItem::child(QString const& filename)
{
    FileTreeItem* item(nullptr);

    int const row = getMyChildRows().value(filename, -1);

    if (row != -1)
    {
        item = child(row);
        assert(filename == item->name());
    }

    return item;
}

int FileTreeItem::row() const
{
    int i(-1);

    if (parent_ != nullptr)
    {
        i = parent_->getMyChildRows().value(name(), -1);
        assert(this == parent_->children_[i]);
    }

    return i;
}

QVariant FileTreeItem::data(int column, int role) const
{
    QVariant value;

    switch (role)
    {
    case FileTreeModel::FileIndexRole:
        value.setValue(file_index_);
        break;

    case FileTreeModel::WantedRole:
        value.setValue(isSubtreeWanted());
        break;

    case FileTreeModel::CompleteRole:
        value.setValue(isComplete());
        break;

    case Qt::ToolTipRole:
    case Qt::EditRole:
        if (column == FileTreeModel::COL_NAME)
        {
            value.setValue(name());
        }

        break;

    case Qt::TextAlignmentRole:
        if (column == FileTreeModel::COL_SIZE)
        {
            value = Qt::AlignRight + Qt::AlignVCenter;
        }

        break;

    case Qt::DisplayRole:
    case FileTreeModel::SortRole:
        switch (column)
        {
        case FileTreeModel::COL_NAME:
            value.setValue(name());
            break;

        case FileTreeModel::COL_SIZE:
            if (role == Qt::DisplayRole)
            {
                value.setValue(sizeString());
            }
            else
            {
                value.setValue<quint64>(size());
            }

            break;

        case FileTreeModel::COL_PROGRESS:
            value.setValue(progress());
            break;

        case FileTreeModel::COL_WANTED:
            value.setValue(isSubtreeWanted());
            break;

        case FileTreeModel::COL_PRIORITY:
            if (role == Qt::DisplayRole)
            {
                value.setValue(priorityString());
            }
            else
            {
                value.setValue(priority());
            }

            break;
        }

        break;

    case Qt::DecorationRole:
        if (column == FileTreeModel::COL_NAME)
        {
            if (file_index_ < 0)
            {
                value = QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon);
            }
            else
            {
                auto const& icon_cache = IconCache::get();
                value = childCount() > 0 ?
                    icon_cache.folderIcon() :
                    icon_cache.guessMimeIcon(name(), icon_cache.fileIcon());
            }
        }

        break;
    }

    return value;
}

void FileTreeItem::getSubtreeWantedSize(uint64_t& have, uint64_t& total) const
{
    if (is_wanted_)
    {
        have += have_size_;
        total += total_size_;
    }

    for (FileTreeItem const* const i : children_)
    {
        i->getSubtreeWantedSize(have, total);
    }
}

double FileTreeItem::progress() const
{
    double d(0);
    uint64_t have(0);
    uint64_t total(0);

    getSubtreeWantedSize(have, total);

    if (total != 0)
    {
        d = static_cast<double>(have) / static_cast<double>(total);
    }

    return d;
}

QString FileTreeItem::sizeString() const
{
    return Formatter::get().sizeToString(size());
}

uint64_t FileTreeItem::size() const
{
    if (children_.isEmpty())
    {
        return total_size_;
    }

    uint64_t have = 0;
    uint64_t total = 0;
    getSubtreeWantedSize(have, total);
    return total;
}

std::pair<int, int> FileTreeItem::update(QString const& name, bool wanted, int priority, uint64_t have_size, bool update_fields)
{
    auto changed_columns = std::set<int>{};

    if (name_ != name)
    {
        if (parent_ != nullptr)
        {
            parent_->first_unhashed_row_ = row();
        }

        name_ = name;
        changed_columns.insert(FileTreeModel::COL_NAME);
    }

    if (fileIndex() != -1)
    {
        if (have_size_ != have_size)
        {
            have_size_ = have_size;
            changed_columns.insert(FileTreeModel::COL_PROGRESS);
        }

        if (update_fields)
        {
            if (is_wanted_ != wanted)
            {
                is_wanted_ = wanted;
                changed_columns.insert(FileTreeModel::COL_WANTED);
            }

            if (priority_ != priority)
            {
                priority_ = priority;
                changed_columns.insert(FileTreeModel::COL_PRIORITY);
            }
        }
    }

    std::pair<int, int> changed(-1, -1);

    if (!changed_columns.empty())
    {
        changed.first = *std::cbegin(changed_columns);
        changed.second = *std::crbegin(changed_columns);
    }

    return changed;
}

QString FileTreeItem::priorityString() const
{
    int const i = priority();

    switch (i)
    {
    case LOW:
        return tr("Low");

    case HIGH:
        return tr("High");

    case NORMAL:
        return tr("Normal");

    default:
        return tr("Mixed");
    }
}

int FileTreeItem::priority() const
{
    int i(0);

    if (children_.isEmpty())
    {
        switch (priority_)
        {
        case TR_PRI_LOW:
            i |= LOW;
            break;

        case TR_PRI_HIGH:
            i |= HIGH;
            break;

        default:
            i |= NORMAL;
            break;
        }
    }

    for (FileTreeItem const* const child : children_)
    {
        i |= child->priority();
    }

    return i;
}

void FileTreeItem::setSubtreePriority(int i, QSet<int>& ids)
{
    if (priority_ != i)
    {
        priority_ = i;

        if (file_index_ >= 0)
        {
            ids.insert(file_index_);
        }
    }

    for (FileTreeItem* const child : children_)
    {
        child->setSubtreePriority(i, ids);
    }
}

int FileTreeItem::isSubtreeWanted() const
{
    if (children_.isEmpty())
    {
        return is_wanted_ ? Qt::Checked : Qt::Unchecked;
    }

    int wanted(-1);

    for (FileTreeItem const* const child : children_)
    {
        int const child_wanted = child->isSubtreeWanted();

        if (wanted == -1)
        {
            wanted = child_wanted;
        }

        if (wanted != child_wanted)
        {
            wanted = Qt::PartiallyChecked;
        }

        if (wanted == Qt::PartiallyChecked)
        {
            return wanted;
        }
    }

    return wanted;
}

void FileTreeItem::setSubtreeWanted(bool b, QSet<int>& ids)
{
    if (is_wanted_ != b)
    {
        is_wanted_ = b;

        if (file_index_ >= 0)
        {
            ids.insert(file_index_);
        }
    }

    for (FileTreeItem* const child : children_)
    {
        child->setSubtreeWanted(b, ids);
    }
}

QString FileTreeItem::path() const
{
    QString item_path;
    FileTreeItem const* item = this;

    while (item != nullptr && !item->name().isEmpty())
    {
        if (item_path.isEmpty())
        {
            item_path = item->name();
        }
        else
        {
            item_path = item->name() + QLatin1Char('/') + item_path;
        }

        item = item->parent();
    }

    return item_path;
}

bool FileTreeItem::isComplete() const
{
    return have_size_ == totalSize();
}
