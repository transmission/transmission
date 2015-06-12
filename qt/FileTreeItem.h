/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FILE_TREE_ITEM_H
#define QTR_FILE_TREE_ITEM_H

#include <stdint.h>

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariant>

class FileTreeItem
{
    Q_DECLARE_TR_FUNCTIONS (FileTreeItem)

  public:
    FileTreeItem (const QString& name = QString (), int fileIndex = -1, uint64_t size = 0):
      myName (name),
      myFileIndex (fileIndex),
      myTotalSize (size),
      myParent (nullptr),
      myPriority (0),
      myIsWanted (false),
      myHaveSize (0),
      myFirstUnhashedRow (0) {}
    ~FileTreeItem();

  public:
    void appendChild (FileTreeItem * child);
    FileTreeItem * child (const QString& filename);
    FileTreeItem * child (int row) { return myChildren.at (row); }
    int childCount () const { return myChildren.size (); }
    FileTreeItem * parent () { return myParent; }
    const FileTreeItem * parent () const { return myParent; }
    int row () const;
    const QString& name () const { return myName; }
    QVariant data (int column, int role) const;
    std::pair<int, int> update (const QString& name, bool want, int priority, uint64_t have, bool updateFields);
    void twiddleWanted (QSet<int>& fileIds, bool&);
    void twiddlePriority (QSet<int>& fileIds, int&);
    int fileIndex () const { return myFileIndex; }
    uint64_t totalSize () const { return myTotalSize; }
    QString path () const;
    bool isComplete () const;

  private:
    enum
    {
      LOW    = (1 << 0),
      NORMAL = (1 << 1),
      HIGH   = (1 << 2)
    };

  private:
    void setSubtreePriority (int priority, QSet<int>& fileIds);
    void setSubtreeWanted (bool, QSet<int>& fileIds);
    QString priorityString () const;
    QString sizeString () const;
    void getSubtreeWantedSize (uint64_t& have, uint64_t& total) const;
    double progress () const;
    int priority () const;
    int isSubtreeWanted () const;
    const QHash<QString,int>& getMyChildRows();

  private:
    QString myName;
    const int myFileIndex;
    const uint64_t myTotalSize;

    FileTreeItem * myParent;
    QList<FileTreeItem*> myChildren;
    QHash<QString,int> myChildRows;
    int myPriority;
    bool myIsWanted;
    uint64_t myHaveSize;
    size_t myFirstUnhashedRow;
};

#endif // QTR_FILE_TREE_ITEM_H
