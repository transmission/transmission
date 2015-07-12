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

#include <cstdint>

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

    enum Role
    {
      SortRole = Qt::UserRole
    };

  public:
    FileTreeModel (QObject * parent = nullptr, bool isEditable = true);
    virtual ~FileTreeModel ();

    void setEditable (bool editable);

    void clear ();
    void addFile (int index, const QString& filename,
                  bool wanted, int priority,
                  uint64_t size, uint64_t have,
                  bool torrentChanged);

    QModelIndex parent (const QModelIndex& child, int column) const;

    // QAbstractItemModel
    virtual QVariant data (const QModelIndex& index, int role = Qt::DisplayRole) const;
    virtual Qt::ItemFlags flags (const QModelIndex& index) const;
    virtual QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
    virtual QModelIndex index (int row, int column, const QModelIndex& parent = QModelIndex ()) const;
    virtual QModelIndex parent (const QModelIndex& child) const;
    virtual int rowCount (const QModelIndex& parent = QModelIndex ()) const;
    virtual int columnCount (const QModelIndex& parent = QModelIndex ()) const;
    virtual bool setData (const QModelIndex& index, const QVariant& value, int role = Qt::EditRole);

  public slots:
    void clicked (const QModelIndex& index);
    void doubleClicked (const QModelIndex& index);

  signals:
    void priorityChanged (const QSet<int>& fileIndices, int);
    void wantedChanged (const QSet<int>& fileIndices, bool);
    void pathEdited (const QString& oldpath, const QString& newname);
    void openRequested (const QString& path);

  private:
    void clearSubtree (const QModelIndex&);
    QModelIndex indexOf (FileTreeItem *, int column) const;
    void parentsChanged (const QModelIndex&, int firstColumn, int lastColumn);
    void subtreeChanged (const QModelIndex&, int firstColumn, int lastColumn);
    FileTreeItem * findItemForFileIndex (int fileIndex) const;
    FileTreeItem * itemFromIndex (const QModelIndex&) const;

  private:
    bool myIsEditable;

    FileTreeItem * myRootItem;
    QMap<int, FileTreeItem *> myIndexCache;
};

#endif // QTR_FILE_TREE_MODEL_H
