/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <cassert>

#include "FileTreeItem.h"
#include "FileTreeModel.h"

namespace
{
  class PathIteratorBase
  {
    protected:
      PathIteratorBase(const QString& path, int slashIndex):
        myPath (path),
        mySlashIndex (slashIndex),
        myToken ()
      {
        myToken.reserve (path.size () / 2);
      }

    protected:
      const QString& myPath;
      int mySlashIndex;
      QString myToken;

      static const QChar SlashChar;
  };

  const QChar PathIteratorBase::SlashChar = QLatin1Char ('/');

  class ForwardPathIterator: public PathIteratorBase
  {
    public:
      ForwardPathIterator (const QString& path):
        PathIteratorBase (path, path.size () - 1)
      {
      }

      bool hasNext () const
      {
        return mySlashIndex > 0;
      }

      const QString& next ()
      {
        int newSlashIndex = myPath.lastIndexOf (SlashChar, mySlashIndex);
        myToken.truncate (0);
        myToken += myPath.midRef (newSlashIndex + 1, mySlashIndex - newSlashIndex);
        mySlashIndex = newSlashIndex - 1;
        return myToken;
      }
  };

  class BackwardPathIterator: public PathIteratorBase
  {
    public:
      BackwardPathIterator (const QString& path):
        PathIteratorBase (path, 0)
      {
      }

      bool hasNext () const
      {
        return mySlashIndex < myPath.size ();
      }

      const QString& next ()
      {
        int newSlashIndex = myPath.indexOf (SlashChar, mySlashIndex);
        if (newSlashIndex == -1)
          newSlashIndex = myPath.size ();
        myToken.truncate (0);
        myToken += myPath.midRef (mySlashIndex, newSlashIndex - mySlashIndex);
        mySlashIndex = newSlashIndex + 1;
        return myToken;
      }
  };
}

FileTreeModel::FileTreeModel (QObject * parent, bool isEditable):
  QAbstractItemModel(parent),
  myIsEditable (isEditable),
  myRootItem (new FileTreeItem),
  myIndexCache ()
{
}

FileTreeModel::~FileTreeModel()
{
  clear();

  delete myRootItem;
}

void
FileTreeModel::setEditable (bool editable)
{
  myIsEditable = editable;
}

FileTreeItem *
FileTreeModel::itemFromIndex (const QModelIndex& index) const
{
  return static_cast<FileTreeItem*>(index.internalPointer());
}

QVariant
FileTreeModel::data (const QModelIndex &index, int role) const
{
  QVariant value;

  if (index.isValid())
    value = itemFromIndex(index)->data (index.column(), role);

  return value;
}

