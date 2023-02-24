// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>
#include <unordered_set>

#include <QString>
#include <QMap>
#include <QSet>
#include <QTimer>

#include <libtransmission/tr-macros.h>

#include "BaseDialog.h"
#include "Session.h"
#include "Typedefs.h"

#include "ui_DetailsDialog.h"
#include "ui_TrackersDialog.h"

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
    TR_DISABLE_COPY_MOVE(DetailsDialog)

public:
    DetailsDialog(Session&, Prefs&, TorrentModel const&, QWidget* parent = nullptr);
    ~DetailsDialog() override;

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
    void initFilesTab() const;
    void initOptionsTab();

    void setEnabled(bool);

private slots:
    void refreshModel();
    void refreshPref(int key);
    void refreshUI();

    void onTorrentsEdited(torrent_ids_t const& ids);
    void onTorrentsChanged(torrent_ids_t const& ids, Torrent::fields_t const& fields);
    void onSessionCalled(Session::Tag tag);

    // Tracker tab
    void onTrackerSelectionChanged();
    void onAddTrackerClicked();
    void onEditTrackersClicked();
    void onRemoveTrackerClicked();
    void onShowTrackerScrapesToggled(bool);
    void onShowBackupTrackersToggled(bool);
    void onTrackerListEdited(QString);

    // Files tab
    void onFilePriorityChanged(QSet<int> const& file_indices, int);
    void onFileWantedChanged(QSet<int> const& file_indices, bool);
    void onPathEdited(QString const& old_path, QString const& new_name);
    void onOpenRequested(QString const& path) const;

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
    /* When a torrent property is edited in the details dialog (e.g.
       file priority, speed limits, etc.), don't update those UI fields
       until we know the server has processed the request. This keeps
       the UI from appearing to undo the change if we receive a refresh
       that was already in-flight _before_ the property was edited. */
    bool canEdit() const
    {
        return std::empty(pending_changes_tags_);
    }
    std::unordered_set<Session::Tag> pending_changes_tags_;
    QMetaObject::Connection pending_changes_connection_;

    template<typename T>
    void torrentSet(torrent_ids_t const& ids, tr_quark key, T val)
    {
        auto const tag = session_.torrentSet(ids, key, val);
        pending_changes_tags_.insert(tag);
        if (!pending_changes_connection_)
        {
            pending_changes_connection_ = connect(&session_, &Session::sessionCalled, this, &DetailsDialog::onSessionCalled);
        }
    }

    template<typename T>
    void torrentSet(tr_quark key, T val)
    {
        torrentSet(ids_, key, val);
    }

    Session& session_;
    Prefs& prefs_;
    TorrentModel const& model_;

    Ui::DetailsDialog ui_ = {};
    Ui::TrackersDialog trackers_ui_ = {};

    torrent_ids_t ids_;
    QTimer model_timer_;
    QTimer ui_debounce_timer_;

    std::shared_ptr<TrackerModel> tracker_model_;
    std::shared_ptr<TrackerModelFilter> tracker_filter_;
    std::shared_ptr<TrackerDelegate> tracker_delegate_;

    QMap<QString, QTreeWidgetItem*> peers_;

    QIcon const icon_encrypted_ = QIcon(QStringLiteral(":/icons/encrypted.svg"));
    QIcon const icon_unencrypted_ = {};

    static int prev_tab_index_;
};
