// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#include <vector>

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariant>

#include <libtransmission/tr-macros.h>

class FileTreeItem
{
    Q_DECLARE_TR_FUNCTIONS(FileTreeItem)
    TR_DISABLE_COPY_MOVE(FileTreeItem)

public:
    static auto constexpr Low = int{ 1 << 0 };
    static auto constexpr Normal = int{ 1 << 1 };
    static auto constexpr High = int{ 1 << 2 };

    FileTreeItem(QString const& name = QString(), int file_index = -1, uint64_t size = 0)
        : name_(name)
        , total_size_(size)
        , file_index_(file_index)
    {
    }

    ~FileTreeItem();

    void appendChild(FileTreeItem* child);
    FileTreeItem* child(QString const& filename);

    FileTreeItem* child(int row)
    {
        return children_.at(row);
    }

    [[nodiscard]] TR_CONSTEXPR20 int childCount() const noexcept
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
    void setSubtreeWanted(bool, QSet<int>& file_ids);
    void setSubtreePriority(int priority, QSet<int>& file_ids);

    [[nodiscard]] constexpr auto fileIndex() const noexcept
    {
        return file_index_;
    }

    [[nodiscard]] constexpr auto totalSize() const noexcept
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

    FileTreeItem* parent_ = {};
    QHash<QString, int> child_rows_;
    std::vector<FileTreeItem*> children_;
    QString name_;
    uint64_t const total_size_ = {};
    uint64_t have_size_ = {};
    int first_unhashed_row_ = {};
    int const file_index_ = {};
    int priority_ = {};
    bool is_wanted_ = {};
};
