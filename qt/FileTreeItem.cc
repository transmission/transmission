// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cassert>
#include <set>
#include <utility>

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
    assert(std::empty(children_));

    if (parent_ == nullptr)
    {
        return;
    }

    // find the parent's reference to this child
    auto& siblings = parent_->children_;
    auto it = std::find(std::begin(siblings), std::end(siblings), this);
    if (it == std::end(siblings))
    {
        return;
    }

    // remove this child from the parent
    parent_->child_rows_.remove(name());
    it = siblings.erase(it);

    // invalidate the row numbers of the siblings that came after this child
    parent_->first_unhashed_row_ = std::distance(std::begin(siblings), it);
}

void FileTreeItem::appendChild(FileTreeItem* child)
{
    int const n = childCount();
    child->parent_ = this;
    children_.push_back(child);
    first_unhashed_row_ = n;
}

FileTreeItem* FileTreeItem::child(QString const& filename)
{
    FileTreeItem* item(nullptr);

    if (int const row = getMyChildRows().value(filename, -1); row != -1)
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
        assert(i == -1 || this == parent_->children_[i]);
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
            value = static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
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
                value = childCount() > 0 ? icon_cache.folderIcon() : icon_cache.guessMimeIcon(name(), icon_cache.fileIcon());
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
    if (std::empty(children_))
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
    case Low:
        return tr("Low");

    case High:
        return tr("High");

    case Normal:
        return tr("Normal");

    default:
        return tr("Mixed");
    }
}

int FileTreeItem::priority() const
{
    int i(0);

    if (std::empty(children_))
    {
        switch (priority_)
        {
        case TR_PRI_LOW:
            i |= Low;
            break;

        case TR_PRI_HIGH:
            i |= High;
            break;

        default:
            i |= Normal;
            break;
        }
    }

    for (FileTreeItem const* const child : children_)
    {
        i |= child->priority();
    }

    return i;
}

void FileTreeItem::setSubtreePriority(int priority, QSet<int>& ids)
{
    if (priority_ != priority)
    {
        priority_ = priority;

        if (file_index_ >= 0)
        {
            ids.insert(file_index_);
        }
    }

    for (FileTreeItem* const child : children_)
    {
        child->setSubtreePriority(priority, ids);
    }
}

int FileTreeItem::isSubtreeWanted() const
{
    if (std::empty(children_))
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
