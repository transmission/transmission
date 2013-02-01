/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id$
 */

#include <algorithm>
#include <cassert>

#include <QApplication>
#include <QHeaderView>
#include <QPainter>
#include <QResizeEvent>
#include <QSortFilterProxyModel>
#include <QStringList>

#include <libtransmission/transmission.h> // priorities

#include "file-tree.h"
#include "formatter.h"
#include "hig.h"
#include "torrent.h" // FileList
#include "utils.h" // mime icons

enum
{
  COL_NAME,
  FIRST_VISIBLE_COLUMN = COL_NAME,
  COL_PROGRESS,
  COL_WANTED,
  COL_PRIORITY,
  LAST_VISIBLE_COLUMN = COL_PRIORITY,

  COL_FILE_INDEX,
  NUM_COLUMNS
};

/****
*****
****/

const QHash<QString,int>&
FileTreeItem :: getMyChildRows ()
{
  const size_t n = childCount();

  // ensure that all the rows are hashed
  while (myFirstUnhashedRow < n)
    {
      myChildRows.insert (myChildren[myFirstUnhashedRow]->name(),
                          myFirstUnhashedRow);
      ++myFirstUnhashedRow;
    }

  return myChildRows;
}


FileTreeItem :: ~FileTreeItem ()
{
  assert(myChildren.isEmpty());

  if (myParent != 0)
    {
      const int pos = row();
      assert ((pos>=0) && "couldn't find child in parent's lookup");
      myParent->myChildren.removeAt(pos);
      myParent->myChildRows.remove(name());
      myParent->myFirstUnhashedRow = pos;
    }
}

void
FileTreeItem :: appendChild (FileTreeItem * child)
{
  const size_t n = childCount();
  child->myParent = this;
  myChildren.append (child);
  myFirstUnhashedRow = n;
}

FileTreeItem *
FileTreeItem :: child (const QString& filename)
{
  FileTreeItem * item(0);

  const int row = getMyChildRows().value (filename, -1);
  if (row != -1)
    {
      item = child (row);
      assert (filename == item->name());
    }

  return item;
}

int
FileTreeItem :: row () const
{
  int i(-1);

  if(myParent)
    {
      i = myParent->getMyChildRows().value (name(), -1);
      assert (this == myParent->myChildren[i]);
    }

  return i;
}

QVariant
FileTreeItem :: data (int column, int role) const
{
  QVariant value;

  if (column == COL_FILE_INDEX)
    {
      return myFileIndex;
    }
  else if (role == Qt::EditRole)
    {
      if (column == 0)
        value.setValue (name());
    }
  else if (role == Qt::DisplayRole)
    {
      switch(column)
       {
         case COL_NAME:
           value.setValue (fileSizeName());
           break;

         case COL_PROGRESS:
           value.setValue (progress());
           break;

         case COL_WANTED:
           value.setValue (isSubtreeWanted());
           break;

         case COL_PRIORITY:
           value.setValue (priorityString());
           break;
        }
    }

  return value;
}

void
FileTreeItem :: getSubtreeSize (uint64_t& have, uint64_t& total) const
{
  have += myHaveSize;
  total += myTotalSize;

  foreach(const FileTreeItem * i, myChildren)
    i->getSubtreeSize(have, total);
}

double
FileTreeItem :: progress () const
{
  double d(0);
  uint64_t have(0), total(0);

  getSubtreeSize(have, total);
  if (total)
    d = have / (double)total;

  return d;
}

QString
FileTreeItem :: fileSizeName () const
{
  uint64_t have(0), total(0);
  QString str;
  getSubtreeSize(have, total);
  str = QString(name() + " (%1)").arg(Formatter::sizeToString(total));
  return str;
}

#include <iostream>

std::pair<int,int>
FileTreeItem :: update (const QString& name,
                        bool           wanted,
                        int            priority,
                        uint64_t       haveSize,
                        bool           updateFields)
{
  int changed_count = 0;
  int changed_fields[3];

  if (myName != name)
    {
      if (myParent)
        myParent->myFirstUnhashedRow = row();

      myName = name;
      changed_fields[changed_count++] = COL_NAME;
    }

  if (fileIndex() != -1)
    {
      if (myHaveSize != haveSize)
        myHaveSize = haveSize;

      if (updateFields)
        {
          if (myIsWanted != wanted)
            {
              myIsWanted = wanted;
              changed_fields[changed_count++] = COL_WANTED;
            }

          if (myPriority != priority)
            {
              myPriority = priority;
              changed_fields[changed_count++] = COL_PRIORITY;
            }
        }
    }

  std::pair<int,int> changed (-1, -1);
  if (changed_count > 0)
    {
      std::sort (changed_fields, changed_fields+changed_count);
      changed.first = changed_fields[0];
      changed.second = changed_fields [changed_count-1];
      std::cerr << "changed.first " << changed.first << " changed.second " << changed.second << std::endl;
    }
  return changed;
}

