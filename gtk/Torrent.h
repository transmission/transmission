// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "Flags.h"

#include <libtransmission/transmission.h>
#include <libtransmission/values.h>

#include <giomm/icon.h>
#include <glibmm/extraclassinit.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/treemodelcolumn.h>

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <initializer_list>
#include <memory>

class Percents;

class Torrent
    : public Glib::ExtraClassInit
    , public Glib::Object
{
public:
    class Columns : public Gtk::TreeModelColumnRecord
    {
    public:
        Columns();

        Gtk::TreeModelColumn<Torrent*> self;
        Gtk::TreeModelColumn<Glib::ustring> name_collated;
    };

    enum class ChangeFlag : uint8_t
    {
        ACTIVE_PEER_COUNT,
        ACTIVE_PEERS_DOWN,
        ACTIVE_PEERS_UP,
        ACTIVE,
        ACTIVITY,
        ADDED_DATE,
        ERROR_CODE,
        ERROR_MESSAGE,
        ETA,
        FINISHED,
        HAS_METADATA,
        LONG_PROGRESS,
        LONG_STATUS,
        MIME_TYPE,
        NAME,
        PERCENT_COMPLETE,
        PERCENT_DONE,
        PRIORITY,
        QUEUE_POSITION,
        RATIO,
        RECHECK_PROGRESS,
        SEED_RATIO_PERCENT_DONE,
        SPEED_DOWN,
        SPEED_UP,
        STALLED,
        TOTAL_SIZE,
        TRACKERS,
        N_FLAGS,
    };

    using ChangeFlags = Flags<ChangeFlag>;

public:
    using Speed = libtransmission::Values::Speed;
    using Storage = libtransmission::Values::Storage;

    int get_active_peer_count() const;
    int get_active_peers_down() const;
    int get_active_peers_up() const;
    bool get_active() const;
    tr_torrent_activity get_activity() const;
    time_t get_added_date() const;
    int get_error_code() const;
    Glib::ustring const& get_error_message() const;
    time_t get_eta() const;
    bool get_finished() const;
    tr_torrent_id_t get_id() const;
    Glib::ustring const& get_name_collated() const;
    Glib::ustring get_name() const;
    Percents get_percent_complete() const;
    Percents get_percent_done() const;
    float get_percent_done_fraction() const;
    tr_priority_t get_priority() const;
    size_t get_queue_position() const;
    float get_ratio() const;
    Percents get_recheck_progress() const;
    Percents get_seed_ratio_percent_done() const;
    Speed get_speed_down() const;
    Speed get_speed_up() const;
    tr_torrent& get_underlying() const;
    Storage get_total_size() const;
    unsigned int get_trackers() const;

    Glib::RefPtr<Gio::Icon> get_icon() const;
    Glib::ustring get_short_status_text() const;
    Glib::ustring get_long_progress_text() const;
    Glib::ustring get_long_status_text() const;
    bool get_sensitive() const;
    std::vector<Glib::ustring> get_css_classes() const;

    ChangeFlags update();

    static Glib::RefPtr<Torrent> create(tr_torrent* torrent);

    static Columns const& get_columns();

    static int get_item_id(Glib::RefPtr<Glib::ObjectBase const> const& item);
    static void get_item_value(Glib::RefPtr<Glib::ObjectBase const> const& item, int column, Glib::ValueBase& value);

    static int compare_by_id(Glib::RefPtr<Torrent const> const& lhs, Glib::RefPtr<Torrent const> const& rhs);
    static bool less_by_id(Glib::RefPtr<Torrent const> const& lhs, Glib::RefPtr<Torrent const> const& rhs);

private:
    Torrent();
    explicit Torrent(tr_torrent* torrent);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};

DEFINE_FLAGS_OPERATORS(Torrent::ChangeFlag)
