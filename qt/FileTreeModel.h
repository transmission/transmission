// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint64_t
#include <map>
#include <memory>
#include <set>

#include <QAbstractItemModel>

#include "Typedefs.h" // file_indices_t

class FileTreeItem;

class FileTreeModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum : uint8_t
    {
        COL_NAME,
        COL_SIZE,
        COL_PROGRESS,
        COL_WANTED,
        COL_PRIORITY,
        //
        NUM_COLUMNS
    };

    enum Role : uint16_t
    {
        SortRole = Qt::UserRole,
        FileIndexRole,
        WantedRole,
        CompleteRole
    };

    explicit FileTreeModel(QObject* parent = nullptr, bool is_editable = true);
    FileTreeModel& operator=(FileTreeModel&&) = delete;
    FileTreeModel& operator=(FileTreeModel const&) = delete;
    FileTreeModel(FileTreeModel&&) = delete;
    FileTreeModel(FileTreeModel const&) = delete;
    ~FileTreeModel() override;

    void set_editable(bool editable);

    void clear();
    void add_file(
        int index,
        QString const& filename,
        bool wanted,
        int priority,
        uint64_t size,
        uint64_t have,
        bool update_fields);

    bool open_file(QModelIndex const& index);

    void twiddle_wanted(QModelIndexList const& indices);
    void twiddle_priority(QModelIndexList const& indices);

    void set_wanted(QModelIndexList const& indices, bool wanted);
    void set_priority(QModelIndexList const& indices, int priority);

    [[nodiscard]] QModelIndex parent(QModelIndex const& child, int column) const;

    // QAbstractItemModel
    [[nodiscard]] QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(QModelIndex const& index) const override;
    [[nodiscard]] QVariant headerData(int column, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QModelIndex index(int row, int column, QModelIndex const& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(QModelIndex const& child) const override;
    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] int columnCount(QModelIndex const& parent = {}) const override;
    bool setData(QModelIndex const& index, QVariant const& value, int role = Qt::EditRole) override;

signals:
    void priority_changed(file_indices_t const& file_indices, int);
    void wanted_changed(file_indices_t const& file_indices, bool);
    void path_edited(QString const& oldpath, QString const& new_name);
    void open_requested(QString const& path);

private:
    void clear_subtree(QModelIndex const& top);
    QModelIndex index_of(FileTreeItem* item, int column) const;
    void emit_parents_changed(
        QModelIndex const& index,
        int first_column,
        int last_column,
        std::set<QModelIndex>* visited_parent_indices = nullptr);
    void emit_subtree_changed(QModelIndex const& idx, int first_column, int last_column);
    [[nodiscard]] FileTreeItem* find_item_for_file_index(int file_index) const;
    [[nodiscard]] FileTreeItem* item_from_index(QModelIndex const& index) const;
    [[nodiscard]] QModelIndexList get_orphan_indices(QModelIndexList const& indices) const;

    std::map<int, FileTreeItem*> index_cache_;
    std::unique_ptr<FileTreeItem> root_item_;
    bool is_editable_ = {};
};
