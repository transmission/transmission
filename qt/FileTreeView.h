// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QTreeView>

#include "Torrent.h" // FileList
#include "Typedefs.h" // file_indices_t

class QAction;
class QMenu;
class QSortFilterProxyModel;

class FileTreeDelegate;
class FileTreeModel;

class FileTreeView : public QTreeView
{
    Q_OBJECT

public:
    explicit FileTreeView(QWidget* parent = nullptr, bool editable = true);
    ~FileTreeView() override = default;
    FileTreeView(FileTreeView&&) = delete;
    FileTreeView(FileTreeView const&) = delete;
    FileTreeView& operator=(FileTreeView&&) = delete;
    FileTreeView& operator=(FileTreeView const&) = delete;

    void clear();
    void update(FileList const& files, bool update_fields = true);

    void set_editable(bool editable);

signals:
    void priority_changed(file_indices_t const& file_indices, int priority);
    void wanted_changed(file_indices_t const& file_indices, bool wanted);
    void path_edited(QString const& old_path, QString const& new_name);
    void open_requested(QString const& path);

protected:
    // QWidget
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    // QAbstractItemView
    bool edit(QModelIndex const& index, EditTrigger trigger, QEvent* event) override;

private slots:
    void on_clicked(QModelIndex const& index);

    void check_selected_items();
    void uncheck_selected_items();
    void only_check_selected_items();
    void set_selected_items_priority();
    bool open_selected_item();
    void rename_selected_item();

    void refresh_context_menu_actions_sensitivity();

private:
    void init_context_menu();
    [[nodiscard]] QModelIndexList selected_source_rows(int column = 0) const;

    static Qt::CheckState get_cumulative_check_state(QModelIndexList const& indices);

    FileTreeModel* model_ = {};
    QSortFilterProxyModel* proxy_ = {};
    FileTreeDelegate* delegate_ = {};

    QMenu* context_menu_ = {};
    QMenu* priority_menu_ = {};
    QAction* check_selected_action_ = {};
    QAction* uncheck_selected_action_ = {};
    QAction* only_check_selected_action_ = {};
    QAction* high_priority_action_ = {};
    QAction* normal_priority_action_ = {};
    QAction* low_priority_action_ = {};
    QAction* open_action_ = {};
    QAction* rename_action_ = {};
};