QString
FileTreeItem :: priorityString () const
{
  const int i = priority();

  switch (i)
    {
      case LOW:    return tr("Low");
      case HIGH:   return tr("High");
      case NORMAL: return tr("Normal");
      default:     return tr("Mixed");
    }
}

int
FileTreeItem :: priority () const
{
  int i(0);

  if (myChildren.isEmpty())
    {
      switch (myPriority)
        {
          case TR_PRI_LOW:
            i |= LOW;
            break;

          case TR_PRI_HIGH:
            i |= HIGH;
            break;

          default:
            i |= NORMAL;
            break;
        }
    }

  foreach (const FileTreeItem * child, myChildren)
    i |= child->priority();

  return i;
}

void
FileTreeItem :: setSubtreePriority (int i, QSet<int>& ids)
{
  if (myPriority != i)
    {
      myPriority = i;

      if (myFileIndex >= 0)
        ids.insert (myFileIndex);
    }

  foreach (FileTreeItem * child, myChildren)
    child->setSubtreePriority (i, ids);
}

void
FileTreeItem :: twiddlePriority (QSet<int>& ids, int& p)
{
  const int old(priority());

  if (old & LOW)
    p = TR_PRI_NORMAL;
  else if (old & NORMAL)
    p = TR_PRI_HIGH;
  else
    p = TR_PRI_LOW;

  setSubtreePriority (p, ids);
}

int
FileTreeItem :: isSubtreeWanted () const
{
  if(myChildren.isEmpty())
    return myIsWanted ? Qt::Checked : Qt::Unchecked;

  int wanted(-1);
  foreach (const FileTreeItem * child, myChildren)
    {
      const int childWanted = child->isSubtreeWanted();

      if (wanted == -1)
        wanted = childWanted;

      if (wanted != childWanted)
        wanted = Qt::PartiallyChecked;

      if (wanted == Qt::PartiallyChecked)
        return wanted;
    }

  return wanted;
}

void
FileTreeItem :: setSubtreeWanted (bool b, QSet<int>& ids)
{
  if (myIsWanted != b)
    {
      myIsWanted = b;

      if (myFileIndex >= 0)
        ids.insert(myFileIndex);
    }

  foreach (FileTreeItem * child, myChildren)
    child->setSubtreeWanted (b, ids);
}

void
FileTreeItem :: twiddleWanted (QSet<int>& ids, bool& wanted)
{
  wanted = isSubtreeWanted() != Qt::Checked;
  setSubtreeWanted (wanted, ids);
}

/***
****
****
***/

FileTreeModel :: FileTreeModel (QObject *parent, bool isEditable):
  QAbstractItemModel(parent),
  myRootItem (new FileTreeItem),
  myIsEditable (isEditable)
{
}

FileTreeModel :: ~FileTreeModel()
{
  clear();

  delete myRootItem;
}

FileTreeItem *
FileTreeModel :: itemFromIndex (const QModelIndex& index) const
{
  return static_cast<FileTreeItem*>(index.internalPointer()); 
}

QVariant
FileTreeModel :: data (const QModelIndex &index, int role) const
{
  QVariant value;

  if (index.isValid())
    value = itemFromIndex(index)->data (index.column(), role);

  return value;
}

