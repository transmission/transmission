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
#include "FileTreeModel.h"
#include "FileTreeView.h"

FileTreeView::FileTreeView (QWidget * parent, bool isEditable):
  QTreeView (parent),
  myModel (new FileTreeModel (this, isEditable)),
  myProxy (new QSortFilterProxyModel (this)),
  myDelegate (new FileTreeDelegate (this))
{
  setSortingEnabled (true);
  setAlternatingRowColors (true);
  setSelectionBehavior (QAbstractItemView::SelectRows);
  setSelectionMode (QAbstractItemView::ExtendedSelection);
  myProxy->setSourceModel (myModel);
  setModel (myProxy);
  setItemDelegate (myDelegate);
  setHorizontalScrollBarPolicy (Qt::ScrollBarAlwaysOff);
  sortByColumn (FileTreeModel::COL_NAME, Qt::AscendingOrder);
  installEventFilter (this);

  for (int i=0; i<FileTreeModel::NUM_COLUMNS; ++i)
    {
      setColumnHidden (i, (i<FileTreeModel::FIRST_VISIBLE_COLUMN) || (FileTreeModel::LAST_VISIBLE_COLUMN<i));

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
      header()->setResizeMode(i, QHeaderView::Interactive);
#else
      header()->setSectionResizeMode(i, QHeaderView::Interactive);
#endif
    }

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

bool
FileTreeView::eventFilter (QObject * o, QEvent * event)
{
  // this is kind of a hack to get the last three columns be the
  // right size, and to have the filename column use whatever
  // space is left over...
  if ((o == this) && (event->type() == QEvent::Resize))
    {
      QResizeEvent * r = static_cast<QResizeEvent*> (event);
      int left = r->size().width();
      const QFontMetrics fontMetrics(font());
      for (int column=FileTreeModel::FIRST_VISIBLE_COLUMN; column<=FileTreeModel::LAST_VISIBLE_COLUMN; ++column)
        {
          if (column == FileTreeModel::COL_NAME)
            continue;
          if (isColumnHidden (column))
            continue;

          QString header;
          if (column == FileTreeModel::COL_SIZE)
            header = QLatin1String ("999.9 KiB");
          else
            header = myModel->headerData (column, Qt::Horizontal).toString();
          header += QLatin1String ("    ");
          const int width = fontMetrics.size (0, header).width();
          setColumnWidth (column, width);
            left -= width;
        }
      left -= 20; // not sure why this is necessary.  it works in different themes + font sizes though...
      setColumnWidth(FileTreeModel::COL_NAME, std::max(left,0));
    }

  // handle using the keyboard to toggle the
  // wanted/unwanted state or the file priority
  else if (event->type () == QEvent::KeyPress && state () != EditingState)
    {
      switch (static_cast<QKeyEvent*> (event)->key ())
        {
        case Qt::Key_Space:
          for (const QModelIndex& i: selectionModel ()->selectedRows (FileTreeModel::COL_WANTED))
            clicked (i);
          break;

        case Qt::Key_Enter:
        case Qt::Key_Return:
          for (const QModelIndex& i: selectionModel ()->selectedRows (FileTreeModel::COL_PRIORITY))
            clicked (i);
          break;
        }
    }

  return false;
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
