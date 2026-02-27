// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QString>
#include <QVariant>

#include "Utils.h" // for std::hash<QString>
#include "Typedefs.h"

#include "libtransmission/tr-macros.h"

class FileTreeItem
{
    Q_DECLARE_TR_FUNCTIONS(FileTreeItem)

public:
    static int constexpr Low = 1 << 0;
    static int constexpr Normal = 1 << 1;
    static int constexpr High = 1 << 2;

    explicit FileTreeItem(QString name = QString{}, int file_index = -1, uint64_t size = 0)
        : name_{ std::move(name) }
        , total_size_{ size }
        , file_index_{ file_index }
    {
    }

    FileTreeItem& operator=(FileTreeItem&&) = delete;
    FileTreeItem& operator=(FileTreeItem const&) = delete;
    FileTreeItem(FileTreeItem&&) = delete;
    FileTreeItem(FileTreeItem const&) = delete;
    ~FileTreeItem();

    void append_child(FileTreeItem* child);
    FileTreeItem* child(QString const& filename);

    FileTreeItem* child(int row)
    {
        return children_.at(row);
    }

    [[nodiscard]] constexpr int child_count() const noexcept
    {
        return std::size(children_);
    }

    [[nodiscard]] constexpr auto* parent() noexcept
    {
        return parent_;
    }

    [[nodiscard]] constexpr auto const* parent() const noexcept
    {
        return parent_;
    }

    int row() const;

    [[nodiscard]] constexpr auto const& name() const noexcept
    {
        return name_;
    }

    QVariant data(int column, int role) const;
    std::pair<int, int> update(QString const& name, bool want, int priority, uint64_t have, bool update_fields);
    void set_subtree_wanted(bool wanted, file_indices_t& setme_changed_ids);
    void set_subtree_priority(int priority, file_indices_t& setme_changed_ids);

    [[nodiscard]] constexpr auto file_index() const noexcept
    {
        return file_index_;
    }

    [[nodiscard]] constexpr auto total_size() const noexcept
    {
        return total_size_;
    }

    [[nodiscard]] constexpr auto is_complete() const noexcept
    {
        return have_size_ == total_size();
    }

    QString path() const;
    int priority() const;
    int is_subtree_wanted() const;

private:
    QString priority_string() const;
    QString size_string() const;
    std::pair<uint64_t, uint64_t> get_subtree_wanted_size() const;
    double progress() const;
    uint64_t size() const;
    std::unordered_map<QString, int> const& get_my_child_rows() const;

    FileTreeItem* parent_ = {};
    mutable std::unordered_map<QString, int> child_rows_;
    std::vector<FileTreeItem*> children_;
    QString name_;
    uint64_t const total_size_ = {};
    uint64_t have_size_ = {};
    mutable int first_unhashed_row_ = {};
    int const file_index_ = {};
    int priority_ = {};
    bool is_wanted_ = {};
};
