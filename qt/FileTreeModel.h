/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FILE_TREE_MODEL_H
#define QTR_FILE_TREE_MODEL_H

#include <stdint.h>

#include <QAbstractItemModel>
#include <QList>
#include <QMap>
#include <QSet>

class FileTreeItem;

class FileTreeModel: public QAbstractItemModel
{
    Q_OBJECT

  public:
    enum
    {
      COL_NAME,
      FIRST_VISIBLE_COLUMN = COL_NAME,
      COL_SIZE,
      COL_PROGRESS,
      COL_WANTED,
      COL_PRIORITY,
      LAST_VISIBLE_COLUMN = COL_PRIORITY,

      COL_FILE_INDEX,
      NUM_COLUMNS
    };

  public:
    FileTreeModel (QObject *parent = 0, bool isEditable = true);
    ~FileTreeModel ();

    void setEditable (bool editable);

  public:
    QVariant data (const QModelIndex &index, int role = Qt::DisplayRole) const;
    Qt::ItemFlags flags (const QModelIndex& index) const;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
    QModelIndex index (int row, int column, const QModelIndex& parent = QModelIndex()) const;
    QModelIndex parent (const QModelIndex& child) const;
    QModelIndex parent (const QModelIndex& child, int column) const;
    int rowCount (const QModelIndex& parent = QModelIndex()) const;
    int columnCount (const QModelIndex &parent = QModelIndex()) const;
    virtual bool setData (const QModelIndex & index, const QVariant & value, int role = Qt::EditRole);

  signals:
    void priorityChanged (const QSet<int>& fileIndices, int);
    void wantedChanged (const QSet<int>& fileIndices, bool);
    void pathEdited (const QString& oldpath, const QString& newname);
    void openRequested (const QString& path);

  public:
    void clear ();
    void addFile (int index, const QString& filename,
                  bool wanted, int priority,
                  uint64_t size, uint64_t have,
                  QList<QModelIndex>& rowsAdded,
                  bool torrentChanged);

  private:
    void clearSubtree (const QModelIndex &);
    QModelIndex indexOf (FileTreeItem *, int column) const;
    void parentsChanged (const QModelIndex &, int firstColumn, int lastColumn);
    void subtreeChanged (const QModelIndex &, int firstColumn, int lastColumn);
    FileTreeItem * findItemForFileIndex (int fileIndex) const;
    FileTreeItem * itemFromIndex (const QModelIndex&) const;

  private:
    FileTreeItem * myRootItem;
    QMap<int, FileTreeItem *> myIndexCache;
    bool myIsEditable;

  public slots:
    void clicked (const QModelIndex & index);
    void doubleClicked (const QModelIndex & index);
};

#endif // QTR_FILE_TREE_MODEL_H
