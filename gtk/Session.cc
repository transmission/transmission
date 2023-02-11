// Copyright © Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#include <algorithm>
#include <cmath> // pow()
#include <cstring> // strstr
#include <cinttypes> // PRId64
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <glibmm/i18n.h>

#include <event2/buffer.h>

#include <fmt/core.h>

#include <libtransmission/transmission.h>

#include <libtransmission/log.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/tr-assert.h>
#include <libtransmission/utils.h> // tr_time()
#include <libtransmission/web-utils.h> // tr_urlIsValid()
#include <libtransmission/variant.h>

#include "Actions.h"
#include "Notify.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

using namespace std::literals;

namespace
{

class TrVariantDeleter
{
public:
    void operator()(tr_variant* ptr) const
    {
        tr_variantClear(ptr);
        std::default_delete<tr_variant>()(ptr);
    }
};

using TrVariantPtr = std::unique_ptr<tr_variant, TrVariantDeleter>;

TrVariantPtr create_variant(tr_variant& other)
{
    auto result = TrVariantPtr(new tr_variant(other));
    tr_variantInitBool(&other, false);
    return result;
}

class ScopedModelSortBlocker
{
public:
    explicit ScopedModelSortBlocker(Gtk::TreeSortable& model)
        : model_(model)
    {
        model_.get_sort_column_id(sort_column_id_, sort_type_);
        model_.set_sort_column(Gtk::TreeSortable::DEFAULT_SORT_COLUMN_ID, TR_GTK_SORT_TYPE(ASCENDING));
    }

    ~ScopedModelSortBlocker()
    {
        model_.set_sort_column(sort_column_id_, sort_type_);
    }

    TR_DISABLE_COPY_MOVE(ScopedModelSortBlocker)

private:
    Gtk::TreeSortable& model_;
    int sort_column_id_ = -1;
    Gtk::SortType sort_type_ = TR_GTK_SORT_TYPE(ASCENDING);
};

} // namespace

class Session::Impl
{
public:
    Impl(Session& core, tr_session* session);

    tr_session* close();

    Glib::RefPtr<Gtk::ListStore> get_raw_model() const;
    Glib::RefPtr<Gtk::TreeModelSort> get_model();
    Glib::RefPtr<Gtk::TreeModelSort const> get_model() const;
    tr_session* get_session() const;

    size_t get_active_torrent_count() const;

    void update();
    void torrents_added();

    void add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify);
    int add_ctor(tr_ctor* ctor, bool do_prompt, bool do_notify);
    void add_torrent(tr_torrent* tor, bool do_notify);
    bool add_from_url(Glib::ustring const& url);

    void send_rpc_request(tr_variant const* request, int64_t tag, std::function<void(tr_variant&)> const& response_func);

    void commit_prefs_change(tr_quark key);

    auto& signal_add_error()
    {
        return signal_add_error_;
    }

    auto& signal_add_prompt()
    {
        return signal_add_prompt_;
    }

    auto& signal_blocklist_updated()
    {
        return signal_blocklist_updated_;
    }

    auto& signal_busy()
    {
        return signal_busy_;
    }

    auto& signal_prefs_changed()
    {
        return signal_prefs_changed_;
    }

    auto& signal_port_tested()
    {
        return signal_port_tested_;
    }

private:
    Glib::RefPtr<Session> get_core_ptr() const;

    bool is_busy() const;
    void add_to_busy(int addMe);
    void inc_busy();
    void dec_busy();

    bool add_file(Glib::RefPtr<Gio::File> const& file, bool do_start, bool do_prompt, bool do_notify);
    void add_file_async_callback(
        Glib::RefPtr<Gio::File> const& file,
        Glib::RefPtr<Gio::AsyncResult>& result,
        tr_ctor* ctor,
        bool do_prompt,
        bool do_notify);

    tr_torrent* create_new_torrent(tr_ctor* ctor);

    void set_sort_mode(std::string_view mode, bool is_reversed);

    void maybe_inhibit_hibernation();
    void set_hibernation_allowed(bool allowed);

    void watchdir_update();
    void watchdir_scan();
    void watchdir_monitor_file(Glib::RefPtr<Gio::File> const& file);
    bool watchdir_idle();
    void on_file_changed_in_watchdir(
        Glib::RefPtr<Gio::File> const& file,
        Glib::RefPtr<Gio::File> const& other_type,
        IF_GLIBMM2_68(Gio::FileMonitor::Event, Gio::FileMonitorEvent) event_type);

    void on_pref_changed(tr_quark key);

    void on_torrent_completeness_changed(tr_torrent* tor, tr_completeness completeness, bool was_running);
    void on_torrent_metadata_changed(tr_torrent* tor);

private:
    Session& core_;

    sigc::signal<void(ErrorCode, Glib::ustring const&)> signal_add_error_;
    sigc::signal<void(tr_ctor*)> signal_add_prompt_;
    sigc::signal<void(int)> signal_blocklist_updated_;
    sigc::signal<void(bool)> signal_busy_;
    sigc::signal<void(tr_quark)> signal_prefs_changed_;
    sigc::signal<void(bool)> signal_port_tested_;

    Glib::RefPtr<Gio::FileMonitor> monitor_;
    sigc::connection monitor_tag_;
    Glib::RefPtr<Gio::File> monitor_dir_;
    std::vector<Glib::RefPtr<Gio::File>> monitor_files_;
    sigc::connection monitor_idle_tag_;

    bool adding_from_watch_dir_ = false;
    bool inhibit_allowed_ = false;
    bool have_inhibit_cookie_ = false;
    bool dbus_error_ = false;
    guint inhibit_cookie_ = 0;
    gint busy_count_ = 0;
    Glib::RefPtr<Gtk::ListStore> raw_model_;
    Glib::RefPtr<Gtk::TreeModelSort> sorted_model_;
    tr_session* session_ = nullptr;
};

TorrentModelColumns::TorrentModelColumns() noexcept
{
    add(name_collated);
    add(torrent);
    add(torrent_id);
    add(speed_up);
    add(speed_down);
    add(active_peers_up);
    add(active_peers_down);
    add(recheck_progress);
    add(active);
    add(activity);
    add(finished);
    add(priority);
    add(queue_position);
    add(trackers);
    add(error);
    add(active_peer_count);
}

TorrentModelColumns const torrent_cols;

