/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FILE_TREE
#define QTR_FILE_TREE

#include <QAbstractItemModel>
#include <QObject>
#include <QItemDelegate>
#include <QList>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QTreeView>
#include <QVariant>

class QSortFilterProxyModel;
class QStyle;

#include "torrent.h" // FileList

/****
*****
****/

class FileTreeItem: public QObject
{
    Q_OBJECT;

    enum { LOW=(1<<0), NORMAL=(1<<1), HIGH=(1<<2) };

  public:

    virtual ~FileTreeItem();

    FileTreeItem (const QString& name="", int fileIndex=-1, uint64_t size=0):
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

class FileTreeModel: public QAbstractItemModel
{
    Q_OBJECT

  public:
    FileTreeModel (QObject *parent = 0, bool isEditable = true);
    ~FileTreeModel ();

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
    const bool myIsEditable;

  public slots:
    void clicked (const QModelIndex & index);
    void doubleClicked (const QModelIndex & index);
};

class FileTreeDelegate: public QItemDelegate
{
    Q_OBJECT

  public:
    FileTreeDelegate (QObject * parent=0): QItemDelegate(parent) {}
    virtual ~FileTreeDelegate() {}

  public:
    virtual QSize sizeHint (const QStyleOptionViewItem&, const QModelIndex&) const;
    virtual void paint (QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const;
};

class FileTreeView: public QTreeView
{
    Q_OBJECT

  public:
    FileTreeView (QWidget * parent=0, bool editable=true);
    virtual ~FileTreeView ();
    void clear ();
    void update (const FileList& files, bool updateProperties=true);

  signals:
    void priorityChanged (const QSet<int>& fileIndices, int priority);
    void wantedChanged (const QSet<int>& fileIndices, bool wanted);
    void pathEdited (const QString& oldpath, const QString& newname);
    void openRequested (const QString& path);

  protected:
    bool eventFilter (QObject *, QEvent *);

  private:
    FileTreeModel myModel;
    QSortFilterProxyModel * myProxy;
    FileTreeDelegate myDelegate;

  public slots:
    void onClicked (const QModelIndex& index);
    void onDoubleClicked (const QModelIndex& index);
    void onOpenRequested (const QString& path);
};

#endif
