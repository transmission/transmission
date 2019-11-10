/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

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

class DetailsDialog : public BaseDialog
{
    Q_OBJECT

public:
    DetailsDialog(Session&, Prefs&, TorrentModel const&, QWidget* parent = nullptr);
    virtual ~DetailsDialog();

    void setIds(QSet<int> const& ids);

    // QWidget
    QSize sizeHint() const override
    {
        return QSize(440, 460);
    }

private:
    void initPeersTab();
    void initTrackerTab();
    void initInfoTab();
    void initFilesTab();
    void initOptionsTab();

    void getNewData();

    QIcon getStockIcon(QString const& freedesktop_name, int fallback);
    void setEnabled(bool);

private slots:
    void refresh();
    void refreshPref(int key);

    void onTorrentsChanged(QSet<int> const& ids);
    void onTimer();

    // Tracker tab
    void onTrackerSelectionChanged();
    void onAddTrackerClicked();
    void onEditTrackerClicked();
    void onRemoveTrackerClicked();
    void onShowTrackerScrapesToggled(bool);
    void onShowBackupTrackersToggled(bool);

    // Files tab
    void onFilePriorityChanged(QSet<int> const& fileIndices, int);
    void onFileWantedChanged(QSet<int> const& fileIndices, bool);
    void onPathEdited(QString const& oldpath, QString const& newname);
    void onOpenRequested(QString const& path);

    // Options tab
    void onBandwidthPriorityChanged(int);
    void onHonorsSessionLimitsToggled(bool);
    void onDownloadLimitedToggled(bool);
    void onSpinBoxEditingFinished();
    void onUploadLimitedToggled(bool);
    void onRatioModeChanged(int);
    void onIdleModeChanged(int);
    void onIdleLimitChanged();

private:
    Session& mySession;
    Prefs& myPrefs;
    TorrentModel const& myModel;

    Ui::DetailsDialog ui;

    QSet<int> myIds;
    QTimer myTimer;
    bool myChangedTorrents;
    bool myHavePendingRefresh;

    TrackerModel* myTrackerModel;
    TrackerModelFilter* myTrackerFilter;
    TrackerDelegate* myTrackerDelegate;

    QMap<QString, QTreeWidgetItem*> myPeers;
};
