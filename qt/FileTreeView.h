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

class QSortFilterProxyModel;

class FileTreeDelegate;
class FileTreeModel;

class FileTreeView: public QTreeView
{
    Q_OBJECT

  public:
    FileTreeView (QWidget * parent=0, bool editable=true);
    void clear ();
    void update (const FileList& files, bool updateProperties=true);

    void setEditable (bool editable);

  signals:
    void priorityChanged (const QSet<int>& fileIndices, int priority);
    void wantedChanged (const QSet<int>& fileIndices, bool wanted);
    void pathEdited (const QString& oldpath, const QString& newname);
    void openRequested (const QString& path);

  protected:
    bool eventFilter (QObject *, QEvent *);

  private:
    FileTreeModel * myModel;
    QSortFilterProxyModel * myProxy;
    FileTreeDelegate * myDelegate;

  public slots:
    void onClicked (const QModelIndex& index);
    void onDoubleClicked (const QModelIndex& index);
    void onOpenRequested (const QString& path);
};

#endif // QTR_FILE_TREE_VIEW_H
