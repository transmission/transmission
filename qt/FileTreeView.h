/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QSet>
#include <QTreeView>

#include "Torrent.h" // FileList

class QAction;
class QMenu;
class QSortFilterProxyModel;

class FileTreeDelegate;
class FileTreeModel;

class FileTreeView : public QTreeView
{
    Q_OBJECT

public:
    FileTreeView(QWidget* parent = nullptr, bool editable = true);

    void clear();
    void update(FileList const& files, bool updateProperties = true);

    void setEditable(bool editable);

signals:
    void priorityChanged(QSet<int> const& fileIndices, int priority);
    void wantedChanged(QSet<int> const& fileIndices, bool wanted);
    void pathEdited(QString const& oldpath, QString const& newname);
    void openRequested(QString const& path);

protected:
    // QWidget
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    // QAbstractItemView
    bool edit(QModelIndex const& index, EditTrigger trigger, QEvent* event) override;

private slots:
    void onClicked(QModelIndex const& index);

    void checkSelectedItems();
    void uncheckSelectedItems();
    void onlyCheckSelectedItems();
    void setSelectedItemsPriority();
    bool openSelectedItem();
    void renameSelectedItem();

    void refreshContextMenuActionsSensitivity();

private:
    void initContextMenu();
    QModelIndexList selectedSourceRows(int column = 0) const;

    static Qt::CheckState getCumulativeCheckState(QModelIndexList const& indices);

private:
    FileTreeModel* model_;
    QSortFilterProxyModel* proxy_;
    FileTreeDelegate* delegate_;

    QMenu* context_menu_ = nullptr;
    QMenu* priority_menu_ = nullptr;
    QAction* check_selected_action_ = nullptr;
    QAction* uncheck_selected_action_ = nullptr;
    QAction* only_check_selected_action_ = nullptr;
    QAction* high_priority_action_ = nullptr;
    QAction* normal_priority_action_ = nullptr;
    QAction* low_priority_action_ = nullptr;
    QAction* open_action_ = nullptr;
    QAction* rename_action_ = nullptr;
};
