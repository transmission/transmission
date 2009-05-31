/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_FILTER_H
#define QTR_TORRENT_FILTER_H

#include <QSortFilterProxyModel>
#include <QMetaType>
#include <QVariant>


struct Prefs;
struct QString;

class TorrentFilter: public QSortFilterProxyModel
{
        Q_OBJECT

    public:
        TorrentFilter( Prefs& prefs );
        virtual ~TorrentFilter( );

    public:
        enum TextMode { FILTER_BY_NAME, FILTER_BY_FILES, FILTER_BY_TRACKER };
        TextMode getTextMode( ) const { return myTextMode; }
        int hiddenRowCount( ) const;

    public slots:
        void setTextMode( int textMode );
        void setText( QString );

    private slots:
        void refreshPref( int key );

    protected:
        virtual bool filterAcceptsRow( int, const QModelIndex& ) const;
        virtual bool lessThan( const QModelIndex&, const QModelIndex& ) const;

    private:
        Prefs& myPrefs;
        TextMode myTextMode;
        QString myText;
};

#endif
