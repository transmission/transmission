/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_DETAILS_DIALOG_H
#define QTR_DETAILS_DIALOG_H

#include <QString>
#include <QMap>
#include <QSet>
#include <QTimer>

#include "BaseDialog.h"

#include "ui_DetailsDialog.h"

class QTreeWidgetItem;

class Prefs;
class Session;
class Torrent;
class TorrentModel;
class TrackerDelegate;
class TrackerModel;
class TrackerModelFilter;

class DetailsDialog: public BaseDialog
{
    Q_OBJECT

  public:
    DetailsDialog (Session&, Prefs&, const TorrentModel&, QWidget * parent = nullptr);
    virtual ~DetailsDialog ();

    void setIds (const QSet<int>& ids);

    // QWidget
    QSize sizeHint () const override { return QSize (440, 460); }

  private:
    void initPeersTab ();
    void initTrackerTab ();
    void initInfoTab ();
    void initFilesTab ();
    void initOptionsTab ();

    QIcon getStockIcon (const QString& freedesktop_name, int fallback);

  private slots:
    void refresh ();
    void refreshPref (int key);

    void onTorrentChanged (int id);
    void getNewData ();

    // Tracker tab
    void onTrackerSelectionChanged ();
    void onAddTrackerClicked ();
    void onEditTrackerClicked ();
    void onRemoveTrackerClicked ();
    void onShowTrackerScrapesToggled (bool);
    void onShowBackupTrackersToggled (bool);

    // Files tab
    void onFilePriorityChanged (const QSet<int>& fileIndices, int);
    void onFileWantedChanged (const QSet<int>& fileIndices, bool);
    void onPathEdited (const QString& oldpath, const QString& newname);
    void onOpenRequested (const QString& path);

    // Options tab
    void onBandwidthPriorityChanged (int);
    void onHonorsSessionLimitsToggled (bool);
    void onDownloadLimitedToggled (bool);
    void onSpinBoxEditingFinished ();
    void onUploadLimitedToggled (bool);
    void onRatioModeChanged (int);
    void onIdleModeChanged (int);
    void onIdleLimitChanged (int unused = 0);

  private:
    Session& mySession;
    Prefs& myPrefs;
    const TorrentModel& myModel;

    Ui::DetailsDialog ui;

    QSet<int> myIds;
    QTimer myTimer;
    bool myChangedTorrents;
    bool myHavePendingRefresh;

    TrackerModel * myTrackerModel;
    TrackerModelFilter * myTrackerFilter;
    TrackerDelegate * myTrackerDelegate;

    QMap<QString, QTreeWidgetItem*> myPeers;
};

#endif // QTR_DETAILS_DIALOG_H