Glib::RefPtr<Session> Session::Impl::get_core_ptr() const
{
    core_.reference();
    return Glib::make_refptr_for_instance(&core_);
}

/***
****
***/

Glib::RefPtr<Gtk::ListStore> Session::Impl::get_raw_model() const
{
    return raw_model_;
}

Glib::RefPtr<Gtk::TreeModel> Session::get_model() const
{
    return impl_->get_model();
}

Glib::RefPtr<Gtk::TreeModelSort> Session::Impl::get_model()
{
    return sorted_model_;
}

Glib::RefPtr<Gtk::TreeModelSort const> Session::Impl::get_model() const
{
    return sorted_model_;
}

tr_session* Session::get_session() const
{
    return impl_->get_session();
}

tr_session* Session::Impl::get_session() const
{
    return session_;
}

/***
****  BUSY
***/

bool Session::Impl::is_busy() const
{
    return busy_count_ > 0;
}

void Session::Impl::add_to_busy(int addMe)
{
    bool const wasBusy = is_busy();

    busy_count_ += addMe;

    if (wasBusy != is_busy())
    {
        signal_busy_.emit(is_busy());
    }
}

void Session::Impl::inc_busy()
{
    add_to_busy(1);
}

void Session::Impl::dec_busy()
{
    add_to_busy(-1);
}

/***
****
****  SORTING THE MODEL
****
***/

