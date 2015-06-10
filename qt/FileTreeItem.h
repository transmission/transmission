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

#include <QObject>
#include <QList>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariant>

class FileTreeItem: public QObject
{
    Q_OBJECT

    enum { LOW=(1<<0), NORMAL=(1<<1), HIGH=(1<<2) };

  public:

    virtual ~FileTreeItem();

    FileTreeItem (const QString& name=QString (), int fileIndex=-1, uint64_t size=0):
      myFileIndex (fileIndex),
      myParent (0),
      myName (name),
      myPriority (0),
      myIsWanted (0),
      myHaveSize (0),
      myTotalSize (size),
      myFirstUnhashedRow (0) {}

  public:
    void appendChild (FileTreeItem *child);
    FileTreeItem * child (const QString& filename);
    FileTreeItem * child (int row) { return myChildren.at(row); }
    int childCount () const { return myChildren.size(); }
    FileTreeItem * parent () { return myParent; }
    const FileTreeItem * parent () const { return myParent; }
    int row () const;
    const QString& name () const { return myName; }
    QVariant data (int column, int role) const;
    std::pair<int,int> update (const QString& name, bool want, int priority, uint64_t have, bool updateFields);
    void twiddleWanted (QSet<int>& fileIds, bool&);
    void twiddlePriority (QSet<int>& fileIds, int&);
    int fileIndex () const { return myFileIndex; }
    uint64_t totalSize () const { return myTotalSize; }
    QString path () const;
    bool isComplete () const;

  private:
    void setSubtreePriority (int priority, QSet<int>& fileIds);
    void setSubtreeWanted (bool, QSet<int>& fileIds);
    QString priorityString () const;
    QString sizeString () const;
    void getSubtreeWantedSize (uint64_t& have, uint64_t& total) const;
    double progress () const;
    int priority () const;
    int isSubtreeWanted () const;

    const int myFileIndex;
    FileTreeItem * myParent;
    QList<FileTreeItem*> myChildren;
    QHash<QString,int> myChildRows;
    const QHash<QString,int>& getMyChildRows();
    QString myName;
    int myPriority;
    bool myIsWanted;
    uint64_t myHaveSize;
    const uint64_t myTotalSize;
    size_t myFirstUnhashedRow;
};

#endif // QTR_FILE_TREE_ITEM_H
