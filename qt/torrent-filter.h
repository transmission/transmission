/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_FILTER_H
#define QTR_TORRENT_FILTER_H

#include <QSortFilterProxyModel>
#include <QMetaType>
#include <QVariant>

class QString;
class QWidget;

class FilterMode;
class Prefs;
class Torrent;

class TorrentFilter: public QSortFilterProxyModel
{
    Q_OBJECT

  public:
    TorrentFilter (Prefs& prefs);
    virtual ~TorrentFilter ();

  public:
    enum TextMode { FILTER_BY_NAME, FILTER_BY_FILES, FILTER_BY_TRACKER };
    int hiddenRowCount () const;

  private slots:
    void refreshPref (int key);

  protected:
    virtual bool filterAcceptsRow (int, const QModelIndex&) const;
    virtual bool lessThan (const QModelIndex&, const QModelIndex&) const;

  private:
    bool activityFilterAcceptsTorrent (const Torrent * tor, const FilterMode& mode) const;
    bool trackerFilterAcceptsTorrent (const Torrent * tor, const QString& tracker) const;

  public:
    void countTorrentsPerMode (int * setmeCounts) const;

  private:
    Prefs& myPrefs;
};

#endif
