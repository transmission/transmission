/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FILE_TREE_VIEW_H
#define QTR_FILE_TREE_VIEW_H

#include <QSet>
#include <QTreeView>

#include "Torrent.h" // FileList

class QAction;
class QMenu;
class QSortFilterProxyModel;

class FileTreeDelegate;
class FileTreeModel;

class FileTreeView: public QTreeView
{
    Q_OBJECT

  public:
    FileTreeView (QWidget * parent = nullptr, bool editable = true);

    void clear ();
    void update (const FileList& files, bool updateProperties = true);

    void setEditable (bool editable);

  signals:
    void priorityChanged (const QSet<int>& fileIndices, int priority);
    void wantedChanged (const QSet<int>& fileIndices, bool wanted);
    void pathEdited (const QString& oldpath, const QString& newname);
    void openRequested (const QString& path);

  protected:
    // QWidget
    virtual void resizeEvent (QResizeEvent * event);
    virtual void keyPressEvent (QKeyEvent * event);
    virtual void mouseDoubleClickEvent (QMouseEvent * event);
    virtual void contextMenuEvent (QContextMenuEvent * event);

    // QAbstractItemView
    virtual bool edit (const QModelIndex& index, EditTrigger trigger, QEvent * event);

  private slots:
    void onClicked (const QModelIndex& index);

    void checkSelectedItems ();
    void uncheckSelectedItems ();
    void onlyCheckSelectedItems ();
    void setSelectedItemsPriority ();
    bool openSelectedItem ();
    void renameSelectedItem ();

    void refreshContextMenuActionsSensitivity ();

  private:
    void initContextMenu ();
    QModelIndexList selectedSourceRows (int column = 0) const;

    static Qt::CheckState getCumulativeCheckState (const QModelIndexList& indices);

  private:
    FileTreeModel * myModel;
    QSortFilterProxyModel * myProxy;
    FileTreeDelegate * myDelegate;

    QMenu * myContextMenu = nullptr;
    QMenu * myPriorityMenu = nullptr;
    QAction * myCheckSelectedAction = nullptr;
    QAction * myUncheckSelectedAction = nullptr;
    QAction * myOnlyCheckSelectedAction = nullptr;
    QAction * myHighPriorityAction = nullptr;
    QAction * myNormalPriorityAction = nullptr;
    QAction * myLowPriorityAction = nullptr;
    QAction * myOpenAction = nullptr;
    QAction * myRenameAction = nullptr;
};

#endif // QTR_FILE_TREE_VIEW_H
