/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_FILTERBAR_H
#define QTR_FILTERBAR_H

#include <QComboBox>
#include <QItemDelegate>
#include <QWidget>

class QLineEdit;
class QPaintEvent;
class QStandardItemModel;
class QTimer;

class Prefs;
class TorrentFilter;
class TorrentModel;

class FilterBarComboBoxDelegate: public QItemDelegate
{
        Q_OBJECT

    public:
        FilterBarComboBoxDelegate( QObject * parent, QComboBox * combo );

    public:
        static bool isSeparator( const QModelIndex &index );
        static void setSeparator( QAbstractItemModel * model, const QModelIndex& index );

    protected:
        virtual void paint( QPainter*, const QStyleOptionViewItem&, const QModelIndex& ) const;
        virtual QSize sizeHint( const QStyleOptionViewItem&, const QModelIndex& ) const;

    private:
        QComboBox * myCombo;

};

class FilterBarComboBox: public QComboBox
{
        Q_OBJECT

    public:
        FilterBarComboBox( QWidget * parent = 0 );

    protected:
        virtual void paintEvent( QPaintEvent * e );
};


class FilterBar: public QWidget
{
        Q_OBJECT

    public:
        FilterBar( Prefs& prefs, TorrentModel& torrents, TorrentFilter& filter, QWidget * parent = 0 );
        ~FilterBar( );

    private:
        QComboBox * createTrackerCombo( QStandardItemModel *  );
        QComboBox * createActivityCombo( );
        void recountSoon( );
        void refreshTrackers( );

    private:
        Prefs& myPrefs;
        TorrentModel& myTorrents;
        TorrentFilter& myFilter;
        QComboBox * myActivityCombo;
        QComboBox * myTrackerCombo;
        QStandardItemModel * myTrackerModel;
        QTimer * myRecountTimer;
        bool myIsBootstrapping;
        QLineEdit * myLineEdit;

    private slots:
        void recount( );
        void refreshPref( int key );
        void onActivityIndexChanged( int index );
        void onTrackerIndexChanged( int index );
        void onTorrentModelReset( );
        void onTorrentModelRowsInserted( const QModelIndex&, int, int );
        void onTorrentModelRowsRemoved( const QModelIndex&, int, int );
        void onTorrentModelDataChanged( const QModelIndex&, const QModelIndex& );
        void onTextChanged( const QString& );
};

#endif
