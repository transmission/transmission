/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FILTER_BAR_H
#define QTR_FILTER_BAR_H

#include <QWidget>

class QLabel;
class QStandardItemModel;
class QTimer;

class FilterBarComboBox;
class FilterBarLineEdit;
class Prefs;
class TorrentFilter;
class TorrentModel;

class FilterBar: public QWidget
{
    Q_OBJECT

  public:
    FilterBar (Prefs& prefs, const TorrentModel& torrents, const TorrentFilter& filter, QWidget * parent = 0);
    ~FilterBar ();

  private:
    FilterBarComboBox * createTrackerCombo (QStandardItemModel * );
    FilterBarComboBox * createActivityCombo ();
    void recountSoon ();
    void refreshTrackers ();
    QString getCountString (int n) const;

  private:
    Prefs& myPrefs;
    const TorrentModel& myTorrents;
    const TorrentFilter& myFilter;
    FilterBarComboBox * myActivityCombo;
    FilterBarComboBox * myTrackerCombo;
    QLabel * myCountLabel;
    QStandardItemModel * myTrackerModel;
    QTimer * myRecountTimer;
    bool myIsBootstrapping;
    FilterBarLineEdit * myLineEdit;

  private slots:
    void recount ();
    void refreshPref (int key);
    void refreshCountLabel ();
    void onActivityIndexChanged (int index);
    void onTrackerIndexChanged (int index);
    void onTorrentModelReset ();
    void onTorrentModelRowsInserted (const QModelIndex&, int, int);
    void onTorrentModelRowsRemoved (const QModelIndex&, int, int);
    void onTorrentModelDataChanged (const QModelIndex&, const QModelIndex&);
    void onTextChanged (const QString&);
};

#endif // QTR_FILTER_BAR_H
