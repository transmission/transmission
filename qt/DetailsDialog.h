// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <map>
#include <memory>
#include <unordered_set>

#include <QString>
#include <QTimer>

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

public:
    DetailsDialog(Session& session, Prefs& prefs, TorrentModel const& model, QWidget* parent = nullptr);
    DetailsDialog(DetailsDialog&&) = delete;
    DetailsDialog(DetailsDialog const&) = delete;
    DetailsDialog& operator=(DetailsDialog&&) = delete;
    DetailsDialog& operator=(DetailsDialog const&) = delete;
    ~DetailsDialog() override;

    void set_ids(torrent_ids_t const& ids);

    // QWidget
    QSize sizeHint() const override
    {
        return QSize{ 440, 460 };
    }

private:
    void init_peers_tab();
    void init_tracker_tab();
    void init_info_tab();
    void init_files_tab() const;
    void init_options_tab();

    void setEnabled(bool enabled);

private slots:
    void refresh_model();
    void refresh_pref(int key);
    void refresh_ui();

    void on_torrents_edited(torrent_ids_t const& ids);
    void on_torrents_changed(torrent_ids_t const& ids, Torrent::fields_t const& fields);
    void on_session_called(Session::Tag tag);

    // Details tab
    void on_button_box_clicked(QAbstractButton* button);

    // Tracker tab
    void on_tracker_selection_changed();
    void on_add_tracker_clicked();
    void on_edit_trackers_clicked();
    void on_remove_tracker_clicked();
    void on_show_tracker_scrapes_toggled(bool val);
    void on_show_backup_trackers_toggled(bool val);
    void on_tracker_list_edited(QString const& tracker_list);

    // Files tab
    void on_file_priority_changed(file_indices_t const& file_indices, int priority);
    void on_file_wanted_changed(file_indices_t const& file_indices, bool wanted);
    void on_path_edited(QString const& old_path, QString const& new_name);
    void on_open_requested(QString const& path) const;

    // Options tab
    void on_bandwidth_priority_changed(int index);
    void on_honors_session_limits_toggled(bool val);
    void on_download_limited_toggled(bool val);
    void on_spin_box_editing_finished();
    void on_upload_limited_toggled(bool val);
    void on_ratio_mode_changed(int index);
    void on_idle_mode_changed(int index);
    void on_idle_limit_changed();

private:
    /* When a torrent property is edited in the details dialog (e.g.
       file priority, speed limits, etc.), don't update those UI fields
       until we know the server has processed the request. This keeps
       the UI from appearing to undo the change if we receive a refresh
       that was already in-flight _before_ the property was edited. */
    bool can_edit() const
    {
        return std::empty(pending_changes_tags_);
    }
    std::unordered_set<Session::Tag> pending_changes_tags_;
    QMetaObject::Connection pending_changes_connection_;

    template<typename T>
    void torrent_set(torrent_ids_t const& ids, tr_quark key, T const& val)
    {
        auto const tag = session_.torrent_set(ids, key, val);
        pending_changes_tags_.insert(tag);
        if (!pending_changes_connection_)
        {
            pending_changes_connection_ = connect(&session_, &Session::session_called, this, &DetailsDialog::on_session_called);
        }
    }

    template<typename T>
    void torrent_set(tr_quark key, T const& val)
    {
        torrent_set(ids_, key, val);
    }

    Session& session_;
    Prefs& prefs_;
    TorrentModel const& model_;

    Ui::DetailsDialog ui_ = {};
    Ui::TrackersDialog trackers_ui_ = {};

    torrent_ids_t ids_;
    QTimer model_timer_;
    QTimer ui_debounce_timer_;
    bool labels_need_refresh_ = true;
    QString labels_baseline_;

    std::shared_ptr<TrackerModel> tracker_model_;
    std::shared_ptr<TrackerModelFilter> tracker_filter_;
    std::shared_ptr<TrackerDelegate> tracker_delegate_;

    std::map<QString, QTreeWidgetItem*> peers_;

    QIcon const icon_encrypted_ = QIcon{ QStringLiteral(":/icons/encrypted.svg") };
    QIcon const icon_unencrypted_;

    static int prev_tab_index;
};
