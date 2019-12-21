/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cstdint>

#include <QAbstractItemModel>
#include <QList>
#include <QMap>
#include <QSet>

class FileTreeItem;

class FileTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum
    {
        COL_NAME,
        COL_SIZE,
        COL_PROGRESS,
        COL_WANTED,
        COL_PRIORITY,
        //
        NUM_COLUMNS
    };

    enum Role
    {
        SortRole = Qt::UserRole,
        FileIndexRole,
        WantedRole,
        CompleteRole
    };

public:
    FileTreeModel(QObject* parent = nullptr, bool isEditable = true);
    ~FileTreeModel();

    void setEditable(bool editable);

    void clear();
    void addFile(int index, QString const& filename, bool wanted, int priority, uint64_t size, uint64_t have,
        bool torrentChanged);

    bool openFile(QModelIndex const& index);

    void twiddleWanted(QModelIndexList const& indices);
    void twiddlePriority(QModelIndexList const& indices);

    void setWanted(QModelIndexList const& indices, bool wanted);
    void setPriority(QModelIndexList const& indices, int priority);

    QModelIndex parent(QModelIndex const& child, int column) const;

    // QAbstractItemModel
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(QModelIndex const& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, QModelIndex const& parent = QModelIndex()) const override;
    QModelIndex parent(QModelIndex const& child) const override;
    int rowCount(QModelIndex const& parent = QModelIndex()) const override;
    int columnCount(QModelIndex const& parent = QModelIndex()) const override;
    bool setData(QModelIndex const& index, QVariant const& value, int role = Qt::EditRole) override;

signals:
    void priorityChanged(QSet<int> const& fileIndices, int);
    void wantedChanged(QSet<int> const& fileIndices, bool);
    void pathEdited(QString const& oldpath, QString const& newname);
    void openRequested(QString const& path);

private:
    void clearSubtree(QModelIndex const&);
    QModelIndex indexOf(FileTreeItem*, int column) const;
    void emitParentsChanged(QModelIndex const&, int firstColumn, int lastColumn,
        QSet<QModelIndex>* visitedParentIndices = nullptr);
    void emitSubtreeChanged(QModelIndex const&, int firstColumn, int lastColumn);
    FileTreeItem* findItemForFileIndex(int fileIndex) const;
    FileTreeItem* itemFromIndex(QModelIndex const&) const;
    QModelIndexList getOrphanIndices(QModelIndexList const& indices) const;

private:
    bool myIsEditable;

    FileTreeItem* myRootItem;
    QMap<int, FileTreeItem*> myIndexCache;
};