Qt::ItemFlags
FileTreeModel :: flags (const QModelIndex& index) const
{
  int i(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

  if(myIsEditable && (index.column() == COL_NAME))
    i |= Qt::ItemIsEditable;

  if(index.column() == COL_WANTED)
    i |= Qt::ItemIsUserCheckable | Qt::ItemIsTristate;

  return (Qt::ItemFlags)i;
}

bool
FileTreeModel :: setData (const QModelIndex& index, const QVariant& newname, int role)
{
  if (role == Qt::EditRole)
    {
      QString oldpath;
      QModelIndex walk = index;
      FileTreeItem * item = itemFromIndex (index);

      while (item && !item->name().isEmpty())
        {
          if (oldpath.isEmpty())
            oldpath = item->name();
          else
            oldpath = item->name() + "/" + oldpath;
          item = item->parent ();
        }

      emit pathEdited (oldpath, newname.toString());
    }

  return false; // don't update the view until the session confirms the change
}

QVariant
FileTreeModel :: headerData (int column, Qt::Orientation orientation, int role) const
{
  QVariant data;

  if (orientation==Qt::Horizontal && role==Qt::DisplayRole)
    {
      switch (column)
        {
          case COL_NAME:
            data.setValue (tr("File"));
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
FileTreeModel :: index (int row, int column, const QModelIndex& parent) const
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
FileTreeModel :: parent (const QModelIndex& child) const
{
  return parent (child, 0); // QAbstractItemModel::parent() wants col 0
}

QModelIndex
FileTreeModel :: parent (const QModelIndex& child, int column) const
{
  QModelIndex parent;

  if (child.isValid())
    parent = indexOf (itemFromIndex(child)->parent(), column);

  return parent;
}

int
FileTreeModel :: rowCount (const QModelIndex& parent) const
{
  FileTreeItem * parentItem;

  if (parent.isValid())
    parentItem = itemFromIndex (parent);
  else
    parentItem = myRootItem;

  return parentItem->childCount();
}

int
FileTreeModel :: columnCount (const QModelIndex &parent) const
{
  Q_UNUSED(parent);

  return NUM_COLUMNS;
}

QModelIndex
FileTreeModel :: indexOf (FileTreeItem * item, int column) const
{
  if (!item || item==myRootItem)
    return QModelIndex();

  return createIndex(item->row(), column, item);
}

void
FileTreeModel :: clearSubtree (const QModelIndex& top)
{
  size_t i = rowCount (top);

  while (i > 0)
    clearSubtree(index(--i, 0, top));

  delete static_cast<FileTreeItem*>(itemFromIndex(top));
}

void
FileTreeModel :: clear ()
{
  clearSubtree (QModelIndex());

  reset ();
}

FileTreeItem *
FileTreeModel :: findItemForFileIndex (int fileIndex) const
{
  FileTreeItem * ret = 0;

  QModelIndexList indices = match (index (0,COL_FILE_INDEX),
                                   Qt::DisplayRole,
                                   fileIndex,
                                   1,
                                   Qt::MatchFlags (Qt::MatchExactly | Qt::MatchRecursive));

  if (!indices.isEmpty ())
    {
      QModelIndex& index = indices.front ();
      if (index.isValid())
        ret = itemFromIndex (index);
    }

  return ret;
}

void
FileTreeModel :: addFile (int                   fileIndex,
                          const QString       & filename,
                          bool                  wanted,
                          int                   priority,
                          uint64_t              totalSize,
                          uint64_t              have,
                          QList<QModelIndex>  & rowsAdded,
                          bool                  updateFields)
{
  bool added = false;
  FileTreeItem * item;
  QStringList tokens = filename.split (QChar::fromAscii('/'));

  item = findItemForFileIndex (fileIndex);

  if (item) // this file is already in the tree, we've added this 
    {
      while (!tokens.isEmpty())
        {
          const QString token = tokens.takeLast();
          const std::pair<int,int> changed = item->update (token, wanted, priority, have, updateFields);
          if (changed.first >= 0)
            dataChanged (indexOf (item, changed.first), indexOf (item, changed.second));
          item = item->parent();
        }
      assert (item == myRootItem);
    }
  else // we haven't build the FileTreeItems for these tokens yet
    {
      item = myRootItem;
      while (!tokens.isEmpty())
        {
          const QString token = tokens.takeFirst();
          FileTreeItem * child(item->child(token));
          if (!child)
            {
              added = true;
              QModelIndex parentIndex (indexOf(item, 0));
              const int n (item->childCount());

              beginInsertRows (parentIndex, n, n);
              if (tokens.isEmpty())
                child = new FileTreeItem (token, fileIndex, totalSize);
              else
                child = new FileTreeItem (token);
              item->appendChild (child);
              endInsertRows ();

              rowsAdded.append (indexOf(child, 0));
            }
          item = child;
        }

      if (item != myRootItem)
        {
          assert (item->fileIndex() == fileIndex);
          assert (item->totalSize() == totalSize);

          const std::pair<int,int> changed = item->update (item->name(), wanted, priority, have, added || updateFields);
          if (changed.first >= 0)
            dataChanged (indexOf (item, changed.first), indexOf (item, changed.second));
        }
    }
}

void
FileTreeModel :: parentsChanged (const QModelIndex& index, int column)
{
  QModelIndex walk = index;

  for (;;)
    {
      walk = parent(walk, column);
      if(!walk.isValid())
        break;

      dataChanged(walk, walk);
    }
}

void
FileTreeModel :: subtreeChanged (const QModelIndex& index, int column)
{
  const int childCount = rowCount (index);
  if (!childCount)
    return;

  // tell everyone that this tier changed
  dataChanged (index.child(0,column), index.child(childCount-1,column));

  // walk the subtiers
  for (int i=0; i<childCount; ++i)
    subtreeChanged (index.child(i,column), column);
}

void
FileTreeModel :: clicked (const QModelIndex& index)
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
      parentsChanged (index, column);
      subtreeChanged (index, column);
    }
  else if (column == COL_PRIORITY)
    {
      int priority;
      QSet<int> file_ids;
      FileTreeItem * item;

      item = itemFromIndex (index);
      item->twiddlePriority (file_ids, priority);
      emit priorityChanged (file_ids, priority);

      dataChanged(index, index);
      parentsChanged(index, column);
      subtreeChanged(index, column);
    }
}

/****
*****
****/

QSize
FileTreeDelegate :: sizeHint(const QStyleOptionViewItem& item, const QModelIndex& index) const
{
  QSize size;

  switch(index.column())
    {
      case COL_NAME:
        {
          const QFontMetrics fm(item.font);
          const QString text = index.data().toString();
          const int iconSize = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
          size.rwidth() = HIG::PAD_SMALL + iconSize;
          size.rheight() = std::max(iconSize, fm.height());
          break;
        }

      case COL_PROGRESS:
      case COL_WANTED:
        size = QSize(20, 1);
        break;

      default:
        {
          const QFontMetrics fm(item.font);
          const QString text = index.data().toString();
          size = fm.size(0, text);
          break;
        }
    }

  size.rheight() += 8; // make the spacing a little nicer
  return size;
}

void
FileTreeDelegate :: paint (QPainter                    * painter,
                           const QStyleOptionViewItem  & option,
                           const QModelIndex           & index) const
{
  const int column(index.column());

  if ((column != COL_PROGRESS) && (column != COL_WANTED) && (column != COL_NAME))
    {
      QItemDelegate::paint(painter, option, index);
      return;
    }

  QStyle * style (QApplication :: style());
  if (option.state & QStyle::State_Selected)
    painter->fillRect (option.rect, option.palette.highlight());
  painter->save();
  if (option.state & QStyle::State_Selected)
    painter->setBrush (option.palette.highlightedText());

  if (column == COL_NAME)
    {
      // draw the file icon
      static const int iconSize (style->pixelMetric(QStyle :: PM_SmallIconSize));
      const QRect iconArea (option.rect.x(),
                            option.rect.y() + (option.rect.height()-iconSize)/2,
                            iconSize, iconSize);
      QIcon icon;
      if (index.model()->hasChildren(index))
        {
          icon = style->standardIcon(QStyle::StandardPixmap(QStyle::SP_DirOpenIcon));
        }
      else
        {
          QString name = index.data().toString();
          icon = Utils :: guessMimeIcon (name.left(name.lastIndexOf(" (")));
        }
      icon.paint (painter, iconArea, Qt::AlignCenter, QIcon::Normal, QIcon::On);

      // draw the name
      QStyleOptionViewItem tmp (option);
      tmp.rect.setWidth (option.rect.width() - iconArea.width() - HIG::PAD_SMALL);
      tmp.rect.moveRight (option.rect.right());
      QItemDelegate::paint (painter, tmp, index);
    }
  else if(column == COL_PROGRESS)
    {
      QStyleOptionProgressBar p;
      p.state = option.state | QStyle::State_Small;
      p.direction = QApplication::layoutDirection();
      p.rect = option.rect;
      p.rect.setSize (QSize(option.rect.width()-2, option.rect.height()-8));
      p.rect.moveCenter (option.rect.center());
      p.fontMetrics = QApplication::fontMetrics();
      p.minimum = 0;
      p.maximum = 100;
      p.textAlignment = Qt::AlignCenter;
      p.textVisible = true;
      p.progress = (int)(100.0*index.data().toDouble());
      p.text = QString().sprintf("%d%%", p.progress);
      style->drawControl(QStyle::CE_ProgressBar, &p, painter);
    }
  else if(column == COL_WANTED)
    {
      QStyleOptionButton o;
      o.state = option.state;
      o.direction = QApplication::layoutDirection();
      o.rect.setSize (QSize(20, option.rect.height()));
      o.rect.moveCenter (option.rect.center());
      o.fontMetrics = QApplication::fontMetrics();
      switch (index.data().toInt())
        {
          case Qt::Unchecked: o.state |= QStyle::State_Off; break;
          case Qt::Checked:   o.state |= QStyle::State_On; break;
          default:            o.state |= QStyle::State_NoChange;break;
        }
      style->drawControl (QStyle::CE_CheckBox, &o, painter);
    }

  painter->restore();
}

/****
*****
*****
*****
****/

FileTreeView :: FileTreeView (QWidget * parent, bool isEditable):
  QTreeView (parent),
  myModel (this, isEditable),
  myProxy (new QSortFilterProxyModel()),
  myDelegate (this)
{
  setSortingEnabled (true);
  setAlternatingRowColors (true);
  setSelectionBehavior (QAbstractItemView::SelectRows);
  setSelectionMode (QAbstractItemView::ExtendedSelection);
  myProxy->setSourceModel (&myModel);
  setModel (myProxy);
  setItemDelegate (&myDelegate);
  setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
  sortByColumn (COL_NAME, Qt::AscendingOrder);
  installEventFilter (this);

  for (int i=0; i<FIRST_VISIBLE_COLUMN; ++i)
    hideColumn (i);
  for (int i=LAST_VISIBLE_COLUMN+1; i<NUM_COLUMNS; ++i)
    hideColumn (i);

  for (int i=FIRST_VISIBLE_COLUMN; i<=LAST_VISIBLE_COLUMN; ++i)
    header()->setResizeMode(i, QHeaderView::Interactive);

  connect (this, SIGNAL(clicked(const QModelIndex&)),
           this, SLOT(onClicked(const QModelIndex&)));

  connect (&myModel, SIGNAL(priorityChanged(const QSet<int>&, int)),
           this,     SIGNAL(priorityChanged(const QSet<int>&, int)));

  connect (&myModel, SIGNAL(wantedChanged(const QSet<int>&, bool)),
           this,     SIGNAL(wantedChanged(const QSet<int>&, bool)));

  connect (&myModel, SIGNAL(pathEdited(const QString&, const QString&)),
           this,     SIGNAL(pathEdited(const QString&, const QString&)));
}

FileTreeView :: ~FileTreeView ()
{
  myProxy->deleteLater();
}

void
FileTreeView :: onClicked (const QModelIndex& proxyIndex)
{
  const QModelIndex modelIndex = myProxy->mapToSource (proxyIndex);
  myModel.clicked (modelIndex);
}

bool
FileTreeView :: eventFilter (QObject * o, QEvent * event)
{
  // this is kind of a hack to get the last three columns be the
  // right size, and to have the filename column use whatever
  // space is left over...
  if ((o == this) && (event->type() == QEvent::Resize))
    {
      QResizeEvent * r = dynamic_cast<QResizeEvent*>(event);
      int left = r->size().width();
      const QFontMetrics fontMetrics(font());
      for (int column=FIRST_VISIBLE_COLUMN; column<=LAST_VISIBLE_COLUMN; ++column)
        {
          if (column == COL_NAME)
            continue;
          if (isColumnHidden (column))
            continue;

          const QString header = myModel.headerData (column, Qt::Horizontal).toString() + "    ";
          const int width = fontMetrics.size (0, header).width();
          setColumnWidth (column, width);
            left -= width;
        }
      left -= 20; // not sure why this is necessary.  it works in different themes + font sizes though...
      setColumnWidth(COL_NAME, std::max(left,0));
    }

  return false;
}

void
FileTreeView :: update (const FileList& files, bool updateFields)
{
  foreach (const TrFile file, files)
    {
      QList<QModelIndex> added;
      myModel.addFile (file.index, file.filename, file.wanted, file.priority, file.size, file.have, added, updateFields);
      foreach (QModelIndex i, added)
        expand (myProxy->mapFromSource(i));
    }
}

void
FileTreeView :: clear ()
{
  myModel.clear();
}
