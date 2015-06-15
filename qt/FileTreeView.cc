/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <algorithm>

#include <QHeaderView>
#include <QResizeEvent>
#include <QSortFilterProxyModel>

#include "FileTreeDelegate.h"
#include "FileTreeItem.h"
#include "FileTreeModel.h"
#include "FileTreeView.h"
#include "Formatter.h"
#include "Utils.h"

FileTreeView::FileTreeView (QWidget * parent, bool isEditable):
  QTreeView (parent),
  myModel (new FileTreeModel (this, isEditable)),
  myProxy (new QSortFilterProxyModel (this)),
  myDelegate (new FileTreeDelegate (this))
{
  myProxy->setSourceModel (myModel);
  myProxy->setSortRole (FileTreeModel::SortRole);
  myProxy->setSortCaseSensitivity (Qt::CaseInsensitive);

  setModel (myProxy);
  setItemDelegate (myDelegate);
  sortByColumn (FileTreeModel::COL_NAME, Qt::AscendingOrder);

  for (int i=0; i<FileTreeModel::NUM_COLUMNS; ++i)
    setColumnHidden (i, (i<FileTreeModel::FIRST_VISIBLE_COLUMN) || (FileTreeModel::LAST_VISIBLE_COLUMN<i));

  connect (this, SIGNAL(clicked(QModelIndex)),
           this, SLOT(onClicked(QModelIndex)));

  connect (this, SIGNAL(doubleClicked(QModelIndex)),
           this, SLOT(onDoubleClicked(QModelIndex)));

  connect (myModel, SIGNAL(priorityChanged(QSet<int>, int)),
           this,    SIGNAL(priorityChanged(QSet<int>, int)));

  connect (myModel, SIGNAL(wantedChanged(QSet<int>, bool)),
           this,    SIGNAL(wantedChanged(QSet<int>, bool)));

  connect (myModel, SIGNAL(pathEdited(QString, QString)),
           this,    SIGNAL(pathEdited(QString, QString)));

  connect (myModel, SIGNAL (openRequested (QString)),
           this,    SLOT (onOpenRequested (QString)),
           Qt::QueuedConnection);
}

void
FileTreeView::onClicked (const QModelIndex& proxyIndex)
{
  const QModelIndex modelIndex = myProxy->mapToSource (proxyIndex);
  myModel->clicked (modelIndex);
}

void
FileTreeView::onDoubleClicked (const QModelIndex& proxyIndex)
{
  const QModelIndex modelIndex = myProxy->mapToSource (proxyIndex);
  myModel->doubleClicked (modelIndex);
}

void
FileTreeView::onOpenRequested (const QString& path)
{
  if (state () == EditingState)
    return;

  emit openRequested (path);
}

void
FileTreeView::resizeEvent (QResizeEvent * event)
{
  QTreeView::resizeEvent (event);

  // this is kind of a hack to get the last four columns be the
  // right size, and to have the filename column use whatever
  // space is left over...

  int left = event->size ().width () - 1;
  for (int column = FileTreeModel::FIRST_VISIBLE_COLUMN; column <= FileTreeModel::LAST_VISIBLE_COLUMN; ++column)
    {
      if (column == FileTreeModel::COL_NAME)
        continue;
      if (isColumnHidden (column))
        continue;

      int minWidth = 0;

      QStringList itemTexts;
      switch (column)
        {
          case FileTreeModel::COL_SIZE:
            for (int s = Formatter::B; s <= Formatter::TB; ++s)
              itemTexts << QLatin1String ("999.9 ") +
                           Formatter::unitStr (Formatter::MEM, static_cast<Formatter::Size> (s));
            break;

          case FileTreeModel::COL_PROGRESS:
            itemTexts << QLatin1String ("  100%  ");
            break;

          case FileTreeModel::COL_WANTED:
            minWidth = 20;
            break;

          case FileTreeModel::COL_PRIORITY:
            itemTexts << FileTreeItem::tr ("Low") << FileTreeItem::tr ("Normal") <<
                         FileTreeItem::tr ("High") << FileTreeItem::tr ("Mixed");
            break;
        }

      int itemWidth = 0;
      for (const QString& itemText: itemTexts)
        itemWidth = std::max (itemWidth, Utils::measureViewItem (this, itemText));

      const QString headerText = myModel->headerData (column, Qt::Horizontal).toString ();
      int headerWidth = Utils::measureHeaderItem (this->header (), headerText);

      const int width = std::max (minWidth, std::max (itemWidth, headerWidth));
      setColumnWidth (column, width);

      left -= width;
    }

  setColumnWidth (FileTreeModel::COL_NAME, std::max (left, 0));
}

void
FileTreeView::keyPressEvent (QKeyEvent * event)
{
  QTreeView::keyPressEvent (event);

  // handle using the keyboard to toggle the
  // wanted/unwanted state or the file priority

  if (state () == EditingState)
    return;

  if (event->key () == Qt::Key_Space)
    {
      int column;

      const Qt::KeyboardModifiers modifiers = event->modifiers ();
      if (modifiers == Qt::NoModifier)
        column = FileTreeModel::COL_WANTED;
      else if (modifiers == Qt::ShiftModifier)
        column = FileTreeModel::COL_PRIORITY;
      else
        return;

      for (const QModelIndex& i: selectionModel ()->selectedRows (column))
        clicked (i);
    }
}

void
FileTreeView::update (const FileList& files, bool updateFields)
{
  for (const TorrentFile& file: files)
    {
      QList<QModelIndex> added;
      myModel->addFile (file.index, file.filename, file.wanted, file.priority, file.size, file.have, added, updateFields);
      for (const QModelIndex& i: added)
        expand (myProxy->mapFromSource(i));
    }
}

void
FileTreeView::clear ()
{
  myModel->clear();
}

void
FileTreeView::setEditable (bool editable)
{
  myModel->setEditable (editable);
}
