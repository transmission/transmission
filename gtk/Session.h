// Copyright © Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <giomm.h>
#include <glibmm.h>
#include <gtkmm.h>

#include <libtransmission/transmission.h>
#include <libtransmission/tr-macros.h>
#include <libtransmission/variant.h>

class Session : public Glib::Object
{
public:
    enum ErrorCode
    {
        ERR_ADD_TORRENT_ERR = TR_PARSE_ERR,
        ERR_ADD_TORRENT_DUP = TR_PARSE_DUPLICATE,
        ERR_NO_MORE_TORRENTS = 1000 /* finished adding a batch */
    };

public:
    ~Session() override;

    TR_DISABLE_COPY_MOVE(Session)

    static Glib::RefPtr<Session> create(tr_session* session);

    tr_session* close();

    /* Return the model used without incrementing the reference count */
    Glib::RefPtr<Gtk::TreeModel> get_model() const;

    void clear();

    tr_session* get_session() const;

    size_t get_active_torrent_count() const;

    size_t get_torrent_count() const;

    tr_torrent* find_torrent(int id) const;

    /******
    *******
    ******/

    /**
     * Load saved state and return number of torrents added.
     * May trigger one or more "error" signals with ERR_ADD_TORRENT
     */
    void load(bool forcepaused);

    /**
     * Add a list of torrents.
     * This function assumes ownership of torrentFiles
     *
     * May pop up dialogs for each torrent if that preference is enabled.
     * May trigger one or more "error" signals with ERR_ADD_TORRENT
     */
    void add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify);

    /** @brief Add a torrent from a URL */
    bool add_from_url(Glib::ustring const& url);

    /** @brief Add a torrent.
        @param ctor this function assumes ownership of the ctor */
    void add_ctor(tr_ctor* ctor);

    /** Add a torrent. */
    void add_torrent(tr_torrent*, bool do_notify);

    /**
     * Notifies listeners that torrents have been added.
     * This should be called after one or more tr_core_add* () calls.
     */
    void torrents_added();

    void torrent_changed(int id);

    /******
    *******
    ******/

    /* remove a torrent */
    void remove_torrent(int id, bool delete_files);

    /* update the model with current torrent status */
    void update();

    /**
     * Attempts to start a torrent immediately.
     */
    void start_now(int id);

    /**
    ***  Set a preference value, save the prefs file, and emit the "prefs-changed" signal
    **/

    void set_pref(tr_quark key, std::string const& val);
    void set_pref(tr_quark key, bool val);
    void set_pref(tr_quark key, int val);
    void set_pref(tr_quark key, double val);

    /**
    ***
    **/

    void port_test();

    void blocklist_update();

    void exec(tr_variant const* benc);

    void open_folder(int torrent_id);

    sigc::signal<void(ErrorCode, Glib::ustring const&)>& signal_add_error();
    sigc::signal<void(tr_ctor*)>& signal_add_prompt();
    sigc::signal<void(int)>& signal_blocklist_updated();
    sigc::signal<void(bool)>& signal_busy();
    sigc::signal<void(tr_quark)>& signal_prefs_changed();
    sigc::signal<void(bool)>& signal_port_tested();

protected:
    explicit Session(tr_session* session);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};

/**
***
**/

class TorrentModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    TorrentModelColumns();

    Gtk::TreeModelColumn<Glib::ustring> name_collated;
    Gtk::TreeModelColumn<gpointer> torrent;
    Gtk::TreeModelColumn<int> torrent_id;
    Gtk::TreeModelColumn<double> speed_up;
    Gtk::TreeModelColumn<double> speed_down;
    Gtk::TreeModelColumn<int> active_peers_up;
    Gtk::TreeModelColumn<int> active_peers_down;
    Gtk::TreeModelColumn<double> recheck_progress;
    Gtk::TreeModelColumn<bool> active;
    Gtk::TreeModelColumn<tr_torrent_activity> activity;
    Gtk::TreeModelColumn<bool> finished;
    Gtk::TreeModelColumn<tr_priority_t> priority;
    Gtk::TreeModelColumn<int> queue_position;
    Gtk::TreeModelColumn<unsigned int> trackers;
    /* tr_stat.error
     * Tracked because ACTIVITY_FILTER_ERROR needs the row-changed events */
    Gtk::TreeModelColumn<int> error;
    /* tr_stat.{ peersSendingToUs + peersGettingFromUs + webseedsSendingToUs }
     * Tracked because ACTIVITY_FILTER_ACTIVE needs the row-changed events */
    Gtk::TreeModelColumn<int> active_peer_count;
};

extern TorrentModelColumns const torrent_cols;
