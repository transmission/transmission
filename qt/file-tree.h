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

#ifndef QTR_TREE_FILE_MODEL
#define QTR_TREE_FILE_MODEL

#include <QAbstractItemModel>
#include <QObject>
#include <QItemDelegate>
#include <QList>
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
        virtual ~FileTreeItem( );
        FileTreeItem( int fileIndex, const QString& name="" ):
            myIndex(fileIndex), myParent(0), myName(name),
            myPriority(0), myIsWanted(0),
            myHaveSize(0), myTotalSize(0) { }

    public:
        void appendChild( FileTreeItem *child );
        FileTreeItem * child( const QString& filename );
        FileTreeItem * child( int row ) { return myChildren.at( row ); }
        int childCount( ) const { return myChildren.size( ); }
        FileTreeItem * parent( ) { return myParent; }
        const FileTreeItem * parent( ) const { return myParent; }
        int row( ) const;
        const QString& name( ) const { return myName; }
        QVariant data( int column ) const;
        bool update( int index, bool want, int priority, uint64_t total, uint64_t have, bool torrentChanged );
        void twiddleWanted( QSet<int>& fileIds, bool& );
        void twiddlePriority( QSet<int>& fileIds, int& );

    private:
        void setSubtreePriority( int priority, QSet<int>& fileIds );
        void setSubtreeWanted( bool, QSet<int>& fileIds );
        QString priorityString( ) const;
        void getSubtreeSize( uint64_t& have, uint64_t& total ) const;
        QString fileSizeName( ) const;
        double progress( ) const;
        int priority( ) const;
        int isSubtreeWanted( ) const;

        int myIndex;
        FileTreeItem * myParent;
        QList<FileTreeItem*> myChildren;
        const QString myName;
        int myPriority;
        bool myIsWanted;
        uint64_t myHaveSize;
        uint64_t myTotalSize;
};

class FileTreeModel: public QAbstractItemModel
{
        Q_OBJECT

    public:
        FileTreeModel( QObject *parent = 0);
        ~FileTreeModel( );

    public:
        QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const;
        Qt::ItemFlags flags( const QModelIndex& index ) const;
        QVariant headerData( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const;
        QModelIndex index( int row, int column, const QModelIndex& parent = QModelIndex() ) const;
        QModelIndex parent( const QModelIndex& child ) const;
        QModelIndex parent( const QModelIndex& child, int column ) const;
        int rowCount( const QModelIndex& parent = QModelIndex( ) ) const;
        int columnCount( const QModelIndex &parent = QModelIndex( ) ) const;

    signals:
        void priorityChanged( const QSet<int>& fileIndices, int );
        void wantedChanged( const QSet<int>& fileIndices, bool );

    public:
        void clear( );
        void addFile( int index, const QString& filename,
                      bool wanted, int priority,
                      uint64_t size, uint64_t have,
                      QList<QModelIndex>& rowsAdded,
                      bool torrentChanged );

    private:
        void clearSubtree( const QModelIndex & );
        QModelIndex indexOf( FileTreeItem *, int column ) const;
        void parentsChanged( const QModelIndex &, int column );
        void subtreeChanged( const QModelIndex &, int column );

    private:
        FileTreeItem * rootItem;

    public slots:
        void clicked ( const QModelIndex & index );
};

class FileTreeDelegate: public QItemDelegate
{
        Q_OBJECT

    public:
        FileTreeDelegate( QObject * parent=0 ): QItemDelegate( parent ) { }
        virtual ~FileTreeDelegate( ) { }

    public:
        virtual QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const;
        virtual void paint(QPainter*, const QStyleOptionViewItem&, const QModelIndex&) const;
};

class FileTreeView: public QTreeView
{
        Q_OBJECT

    public:
        FileTreeView( QWidget * parent=0 );
        virtual ~FileTreeView( );
        void clear( );
        void update( const FileList& files );
        void update( const FileList& files, bool torrentChanged );

    signals:
        void priorityChanged( const QSet<int>& fileIndices, int );
        void wantedChanged( const QSet<int>& fileIndices, bool );

    protected:
        bool eventFilter( QObject *, QEvent * );

    private:
        FileTreeModel myModel;
        QSortFilterProxyModel * myProxy;
        FileTreeDelegate myDelegate;

    public slots:
        void onClicked ( const QModelIndex & index );
};

#endif
