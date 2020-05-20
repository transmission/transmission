/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cstdint>

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariant>

class FileTreeItem
{
    Q_DECLARE_TR_FUNCTIONS(FileTreeItem)
    Q_DISABLE_COPY(FileTreeItem)

public:
/* *INDENT-OFF* */
    enum
    {
        LOW = (1 << 0),
        NORMAL = (1 << 1),
        HIGH = (1 << 2)
    };
/* *INDENT-ON* */

public:
    FileTreeItem(QString const& name = QString(), int fileIndex = -1, uint64_t size = 0) :
        name_(name),
        file_index_(fileIndex),
        total_size_(size),
        parent_(nullptr),
        priority_(0),
        is_wanted_(false),
        have_size_(0),
        first_unhashed_row_(0)
    {
    }

    ~FileTreeItem();

public:
    void appendChild(FileTreeItem* child);
    FileTreeItem* child(QString const& filename);

    FileTreeItem* child(int row)
    {
        return children_.at(row);
    }

    int childCount() const
    {
        return children_.size();
    }

    FileTreeItem* parent()
    {
        return parent_;
    }

    FileTreeItem const* parent() const
    {
        return parent_;
    }

    int row() const;

    QString const& name() const
    {
        return name_;
    }

    QVariant data(int column, int role) const;
    std::pair<int, int> update(QString const& name, bool want, int priority, uint64_t have, bool updateFields);
    void setSubtreeWanted(bool, QSet<int>& fileIds);
    void setSubtreePriority(int priority, QSet<int>& fileIds);

    int fileIndex() const
    {
        return file_index_;
    }

    uint64_t totalSize() const
    {
        return total_size_;
    }

    QString path() const;
    bool isComplete() const;
    int priority() const;
    int isSubtreeWanted() const;

private:
    QString priorityString() const;
    QString sizeString() const;
    void getSubtreeWantedSize(uint64_t& have, uint64_t& total) const;
    double progress() const;
    uint64_t size() const;
    QHash<QString, int> const& getMyChildRows();

private:
    QString name_;
    int const file_index_;
    uint64_t const total_size_;

    FileTreeItem* parent_;
    QList<FileTreeItem*> children_;
    QHash<QString, int> child_rows_;
    int priority_;
    bool is_wanted_;
    uint64_t have_size_;
    size_t first_unhashed_row_;
};