namespace
{

template<typename T>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
constexpr int compare_generic(T const& a, T const& b)
{
    if (a < b)
    {
        return -1;
    }

    if (a > b)
    {
        return 1;
    }

    return 0;
}

constexpr bool is_valid_eta(time_t t)
{
    return t != TR_ETA_NOT_AVAIL && t != TR_ETA_UNKNOWN;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
constexpr int compare_eta(time_t a, time_t b)
{
    bool const a_valid = is_valid_eta(a);
    bool const b_valid = is_valid_eta(b);

    if (!a_valid && !b_valid)
    {
        return 0;
    }

    if (!a_valid)
    {
        return -1;
    }

    if (!b_valid)
    {
        return 1;
    }

    return -compare_generic(a, b);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
constexpr int compare_ratio(double a, double b)
{
    if (static_cast<int>(a) == TR_RATIO_INF && static_cast<int>(b) == TR_RATIO_INF)
    {
        return 0;
    }

    if (static_cast<int>(a) == TR_RATIO_INF)
    {
        return 1;
    }

    if (static_cast<int>(b) == TR_RATIO_INF)
    {
        return -1;
    }

    return compare_generic(a, b);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_name(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    return a->get_value(torrent_cols.name_collated).compare(b->get_value(torrent_cols.name_collated));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_queue(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    auto const* const sa = tr_torrentStatCached(static_cast<tr_torrent*>(a->get_value(torrent_cols.torrent)));
    auto const* const sb = tr_torrentStatCached(static_cast<tr_torrent*>(b->get_value(torrent_cols.torrent)));

    return sb->queuePosition - sa->queuePosition;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_ratio(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    int ret = 0;

    auto const* const sa = tr_torrentStatCached(static_cast<tr_torrent*>(a->get_value(torrent_cols.torrent)));
    auto const* const sb = tr_torrentStatCached(static_cast<tr_torrent*>(b->get_value(torrent_cols.torrent)));

    if (ret == 0)
    {
        ret = compare_ratio(sa->ratio, sb->ratio);
    }

    if (ret == 0)
    {
        ret = compare_by_queue(a, b);
    }

    return ret;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_activity(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    int ret = 0;

    auto* const ta = static_cast<tr_torrent*>(a->get_value(torrent_cols.torrent));
    auto* const tb = static_cast<tr_torrent*>(b->get_value(torrent_cols.torrent));
    auto const aUp = a->get_value(torrent_cols.speed_up);
    auto const aDown = a->get_value(torrent_cols.speed_down);
    auto const bUp = b->get_value(torrent_cols.speed_up);
    auto const bDown = b->get_value(torrent_cols.speed_down);

    ret = compare_generic(aUp + aDown, bUp + bDown);

    if (ret == 0)
    {
        auto const* const sa = tr_torrentStatCached(ta);
        auto const* const sb = tr_torrentStatCached(tb);
        ret = compare_generic(sa->peersSendingToUs + sa->peersGettingFromUs, sb->peersSendingToUs + sb->peersGettingFromUs);
    }

    if (ret == 0)
    {
        ret = compare_by_queue(a, b);
    }

    return ret;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_age(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    auto* const ta = static_cast<tr_torrent*>(a->get_value(torrent_cols.torrent));
    auto* const tb = static_cast<tr_torrent*>(b->get_value(torrent_cols.torrent));
    int ret = compare_generic(tr_torrentStatCached(ta)->addedDate, tr_torrentStatCached(tb)->addedDate);

    if (ret == 0)
    {
        ret = compare_by_name(a, b);
    }

    return ret;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_size(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    auto const size_a = tr_torrentTotalSize(static_cast<tr_torrent*>(a->get_value(torrent_cols.torrent)));
    auto const size_b = tr_torrentTotalSize(static_cast<tr_torrent*>(b->get_value(torrent_cols.torrent)));
    int ret = compare_generic(size_a, size_b);

    if (ret == 0)
    {
        ret = compare_by_name(a, b);
    }

    return ret;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_progress(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    auto const* const sa = tr_torrentStatCached(static_cast<tr_torrent*>(a->get_value(torrent_cols.torrent)));
    auto const* const sb = tr_torrentStatCached(static_cast<tr_torrent*>(b->get_value(torrent_cols.torrent)));
    int ret = compare_generic(sa->percentComplete, sb->percentComplete);

    if (ret == 0)
    {
        ret = compare_generic(sa->seedRatioPercentDone, sb->seedRatioPercentDone);
    }

    if (ret == 0)
    {
        ret = compare_by_ratio(a, b);
    }

    return ret;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_eta(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    auto const* const sa = tr_torrentStatCached(static_cast<tr_torrent*>(a->get_value(torrent_cols.torrent)));
    auto const* const sb = tr_torrentStatCached(static_cast<tr_torrent*>(b->get_value(torrent_cols.torrent)));
    int ret = compare_eta(sa->eta, sb->eta);

    if (ret == 0)
    {
        ret = compare_by_name(a, b);
    }

    return ret;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int compare_by_state(Gtk::TreeModel::const_iterator const& a, Gtk::TreeModel::const_iterator const& b)
{
    auto const sa = a->get_value(torrent_cols.activity);
    auto const sb = b->get_value(torrent_cols.activity);
    int ret = compare_generic(sa, sb);

    if (ret == 0)
    {
        ret = compare_by_queue(a, b);
    }

    return ret;
}

} // namespace

void Session::Impl::set_sort_mode(std::string_view mode, bool is_reversed)
{
    auto const& col = torrent_cols.torrent;
    Gtk::TreeSortable::SlotCompare sort_func;
    auto type = is_reversed ? TR_GTK_SORT_TYPE(ASCENDING) : TR_GTK_SORT_TYPE(DESCENDING);
    auto const sortable = get_model();

    if (mode == "sort-by-activity")
    {
        sort_func = &compare_by_activity;
    }
    else if (mode == "sort-by-age")
    {
        sort_func = &compare_by_age;
    }
    else if (mode == "sort-by-progress")
    {
        sort_func = &compare_by_progress;
    }
    else if (mode == "sort-by-queue")
    {
        sort_func = &compare_by_queue;
    }
    else if (mode == "sort-by-time-left")
    {
        sort_func = &compare_by_eta;
    }
    else if (mode == "sort-by-ratio")
    {
        sort_func = &compare_by_ratio;
    }
    else if (mode == "sort-by-state")
    {
        sort_func = &compare_by_state;
    }
    else if (mode == "sort-by-size")
    {
        sort_func = &compare_by_size;
    }
    else
    {
        sort_func = &compare_by_name;
        type = is_reversed ? TR_GTK_SORT_TYPE(DESCENDING) : TR_GTK_SORT_TYPE(ASCENDING);
    }

    sortable->set_sort_func(col, sort_func);
    sortable->set_sort_column(col, type);
}

/***
****
****  WATCHDIR
****
***/

namespace
{

time_t get_file_mtime(Glib::RefPtr<Gio::File> const& file)
{
    try
    {
        return file->query_info(G_FILE_ATTRIBUTE_TIME_MODIFIED)->get_attribute_uint64(G_FILE_ATTRIBUTE_TIME_MODIFIED);
    }
    catch (Glib::Error const&)
    {
        return 0;
    }
}

void rename_torrent(Glib::RefPtr<Gio::File> const& file)
{
    auto info = Glib::RefPtr<Gio::FileInfo>();

    try
    {
        info = file->query_info(G_FILE_ATTRIBUTE_STANDARD_EDIT_NAME);
    }
    catch (Glib::Error const&)
    {
        return;
    }

    auto const old_name = info->get_attribute_as_string(G_FILE_ATTRIBUTE_STANDARD_EDIT_NAME);
    auto const new_name = fmt::format("{}.added", old_name);

    try
    {
        file->set_display_name(new_name);
    }
    catch (Glib::Error const& e)
    {
        gtr_message(fmt::format(
            _("Couldn't rename '{old_path}' as '{path}': {error} ({error_code})"),
            fmt::arg("old_path", old_name),
            fmt::arg("path", new_name),
            fmt::arg("error", e.what()),
            fmt::arg("error_code", e.code())));
    }
}

} // namespace

bool Session::Impl::watchdir_idle()
{
    std::vector<Glib::RefPtr<Gio::File>> changing;
    std::vector<Glib::RefPtr<Gio::File>> unchanging;
    time_t const now = tr_time();

    /* separate the files into two lists: changing and unchanging */
    for (auto const& file : monitor_files_)
    {
        time_t const mtime = get_file_mtime(file);

        if (mtime + 2 >= now)
        {
            changing.push_back(file);
        }
        else
        {
            unchanging.push_back(file);
        }
    }

    /* add the files that have stopped changing */
    if (!unchanging.empty())
    {
        bool const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents);
        bool const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);

        adding_from_watch_dir_ = true;
        add_files(unchanging, do_start, do_prompt, true);
        std::for_each(unchanging.begin(), unchanging.end(), rename_torrent);
        adding_from_watch_dir_ = false;
    }

    /* keep monitoring the ones that are still changing */
    monitor_files_ = changing;

    /* if monitor_files is nonempty, keep checking every second */
    if (!monitor_files_.empty())
    {
        return true;
    }

    monitor_idle_tag_.disconnect();
    return false;
}

/* If this file is a torrent, add it to our list */
void Session::Impl::watchdir_monitor_file(Glib::RefPtr<Gio::File> const& file)
{
    auto const filename = file->get_path();
    bool const is_torrent = Glib::str_has_suffix(filename, ".torrent");

    if (is_torrent)
    {
        /* if we're not already watching this file, start watching it now */
        bool const found = std::any_of(
            monitor_files_.begin(),
            monitor_files_.end(),
            [file](auto const& f) { return file->equal(f); });

        if (!found)
        {
            monitor_files_.push_back(file);

            if (!monitor_idle_tag_.connected())
            {
                monitor_idle_tag_ = Glib::signal_timeout().connect_seconds(sigc::mem_fun(*this, &Impl::watchdir_idle), 1);
            }
        }
    }
}

/* GFileMonitor noticed a file was created */
void Session::Impl::on_file_changed_in_watchdir(
    Glib::RefPtr<Gio::File> const& file,
    Glib::RefPtr<Gio::File> const& /*other_type*/,
    IF_GLIBMM2_68(Gio::FileMonitor::Event, Gio::FileMonitorEvent) event_type)
{
    if (event_type == TR_GIO_FILE_MONITOR_EVENT(CREATED))
    {
        watchdir_monitor_file(file);
    }
}

/* walk through the pre-existing files in the watchdir */
void Session::Impl::watchdir_scan()
{
    auto const dirname = gtr_pref_string_get(TR_KEY_watch_dir);

    try
    {
        for (auto const& name : Glib::Dir(dirname))
        {
            watchdir_monitor_file(Gio::File::create_for_path(Glib::build_filename(dirname, name)));
        }
    }
    catch (Glib::FileError const&)
    {
    }
}

void Session::Impl::watchdir_update()
{
    bool const is_enabled = gtr_pref_flag_get(TR_KEY_watch_dir_enabled);
    auto const dir = Gio::File::create_for_path(gtr_pref_string_get(TR_KEY_watch_dir));

    if (monitor_ != nullptr && (!is_enabled || !dir->equal(monitor_dir_)))
    {
        monitor_tag_.disconnect();
        monitor_->cancel();

        monitor_dir_.reset();
        monitor_.reset();
    }

    if (is_enabled && monitor_ == nullptr)
    {
        auto const m = dir->monitor_directory();
        watchdir_scan();

        monitor_ = m;
        monitor_dir_ = dir;
        monitor_tag_ = m->signal_changed().connect(sigc::mem_fun(*this, &Impl::on_file_changed_in_watchdir));
    }
}

/***
****
***/

void Session::Impl::on_pref_changed(tr_quark const key)
{
    switch (key)
    {
    case TR_KEY_sort_mode:
    case TR_KEY_sort_reversed:
        {
            auto const mode = gtr_pref_string_get(TR_KEY_sort_mode);
            bool const is_reversed = gtr_pref_flag_get(TR_KEY_sort_reversed);
            set_sort_mode(mode, is_reversed);
            break;
        }

    case TR_KEY_peer_limit_global:
        tr_sessionSetPeerLimit(session_, gtr_pref_int_get(key));
        break;

    case TR_KEY_peer_limit_per_torrent:
        tr_sessionSetPeerLimitPerTorrent(session_, gtr_pref_int_get(key));
        break;

    case TR_KEY_inhibit_desktop_hibernation:
        maybe_inhibit_hibernation();
        break;

    case TR_KEY_watch_dir:
    case TR_KEY_watch_dir_enabled:
        watchdir_update();
        break;

    default:
        break;
    }
}

/**
***
**/

Glib::RefPtr<Session> Session::create(tr_session* session)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new Session(session));
}

Session::Session(tr_session* session)
    : Glib::ObjectBase(typeid(Session))
    , impl_(std::make_unique<Impl>(*this, session))
{
}

Session::~Session() = default;

Session::Impl::Impl(Session& core, tr_session* session)
    : core_(core)
    , session_(session)
{
    raw_model_ = Gtk::ListStore::create(torrent_cols);
    sorted_model_ = Gtk::TreeModelSort::create(raw_model_);
    sorted_model_->set_default_sort_func(
        [](Gtk::TreeModel::const_iterator const& /*a*/, Gtk::TreeModel::const_iterator const& /*b*/) { return 0; });

    /* init from prefs & listen to pref changes */
    on_pref_changed(TR_KEY_sort_mode);
    on_pref_changed(TR_KEY_sort_reversed);
    on_pref_changed(TR_KEY_watch_dir_enabled);
    on_pref_changed(TR_KEY_peer_limit_global);
    on_pref_changed(TR_KEY_inhibit_desktop_hibernation);
    signal_prefs_changed_.connect([this](auto key) { on_pref_changed(key); });

    tr_sessionSetMetadataCallback(
        session,
        [](auto* /*session*/, auto* tor, gpointer impl) { static_cast<Impl*>(impl)->on_torrent_metadata_changed(tor); },
        this);

    tr_sessionSetCompletenessCallback(
        session,
        [](auto* tor, auto completeness, bool was_running, gpointer impl)
        { static_cast<Impl*>(impl)->on_torrent_completeness_changed(tor, completeness, was_running); },
        this);
}

tr_session* Session::close()
{
    return impl_->close();
}

tr_session* Session::Impl::close()
{
    auto* session = session_;

    if (session != nullptr)
    {
        session_ = nullptr;
        gtr_pref_save(session);
    }

    return session;
}

/***
****  COMPLETENESS CALLBACK
***/

/* this is called in the libtransmission thread, *NOT* the GTK+ thread,
   so delegate to the GTK+ thread before calling notify's dbus code... */
void Session::Impl::on_torrent_completeness_changed(tr_torrent* tor, tr_completeness completeness, bool was_running)
{
    if (was_running && completeness != TR_LEECH && tr_torrentStat(tor)->sizeWhenDone != 0)
    {
        Glib::signal_idle().connect(
            [core = get_core_ptr(), torrent_id = tr_torrentId(tor)]()
            {
                gtr_notify_torrent_completed(core, torrent_id);
                return false;
            });
    }
}

/***
****  METADATA CALLBACK
***/

namespace
{

Glib::ustring get_collated_name(tr_torrent const* tor)
{
    return fmt::format("{}\t{}", Glib::ustring(tr_torrentName(tor)).lowercase(), tr_torrentView(tor).hash_string);
}

struct metadata_callback_data
{
    Session* core;
    tr_torrent_id_t torrent_id;
};

Gtk::TreeModel::iterator find_row_from_torrent_id(Glib::RefPtr<Gtk::TreeModel> const& model, tr_torrent_id_t id)
{
    for (auto& row : model->children())
    {
        if (id == row.get_value(torrent_cols.torrent_id))
        {
            return TR_GTK_TREE_MODEL_CHILD_ITER(row);
        }
    }

    return {};
}

} // namespace

/* this is called in the libtransmission thread, *NOT* the GTK+ thread,
   so delegate to the GTK+ thread before changing our list store... */
void Session::Impl::on_torrent_metadata_changed(tr_torrent* tor)
{
    Glib::signal_idle().connect(
        [this, core = get_core_ptr(), torrent_id = tr_torrentId(tor)]()
        {
            /* update the torrent's collated name */
            if (auto const* const tor2 = tr_torrentFindFromId(session_, torrent_id); tor2 != nullptr)
            {
                if (auto const iter = find_row_from_torrent_id(raw_model_, torrent_id); iter)
                {
                    (*iter)[torrent_cols.name_collated] = get_collated_name(tor2);
                }
            }

            return false;
        });
}

/***
****
****  ADDING TORRENTS
****
***/

namespace
{

unsigned int build_torrent_trackers_hash(tr_torrent* tor)
{
    auto hash = uint64_t{};

    for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
    {
        for (auto const ch : std::string_view{ tr_torrentTracker(tor, i).announce })
        {
            hash = (hash << 4) ^ (hash >> 28) ^ ch;
        }
    }

    return hash;
}

bool is_torrent_active(tr_stat const* st)
{
    return st->peersSendingToUs > 0 || st->peersGettingFromUs > 0 || st->activity == TR_STATUS_CHECK;
}

} // namespace

void Session::add_torrent(tr_torrent* tor, bool do_notify)
{
    ScopedModelSortBlocker const disable_sort(*impl_->get_model().get());
    impl_->add_torrent(tor, do_notify);
}

void Session::Impl::add_torrent(tr_torrent* tor, bool do_notify)
{
    if (tor != nullptr)
    {
        tr_stat const* st = tr_torrentStat(tor);
        auto const collated = get_collated_name(tor);
        auto const trackers_hash = build_torrent_trackers_hash(tor);
        auto const store = get_raw_model();

        auto const iter = store->append();
        (*iter)[torrent_cols.name_collated] = collated;
        (*iter)[torrent_cols.torrent] = tor;
        (*iter)[torrent_cols.torrent_id] = tr_torrentId(tor);
        (*iter)[torrent_cols.speed_up] = st->pieceUploadSpeed_KBps;
        (*iter)[torrent_cols.speed_down] = st->pieceDownloadSpeed_KBps;
        (*iter)[torrent_cols.active_peers_up] = st->peersGettingFromUs;
        (*iter)[torrent_cols.active_peers_down] = st->peersSendingToUs + st->webseedsSendingToUs;
        (*iter)[torrent_cols.recheck_progress] = st->recheckProgress;
        (*iter)[torrent_cols.active] = is_torrent_active(st);
        (*iter)[torrent_cols.activity] = st->activity;
        (*iter)[torrent_cols.finished] = st->finished;
        (*iter)[torrent_cols.priority] = tr_torrentGetPriority(tor);
        (*iter)[torrent_cols.queue_position] = st->queuePosition;
        (*iter)[torrent_cols.trackers] = trackers_hash;

        if (do_notify)
        {
            gtr_notify_torrent_added(get_core_ptr(), tr_torrentId(tor));
        }
    }
}

tr_torrent* Session::Impl::create_new_torrent(tr_ctor* ctor)
{
    bool do_trash = false;

    /* let the gtk client handle the removal, since libT
     * doesn't have any concept of the glib trash API */
    tr_ctorGetDeleteSource(ctor, &do_trash);
    tr_ctorSetDeleteSource(ctor, false);
    tr_torrent* const tor = tr_torrentNew(ctor, nullptr);

    if (tor != nullptr && do_trash)
    {
        char const* config = tr_sessionGetConfigDir(session_);
        char const* source = tr_ctorGetSourceFile(ctor);

        if (source != nullptr)
        {
            /* #1294: don't delete the .torrent file if it's our internal copy */
            bool const is_internal = strstr(source, config) == source;

            if (!is_internal)
            {
                gtr_file_trash_or_remove(source, nullptr);
            }
        }
    }

    return tor;
}

int Session::Impl::add_ctor(tr_ctor* ctor, bool do_prompt, bool do_notify)
{
    auto const* metainfo = tr_ctorGetMetainfo(ctor);
    if (metainfo == nullptr)
    {
        return TR_PARSE_ERR;
    }

    if (tr_torrentFindFromMetainfo(get_session(), metainfo) != nullptr)
    {
        /* don't complain about torrent files in the watch directory
         * that have already been added... that gets annoying and we
         * don't want to be nagging users to clean up their watch dirs */
        if (tr_ctorGetSourceFile(ctor) == nullptr || !adding_from_watch_dir_)
        {
            signal_add_error_.emit(ERR_ADD_TORRENT_DUP, metainfo->name().c_str());
        }

        tr_ctorFree(ctor);
        return TR_PARSE_DUPLICATE;
    }

    if (!do_prompt)
    {
        ScopedModelSortBlocker const disable_sort(*sorted_model_.get());
        add_torrent(create_new_torrent(ctor), do_notify);
        tr_ctorFree(ctor);
        return 0;
    }

    signal_add_prompt_.emit(ctor);
    return 0;
}

namespace
{

void core_apply_defaults(tr_ctor* ctor)
{
    if (!tr_ctorGetPaused(ctor, TR_FORCE, nullptr))
    {
        tr_ctorSetPaused(ctor, TR_FORCE, !gtr_pref_flag_get(TR_KEY_start_added_torrents));
    }

    if (!tr_ctorGetDeleteSource(ctor, nullptr))
    {
        tr_ctorSetDeleteSource(ctor, gtr_pref_flag_get(TR_KEY_trash_original_torrent_files));
    }

    if (!tr_ctorGetPeerLimit(ctor, TR_FORCE, nullptr))
    {
        tr_ctorSetPeerLimit(ctor, TR_FORCE, gtr_pref_int_get(TR_KEY_peer_limit_per_torrent));
    }

    if (!tr_ctorGetDownloadDir(ctor, TR_FORCE, nullptr))
    {
        tr_ctorSetDownloadDir(ctor, TR_FORCE, gtr_pref_string_get(TR_KEY_download_dir).c_str());
    }
}

} // namespace

void Session::add_ctor(tr_ctor* ctor)
{
    bool const do_notify = false;
    bool const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);
    core_apply_defaults(ctor);
    impl_->add_ctor(ctor, do_prompt, do_notify);
}

/***
****
***/

void Session::Impl::add_file_async_callback(
    Glib::RefPtr<Gio::File> const& file,
    Glib::RefPtr<Gio::AsyncResult>& result,
    tr_ctor* ctor,
    bool do_prompt,
    bool do_notify)
{
    try
    {
        gsize length = 0;
        char* contents = nullptr;

        if (!file->load_contents_finish(result, contents, length))
        {
            gtr_message(fmt::format(_("Couldn't read '{path}'"), fmt::arg("path", file->get_parse_name())));
        }
        else if (tr_ctorSetMetainfo(ctor, contents, length, nullptr))
        {
            add_ctor(ctor, do_prompt, do_notify);
        }
        else
        {
            tr_ctorFree(ctor);
        }
    }
    catch (Glib::Error const& e)
    {
        gtr_message(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", file->get_parse_name()),
            fmt::arg("error", e.what()),
            fmt::arg("error_code", e.code())));
    }

    dec_busy();
}

bool Session::Impl::add_file(Glib::RefPtr<Gio::File> const& file, bool do_start, bool do_prompt, bool do_notify)
{
    auto const* const session = get_session();
    if (session == nullptr)
    {
        return false;
    }

    bool handled = false;
    auto* ctor = tr_ctorNew(session);
    core_apply_defaults(ctor);
    tr_ctorSetPaused(ctor, TR_FORCE, !do_start);

    bool loaded = false;
    if (auto const path = file->get_path(); !std::empty(path))
    {
        // try to treat it as a file...
        loaded = tr_ctorSetMetainfoFromFile(ctor, path.c_str(), nullptr);
    }

    if (!loaded)
    {
        // try to treat it as a magnet link...
        loaded = tr_ctorSetMetainfoFromMagnetLink(ctor, file->get_uri().c_str(), nullptr);
    }

    // if we could make sense of it, add it
    if (loaded)
    {
        handled = true;
        add_ctor(ctor, do_prompt, do_notify);
    }
    else if (tr_urlIsValid(file->get_uri()))
    {
        handled = true;
        inc_busy();
        file->load_contents_async([this, file, ctor, do_prompt, do_notify](auto& result)
                                  { add_file_async_callback(file, result, ctor, do_prompt, do_notify); });
    }
    else
    {
        tr_ctorFree(ctor);
        std::cerr << fmt::format(_("Couldn't add torrent file '{path}'"), fmt::arg("path", file->get_parse_name()))
                  << std::endl;
    }

    return handled;
}

bool Session::add_from_url(Glib::ustring const& url)
{
    return impl_->add_from_url(url);
}

bool Session::Impl::add_from_url(Glib::ustring const& url)
{
    auto const file = Gio::File::create_for_uri(url);
    auto const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents);
    auto const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);
    auto const do_notify = false;

    auto const handled = add_file(file, do_start, do_prompt, do_notify);
    torrents_added();
    return handled;
}

void Session::add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify)
{
    impl_->add_files(files, do_start, do_prompt, do_notify);
}

void Session::Impl::add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify)
{
    for (auto const& file : files)
    {
        add_file(file, do_start, do_prompt, do_notify);
    }

    torrents_added();
}

void Session::torrents_added()
{
    impl_->torrents_added();
}

void Session::Impl::torrents_added()
{
    update();
    signal_add_error_.emit(ERR_NO_MORE_TORRENTS, {});
}

void Session::torrent_changed(tr_torrent_id_t id)
{
    auto const model = impl_->get_raw_model();

    if (auto const iter = find_row_from_torrent_id(model, id); iter)
    {
        model->row_changed(model->get_path(iter), iter);
    }
}

void Session::remove_torrent(tr_torrent_id_t id, bool delete_files)
{
    auto* tor = find_torrent(id);

    if (tor != nullptr)
    {
        /* remove from the gui */
        auto const model = impl_->get_raw_model();

        if (auto const iter = find_row_from_torrent_id(model, id); iter)
        {
            model->erase(iter);
        }

        /* remove the torrent */
        tr_torrentRemove(
            tor,
            delete_files,
            [](char const* filename, void* /*user_data*/, tr_error** error)
            { return gtr_file_trash_or_remove(filename, error); },
            nullptr);
    }
}

void Session::load(bool force_paused)
{
    auto* const ctor = tr_ctorNew(impl_->get_session());

    if (force_paused)
    {
        tr_ctorSetPaused(ctor, TR_FORCE, true);
    }

    tr_ctorSetPeerLimit(ctor, TR_FALLBACK, gtr_pref_int_get(TR_KEY_peer_limit_per_torrent));

    auto* session = impl_->get_session();
    auto const n_torrents = tr_sessionLoadTorrents(session, ctor);
    tr_ctorFree(ctor);

    ScopedModelSortBlocker const disable_sort(*impl_->get_model().get());

    auto torrents = std::vector<tr_torrent*>{};
    torrents.resize(n_torrents);
    tr_sessionGetAllTorrents(session, std::data(torrents), std::size(torrents));
    for (auto* tor : torrents)
    {
        impl_->add_torrent(tor, false);
    }
}

void Session::clear()
{
    impl_->get_raw_model()->clear();
}

/***
****
***/

namespace
{

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int gtr_compare_double(double const a, double const b, int decimal_places)
{
    auto const ia = int64_t(a * pow(10, decimal_places));
    auto const ib = int64_t(b * pow(10, decimal_places));

    if (ia < ib)
    {
        return -1;
    }

    if (ia > ib)
    {
        return 1;
    }

    return 0;
}

void update_foreach(Gtk::TreeModel::Row& row)
{
    /* get the old states */
    auto* const tor = static_cast<tr_torrent*>(row.get_value(torrent_cols.torrent));
    auto const oldActive = row.get_value(torrent_cols.active);
    auto const oldActivePeerCount = row.get_value(torrent_cols.active_peer_count);
    auto const oldUploadPeerCount = row.get_value(torrent_cols.active_peers_up);
    auto const oldDownloadPeerCount = row.get_value(torrent_cols.active_peers_down);
    auto const oldError = row.get_value(torrent_cols.error);
    auto const oldActivity = row.get_value(torrent_cols.activity);
    auto const oldFinished = row.get_value(torrent_cols.finished);
    auto const oldPriority = row.get_value(torrent_cols.priority);
    auto const oldQueuePosition = row.get_value(torrent_cols.queue_position);
    auto const oldTrackers = row.get_value(torrent_cols.trackers);
    auto const oldUpSpeed = row.get_value(torrent_cols.speed_up);
    auto const oldRecheckProgress = row.get_value(torrent_cols.recheck_progress);
    auto const oldDownSpeed = row.get_value(torrent_cols.speed_down);

    /* get the new states */
    auto const* const st = tr_torrentStat(tor);
    auto const newActive = is_torrent_active(st);
    auto const newActivity = st->activity;
    auto const newFinished = st->finished;
    auto const newPriority = tr_torrentGetPriority(tor);
    auto const newQueuePosition = st->queuePosition;
    auto const newTrackers = build_torrent_trackers_hash(tor);
    auto const newUpSpeed = st->pieceUploadSpeed_KBps;
    auto const newDownSpeed = st->pieceDownloadSpeed_KBps;
    auto const newRecheckProgress = st->recheckProgress;
    auto const newActivePeerCount = st->peersSendingToUs + st->peersGettingFromUs + st->webseedsSendingToUs;
    auto const newDownloadPeerCount = st->peersSendingToUs;
    auto const newUploadPeerCount = st->peersGettingFromUs + st->webseedsSendingToUs;
    auto const newError = st->error;

    /* updating the model triggers off resort/refresh,
       so don't do it unless something's actually changed... */
    if (newActive != oldActive || newActivity != oldActivity || newFinished != oldFinished || newPriority != oldPriority ||
        newQueuePosition != oldQueuePosition || newError != oldError || newActivePeerCount != oldActivePeerCount ||
        newDownloadPeerCount != oldDownloadPeerCount || newUploadPeerCount != oldUploadPeerCount ||
        newTrackers != oldTrackers || gtr_compare_double(newUpSpeed, oldUpSpeed, 2) != 0 ||
        gtr_compare_double(newDownSpeed, oldDownSpeed, 2) != 0 ||
        gtr_compare_double(newRecheckProgress, oldRecheckProgress, 2) != 0)
    {
        row[torrent_cols.active] = newActive;
        row[torrent_cols.active_peer_count] = newActivePeerCount;
        row[torrent_cols.active_peers_up] = newUploadPeerCount;
        row[torrent_cols.active_peers_down] = newDownloadPeerCount;
        row[torrent_cols.error] = newError;
        row[torrent_cols.activity] = newActivity;
        row[torrent_cols.finished] = newFinished;
        row[torrent_cols.priority] = newPriority;
        row[torrent_cols.queue_position] = newQueuePosition;
        row[torrent_cols.trackers] = newTrackers;
        row[torrent_cols.speed_up] = newUpSpeed;
        row[torrent_cols.speed_down] = newDownSpeed;
        row[torrent_cols.recheck_progress] = newRecheckProgress;
    }
}

} // namespace

void Session::update()
{
    impl_->update();
}

void Session::start_now(tr_torrent_id_t id)
{
    tr_variant top;
    tr_variantInitDict(&top, 2);
    tr_variantDictAddStrView(&top, TR_KEY_method, "torrent-start-now");

    auto* args = tr_variantDictAddDict(&top, TR_KEY_arguments, 1);
    auto* ids = tr_variantDictAddList(args, TR_KEY_ids, 1);
    tr_variantListAddInt(ids, id);
    exec(&top);
    tr_variantClear(&top);
}

void Session::Impl::update()
{
    /* update the model */
    for (auto row : raw_model_->children())
    {
        update_foreach(row);
    }

    /* update hibernation */
    maybe_inhibit_hibernation();
}

/**
***  Hibernate
**/

namespace
{

auto const SessionManagerServiceName = "org.gnome.SessionManager"sv; // TODO(C++20): Use ""s
auto const SessionManagerInterface = "org.gnome.SessionManager"sv; // TODO(C++20): Use ""s
auto const SessionManagerObjectPath = "/org/gnome/SessionManager"sv; // TODO(C++20): Use ""s

bool gtr_inhibit_hibernation(guint32& cookie)
{
    bool success = false;
    char const* application = "Transmission BitTorrent Client";
    char const* reason = "BitTorrent Activity";
    int const toplevel_xid = 0;
    int const flags = 4; /* Inhibit suspending the session or computer */

    try
    {
        auto const connection = Gio::DBus::Connection::get_sync(TR_GIO_DBUS_BUS_TYPE(SESSION));

        auto response = connection->call_sync(
            std::string(SessionManagerObjectPath),
            std::string(SessionManagerInterface),
            "Inhibit",
            Glib::VariantContainerBase::create_tuple({
                Glib::Variant<Glib::ustring>::create(application),
                Glib::Variant<guint32>::create(toplevel_xid),
                Glib::Variant<Glib::ustring>::create(reason),
                Glib::Variant<guint32>::create(flags),
            }),
            std::string(SessionManagerServiceName),
            1000);

        cookie = Glib::VariantBase::cast_dynamic<Glib::Variant<guint32>>(response.get_child(0)).get();

        /* logging */
        tr_logAddInfo(_("Inhibiting desktop hibernation"));

        success = true;
    }
    catch (Glib::Error const& e)
    {
        tr_logAddError(fmt::format(_("Couldn't inhibit desktop hibernation: {error}"), fmt::arg("error", e.what())));
    }

    return success;
}

void gtr_uninhibit_hibernation(guint inhibit_cookie)
{
    try
    {
        auto const connection = Gio::DBus::Connection::get_sync(TR_GIO_DBUS_BUS_TYPE(SESSION));

        connection->call_sync(
            std::string(SessionManagerObjectPath),
            std::string(SessionManagerInterface),
            "Uninhibit",
            Glib::VariantContainerBase::create_tuple({ Glib::Variant<guint32>::create(inhibit_cookie) }),
            std::string(SessionManagerServiceName),
            1000);

        /* logging */
        tr_logAddInfo(_("Allowing desktop hibernation"));
    }
    catch (Glib::Error const& e)
    {
        tr_logAddError(fmt::format(_("Couldn't inhibit desktop hibernation: {error}"), fmt::arg("error", e.what())));
    }
}

} // namespace

void Session::Impl::set_hibernation_allowed(bool allowed)
{
    inhibit_allowed_ = allowed;

    if (allowed && have_inhibit_cookie_)
    {
        gtr_uninhibit_hibernation(inhibit_cookie_);
        have_inhibit_cookie_ = false;
    }

    if (!allowed && !have_inhibit_cookie_ && !dbus_error_)
    {
        if (gtr_inhibit_hibernation(inhibit_cookie_))
        {
            have_inhibit_cookie_ = true;
        }
        else
        {
            dbus_error_ = true;
        }
    }
}

void Session::Impl::maybe_inhibit_hibernation()
{
    /* hibernation is allowed if EITHER
     * (a) the "inhibit" pref is turned off OR
     * (b) there aren't any active torrents */
    bool const hibernation_allowed = !gtr_pref_flag_get(TR_KEY_inhibit_desktop_hibernation) || get_active_torrent_count() == 0;
    set_hibernation_allowed(hibernation_allowed);
}

/**
***  Prefs
**/

void Session::Impl::commit_prefs_change(tr_quark const key)
{
    signal_prefs_changed_.emit(key);
    gtr_pref_save(session_);
}

void Session::set_pref(tr_quark const key, std::string const& newval)
{
    if (newval != gtr_pref_string_get(key))
    {
        gtr_pref_string_set(key, newval);
        impl_->commit_prefs_change(key);
    }
}

void Session::set_pref(tr_quark const key, bool newval)
{
    if (newval != gtr_pref_flag_get(key))
    {
        gtr_pref_flag_set(key, newval);
        impl_->commit_prefs_change(key);
    }
}

void Session::set_pref(tr_quark const key, int newval)
{
    if (newval != gtr_pref_int_get(key))
    {
        gtr_pref_int_set(key, newval);
        impl_->commit_prefs_change(key);
    }
}

void Session::set_pref(tr_quark const key, double newval)
{
    if (gtr_compare_double(newval, gtr_pref_double_get(key), 4) != 0)
    {
        gtr_pref_double_set(key, newval);
        impl_->commit_prefs_change(key);
    }
}

/***
****
****  RPC Interface
****
***/

/* #define DEBUG_RPC */

namespace
{

int64_t nextTag = 1;

std::map<int64_t, std::function<void(tr_variant&)>> pendingRequests;

bool core_read_rpc_response_idle(tr_variant& response)
{
    if (int64_t tag = 0; tr_variantDictFindInt(&response, TR_KEY_tag, &tag))
    {
        if (auto const data_it = pendingRequests.find(tag); data_it != pendingRequests.end())
        {
            if (auto const& response_func = data_it->second; response_func)
            {
                response_func(response);
            }

            pendingRequests.erase(data_it);
        }
        else
        {
            gtr_warning(fmt::format(_("Couldn't find pending RPC request for tag {tag}"), fmt::arg("tag", tag)));
        }
    }

    return false;
}

void core_read_rpc_response(tr_session* /*session*/, tr_variant* response, gpointer /*user_data*/)
{
    Glib::signal_idle().connect([owned_response = std::shared_ptr(create_variant(*response))]() mutable
                                { return core_read_rpc_response_idle(*owned_response); });
}

} // namespace

void Session::Impl::send_rpc_request(
    tr_variant const* request,
    int64_t tag,
    std::function<void(tr_variant&)> const& response_func)
{
    if (session_ == nullptr)
    {
        gtr_error("GTK+ client doesn't support connections to remote servers yet.");
    }
    else
    {
        /* remember this request */
        pendingRequests.try_emplace(tag, response_func);

        /* make the request */
#ifdef DEBUG_RPC
        gtr_message(fmt::format("request: [{}]", tr_variantToStr(request, TR_VARIANT_FMT_JSON_LEAN)));
#endif

        tr_rpc_request_exec_json(session_, request, core_read_rpc_response, nullptr);
    }
}

/***
****  Sending a test-port request via RPC
***/

void Session::port_test()
{
    auto const tag = nextTag;
    ++nextTag;

    tr_variant request;
    tr_variantInitDict(&request, 2);
    tr_variantDictAddStrView(&request, TR_KEY_method, "port-test");
    tr_variantDictAddInt(&request, TR_KEY_tag, tag);
    impl_->send_rpc_request(
        &request,
        tag,
        [this](auto& response)
        {
            tr_variant* args = nullptr;
            bool is_open = false;

            if (!tr_variantDictFindDict(&response, TR_KEY_arguments, &args) ||
                !tr_variantDictFindBool(args, TR_KEY_port_is_open, &is_open))
            {
                is_open = false;
            }

            impl_->signal_port_tested().emit(is_open);
        });
    tr_variantClear(&request);
}

/***
****  Updating a blocklist via RPC
***/

void Session::blocklist_update()
{
    auto const tag = nextTag;
    ++nextTag;

    tr_variant request;
    tr_variantInitDict(&request, 2);
    tr_variantDictAddStrView(&request, TR_KEY_method, "blocklist-update");
    tr_variantDictAddInt(&request, TR_KEY_tag, tag);
    impl_->send_rpc_request(
        &request,
        tag,
        [this](auto& response)
        {
            tr_variant* args = nullptr;
            int64_t ruleCount = 0;

            if (!tr_variantDictFindDict(&response, TR_KEY_arguments, &args) ||
                !tr_variantDictFindInt(args, TR_KEY_blocklist_size, &ruleCount))
            {
                ruleCount = -1;
            }

            if (ruleCount > 0)
            {
                gtr_pref_int_set(TR_KEY_blocklist_date, tr_time());
            }

            impl_->signal_blocklist_updated().emit(ruleCount);
        });
    tr_variantClear(&request);
}

/***
****
***/

void Session::exec(tr_variant const* request)
{
    auto const tag = nextTag;
    ++nextTag;

    impl_->send_rpc_request(request, tag, {});
}

/***
****
***/

size_t Session::get_torrent_count() const
{
    return impl_->get_raw_model()->children().size();
}

size_t Session::get_active_torrent_count() const
{
    return impl_->get_active_torrent_count();
}

size_t Session::Impl::get_active_torrent_count() const
{
    size_t activeCount = 0;

    for (auto const& row : raw_model_->children())
    {
        if (row.get_value(torrent_cols.activity) != TR_STATUS_STOPPED)
        {
            ++activeCount;
        }
    }

    return activeCount;
}

tr_torrent* Session::find_torrent(tr_torrent_id_t id) const
{
    tr_torrent* tor = nullptr;

    if (auto* const session = impl_->get_session(); session != nullptr)
    {
        tor = tr_torrentFindFromId(session, id);
    }

    return tor;
}

void Session::open_folder(tr_torrent_id_t torrent_id) const
{
    auto const* tor = find_torrent(torrent_id);

    if (tor != nullptr)
    {
        bool const single = tr_torrentFileCount(tor) == 1;
        char const* currentDir = tr_torrentGetCurrentDir(tor);

        if (single)
        {
            gtr_open_file(currentDir);
        }
        else
        {
            gtr_open_file(Glib::build_filename(currentDir, tr_torrentName(tor)));
        }
    }
}

sigc::signal<void(Session::ErrorCode, Glib::ustring const&)>& Session::signal_add_error()
{
    return impl_->signal_add_error();
}

sigc::signal<void(tr_ctor*)>& Session::signal_add_prompt()
{
    return impl_->signal_add_prompt();
}

sigc::signal<void(int)>& Session::signal_blocklist_updated()
{
    return impl_->signal_blocklist_updated();
}

sigc::signal<void(bool)>& Session::signal_busy()
{
    return impl_->signal_busy();
}

sigc::signal<void(tr_quark)>& Session::signal_prefs_changed()
{
    return impl_->signal_prefs_changed();
}

sigc::signal<void(bool)>& Session::signal_port_tested()
{
    return impl_->signal_port_tested();
}
