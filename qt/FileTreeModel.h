// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint64_t
#include <map>
#include <memory>

#include <QAbstractItemModel>
#include <QSet>

#include <libtransmission/tr-macros.h>

class FileTreeItem;

class FileTreeModel final : public QAbstractItemModel
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FileTreeModel)

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

    FileTreeModel(QObject* parent = nullptr, bool is_editable = true);
    ~FileTreeModel() override;

    void setEditable(bool editable);

    void clear();
    void addFile(
        int index,
        QString const& filename,
        bool wanted,
        int priority,
        uint64_t size,
        uint64_t have,
        bool update_fields);

    bool openFile(QModelIndex const& index);

    void twiddleWanted(QModelIndexList const& indices);
    void twiddlePriority(QModelIndexList const& indices);

    void setWanted(QModelIndexList const& indices, bool wanted);
    void setPriority(QModelIndexList const& indices, int priority);

    QModelIndex parent(QModelIndex const& child, int column) const;

    // QAbstractItemModel
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(QModelIndex const& index) const override;
    QVariant headerData(int column, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, QModelIndex const& parent = {}) const override;
    QModelIndex parent(QModelIndex const& child) const override;
    int rowCount(QModelIndex const& parent = {}) const override;
    int columnCount(QModelIndex const& parent = {}) const override;
    bool setData(QModelIndex const& index, QVariant const& value, int role = Qt::EditRole) override;

signals:
    void priorityChanged(QSet<int> const& file_indices, int);
    void wantedChanged(QSet<int> const& file_indices, bool);
    void pathEdited(QString const& oldpath, QString const& new_name);
    void openRequested(QString const& path);

private:
    void clearSubtree(QModelIndex const&);
    QModelIndex indexOf(FileTreeItem*, int column) const;
    void emitParentsChanged(
        QModelIndex const&,
        int first_column,
        int last_column,
        QSet<QModelIndex>* visited_parent_indices = nullptr);
    void emitSubtreeChanged(QModelIndex const&, int first_column, int last_column);
    FileTreeItem* findItemForFileIndex(int file_index) const;
    FileTreeItem* itemFromIndex(QModelIndex const&) const;
    QModelIndexList getOrphanIndices(QModelIndexList const& indices) const;

    std::map<int, FileTreeItem*> index_cache_;
    std::unique_ptr<FileTreeItem> root_item_;
    bool is_editable_ = {};
};
