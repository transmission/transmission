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
        myName(name),
        myFileIndex(fileIndex),
        myTotalSize(size),
        myParent(nullptr),
        myPriority(0),
        myIsWanted(false),
        myHaveSize(0),
        myFirstUnhashedRow(0)
    {
    }

    ~FileTreeItem();

public:
    void appendChild(FileTreeItem* child);
    FileTreeItem* child(QString const& filename);

    FileTreeItem* child(int row)
    {
        return myChildren.at(row);
    }

    int childCount() const
    {
        return myChildren.size();
    }

    FileTreeItem* parent()
    {
        return myParent;
    }

    FileTreeItem const* parent() const
    {
        return myParent;
    }

    int row() const;

    QString const& name() const
    {
        return myName;
    }

    QVariant data(int column, int role) const;
    std::pair<int, int> update(QString const& name, bool want, int priority, uint64_t have, bool updateFields);
    void setSubtreeWanted(bool, QSet<int>& fileIds);
    void setSubtreePriority(int priority, QSet<int>& fileIds);

    int fileIndex() const
    {
        return myFileIndex;
    }

    uint64_t totalSize() const
    {
        return myTotalSize;
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
    QString myName;
    int const myFileIndex;
    uint64_t const myTotalSize;

    FileTreeItem* myParent;
    QList<FileTreeItem*> myChildren;
    QHash<QString, int> myChildRows;
    int myPriority;
    bool myIsWanted;
    uint64_t myHaveSize;
    size_t myFirstUnhashedRow;
};