Qt::ItemFlags
FileTreeModel::flags (const QModelIndex& index) const
{
  int i(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

  if(myIsEditable && (index.column() == COL_NAME))
    i |= Qt::ItemIsEditable;

  if(index.column() == COL_WANTED)
    i |= Qt::ItemIsUserCheckable | Qt::ItemIsTristate;

  return (Qt::ItemFlags)i;
}

bool
FileTreeModel::setData (const QModelIndex& index, const QVariant& newname, int role)
{
  if (role == Qt::EditRole)
    {
      FileTreeItem * item = itemFromIndex (index);

      emit pathEdited (item->path (), newname.toString ());
    }

  return false; // don't update the view until the session confirms the change
}

QVariant
FileTreeModel::headerData (int column, Qt::Orientation orientation, int role) const
{
  QVariant data;

  if (orientation==Qt::Horizontal && role==Qt::DisplayRole)
    {
      switch (column)
        {
          case COL_NAME:
            data.setValue (tr("File"));
            break;

          case COL_SIZE:
            data.setValue (tr("Size"));
            break;

          case COL_PROGRESS:
            data.setValue (tr("Progress"));
            break;

          case COL_WANTED:
            data.setValue (tr("Download"));
            break;

          case COL_PRIORITY:
            data.setValue (tr("Priority"));
            break;

          default:
            break;
        }
    }

  return data;
}

QModelIndex
FileTreeModel::index (int row, int column, const QModelIndex& parent) const
{
  QModelIndex i;

  if (hasIndex (row, column, parent))
    {
      FileTreeItem * parentItem;

      if (!parent.isValid ())
        parentItem = myRootItem;
      else
        parentItem = itemFromIndex (parent);

      FileTreeItem * childItem = parentItem->child (row);

      if (childItem)
        i = createIndex (row, column, childItem);
    }

  return i;
}

QModelIndex
FileTreeModel::parent (const QModelIndex& child) const
{
  return parent (child, 0); // QAbstractItemModel::parent() wants col 0
}

QModelIndex
FileTreeModel::parent (const QModelIndex& child, int column) const
{
  QModelIndex parent;

  if (child.isValid())
    parent = indexOf (itemFromIndex(child)->parent(), column);

  return parent;
}

int
FileTreeModel::rowCount (const QModelIndex& parent) const
{
  FileTreeItem * parentItem;

  if (parent.isValid())
    parentItem = itemFromIndex (parent);
  else
    parentItem = myRootItem;

  return parentItem->childCount();
}

int
FileTreeModel::columnCount (const QModelIndex& parent) const
{
  Q_UNUSED(parent);

  return NUM_COLUMNS;
}

QModelIndex
FileTreeModel::indexOf (FileTreeItem * item, int column) const
{
  if (!item || item==myRootItem)
    return QModelIndex();

  return createIndex(item->row(), column, item);
}

void
FileTreeModel::clearSubtree (const QModelIndex& top)
{
  size_t i = rowCount (top);

  while (i > 0)
    clearSubtree(index(--i, 0, top));

  FileTreeItem * const item = itemFromIndex (top);
  if (item == 0)
    return;

  if (item->fileIndex () != -1)
    myIndexCache.remove (item->fileIndex ());

  delete item;
}

void
FileTreeModel::clear ()
{
  beginResetModel ();
  clearSubtree (QModelIndex());
  endResetModel ();

  assert (myIndexCache.isEmpty ());
}

FileTreeItem *
FileTreeModel::findItemForFileIndex (int fileIndex) const
{
  return myIndexCache.value (fileIndex, 0);
}

void
FileTreeModel::addFile (int            fileIndex,
                        const QString& filename,
                        bool           wanted,
                        int            priority,
                        uint64_t       totalSize,
                        uint64_t       have,
                        bool           updateFields)
{
  FileTreeItem * item;

  item = findItemForFileIndex (fileIndex);

  if (item) // this file is already in the tree, we've added this
    {
      QModelIndex indexWithChangedParents;
      ForwardPathIterator filenameIt (filename);
      while (filenameIt.hasNext ())
        {
          const QString& token = filenameIt.next ();
          const std::pair<int,int> changed = item->update (token, wanted, priority, have, updateFields);
          if (changed.first >= 0)
            {
              dataChanged (indexOf (item, changed.first), indexOf (item, changed.second));
              if (!indexWithChangedParents.isValid () &&
                  changed.first <= COL_PRIORITY && changed.second >= COL_SIZE)
                indexWithChangedParents = indexOf (item, 0);
            }
          item = item->parent();
        }
      assert (item == myRootItem);
      if (indexWithChangedParents.isValid ())
        parentsChanged (indexWithChangedParents, COL_SIZE, COL_PRIORITY);
    }
  else // we haven't build the FileTreeItems for these tokens yet
    {
      bool added = false;

      item = myRootItem;
      BackwardPathIterator filenameIt (filename);
      while (filenameIt.hasNext ())
        {
          const QString& token = filenameIt.next ();
          FileTreeItem * child(item->child(token));
          if (!child)
            {
              added = true;
              QModelIndex parentIndex (indexOf(item, 0));
              const int n (item->childCount());

              beginInsertRows (parentIndex, n, n);
              if (!filenameIt.hasNext ())
                child = new FileTreeItem (token, fileIndex, totalSize);
              else
                child = new FileTreeItem (token);
              item->appendChild (child);
              endInsertRows ();
            }
          item = child;
        }

      if (item != myRootItem)
        {
          assert (item->fileIndex() == fileIndex);
          assert (item->totalSize() == totalSize);

          myIndexCache[fileIndex] = item;

          const std::pair<int,int> changed = item->update (item->name(), wanted, priority, have, added || updateFields);
          if (changed.first >= 0)
            dataChanged (indexOf (item, changed.first), indexOf (item, changed.second));
        }
    }
}

void
FileTreeModel::parentsChanged (const QModelIndex& index, int firstColumn, int lastColumn)
{
  assert (firstColumn <= lastColumn);

  QModelIndex walk = index;

  for (;;)
    {
      walk = parent (walk, firstColumn);
      if (!walk.isValid ())
        break;

      dataChanged (walk, walk.sibling (walk.row (), lastColumn));
    }
}

void
FileTreeModel::subtreeChanged (const QModelIndex& index, int firstColumn, int lastColumn)
{
  assert (firstColumn <= lastColumn);

  const int childCount = rowCount (index);
  if (!childCount)
    return;

  // tell everyone that this tier changed
  dataChanged (index.child (0, firstColumn), index.child (childCount - 1, lastColumn));

  // walk the subtiers
  for (int i=0; i<childCount; ++i)
    subtreeChanged (index.child (i, 0), firstColumn, lastColumn);
}

void
FileTreeModel::clicked (const QModelIndex& index)
{
  const int column (index.column());

  if (!index.isValid())
    return;

  if (column == COL_WANTED)
    {
      bool want;
      QSet<int> file_ids;
      FileTreeItem * item;

      item = itemFromIndex (index);
      item->twiddleWanted (file_ids, want);
      emit wantedChanged (file_ids, want);

      dataChanged (index, index);
      parentsChanged (index, COL_SIZE, COL_WANTED);
      subtreeChanged (index, COL_WANTED, COL_WANTED);
    }
  else if (column == COL_PRIORITY)
    {
      int priority;
      QSet<int> file_ids;
      FileTreeItem * item;

      item = itemFromIndex (index);
      item->twiddlePriority (file_ids, priority);
      emit priorityChanged (file_ids, priority);

      dataChanged (index, index);
      parentsChanged (index, column, column);
      subtreeChanged (index, column, column);
    }
}

void
FileTreeModel::doubleClicked (const QModelIndex& index)
{
  if (!index.isValid())
    return;

  const int column (index.column());
  if (column == COL_WANTED || column == COL_PRIORITY)
    return;

  FileTreeItem * item = itemFromIndex (index);

  if (item->childCount () == 0 && item->isComplete ())
    emit openRequested (item->path ());
}
