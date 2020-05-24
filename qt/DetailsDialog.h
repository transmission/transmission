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
#include "Typedefs.h"

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

    void setIds(torrent_ids_t const& ids);

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
    void onTimer();

    void onTorrentEdited(torrent_ids_t const& ids);
    void onTorrentsChanged(torrent_ids_t const& ids);

    // Tracker tab
    void onTrackerSelectionChanged();
    void onAddTrackerClicked();
    void onEditTrackerClicked();
    void onRemoveTrackerClicked();
    void onShowTrackerScrapesToggled(bool);
    void onShowBackupTrackersToggled(bool);

    // Files tab
    void onFilePriorityChanged(QSet<int> const& file_indices, int);
    void onFileWantedChanged(QSet<int> const& file_indices, bool);
    void onPathEdited(QString const& old_path, QString const& new_name);
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
    Session& session_;
    Prefs& prefs_;
    TorrentModel const& model_;

    Ui::DetailsDialog ui_;

    torrent_ids_t ids_;
    QTimer timer_;
    bool changed_torrents_;
    bool have_pending_refresh_;

    TrackerModel* tracker_model_ = {};
    TrackerModelFilter* tracker_filter_ = {};
    TrackerDelegate* tracker_delegate_ = {};

    QMap<QString, QTreeWidgetItem*> peers_;
};
