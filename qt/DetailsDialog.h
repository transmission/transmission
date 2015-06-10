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

#include <QDialog>
#include <QString>
#include <QMap>
#include <QSet>
#include <QTimer>

#include "Prefs.h"

#include "ui_DetailsDialog.h"

class QTreeWidgetItem;
class Session;
class Torrent;
class TorrentModel;
class TrackerDelegate;
class TrackerModel;
class TrackerModelFilter;

class DetailsDialog: public QDialog
{
    Q_OBJECT

  private:
    void getNewData ();

  private slots:
    void onTorrentChanged ();
    void onTimer ();

  public:
    DetailsDialog (Session&, Prefs&, const TorrentModel&, QWidget * parent = 0);
    ~DetailsDialog ();
    void setIds (const QSet<int>& ids);
    virtual QSize sizeHint () const { return QSize (440, 460); }

  private:
    void initPeersTab ();
    void initTrackerTab ();
    void initInfoTab ();
    void initFilesTab ();
    void initOptionsTab ();

  private:
    QIcon getStockIcon (const QString& freedesktop_name, int fallback);
    QString timeToStringRounded (int seconds);
    QString trimToDesiredWidth (const QString& str);

  private:
    Session& mySession;
    Prefs& myPrefs;
    const TorrentModel& myModel;
    QSet<int> myIds;
    QTimer myTimer;
    bool myChangedTorrents;
    bool myHavePendingRefresh;

    Ui::DetailsDialog ui;

    TrackerModel * myTrackerModel;
    TrackerModelFilter * myTrackerFilter;
    TrackerDelegate * myTrackerDelegate;

    QMap<QString,QTreeWidgetItem*> myPeers;

  private slots:
    void refreshPref (int key);
    void onBandwidthPriorityChanged (int);
    void onFilePriorityChanged (const QSet<int>& fileIndices, int);
    void onFileWantedChanged (const QSet<int>& fileIndices, bool);
    void onPathEdited (const QString& oldpath, const QString& newname);
    void onOpenRequested (const QString& path);
    void onHonorsSessionLimitsToggled (bool);
    void onDownloadLimitedToggled (bool);
    void onSpinBoxEditingFinished ();
    void onUploadLimitedToggled (bool);
    void onRatioModeChanged (int);
    void onIdleModeChanged (int);
    void onIdleLimitChanged ();
    void onShowTrackerScrapesToggled (bool);
    void onShowBackupTrackersToggled (bool);
    void onTrackerSelectionChanged ();
    void onAddTrackerClicked ();
    void onEditTrackerClicked ();
    void onRemoveTrackerClicked ();
    void refresh ();
};

#endif // QTR_DETAILS_DIALOG_H
