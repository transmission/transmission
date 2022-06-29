// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <limits.h> /* INT_MAX */
#include <numeric>
#include <sstream>
#include <stddef.h>
#include <stdio.h> /* sscanf() */
#include <stdlib.h> /* abort() */
#include <string>
#include <string_view>
#include <unordered_map>

#include <glibmm/i18n.h>

#include <fmt/core.h>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_free */
#include <libtransmission/web-utils.h>

#include "Actions.h"
#include "DetailsDialog.h"
#include "FaviconCache.h" /* gtr_get_favicon() */
#include "FileList.h"
#include "HigWorkarea.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

using namespace std::literals;

class DetailsDialog::Impl
{
public:
    Impl(DetailsDialog& dialog, Glib::RefPtr<Session> const& core);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

    void set_torrents(std::vector<tr_torrent_id_t> const& torrent_ids);

private:
    Gtk::Widget* info_page_new();
    Gtk::Widget* peer_page_new();
    Gtk::Widget* tracker_page_new();
    Gtk::Widget* options_page_new();

    void on_details_window_size_allocated(Gtk::Allocation& alloc);

    bool onPeerViewQueryTooltip(int x, int y, bool keyboard_tip, Glib::RefPtr<Gtk::Tooltip> const& tooltip);
    void onMorePeerInfoToggled();

    bool trackerVisibleFunc(Gtk::TreeModel::const_iterator const& iter);
    void on_tracker_list_selection_changed();

    void on_tracker_list_add_button_clicked();
    void on_edit_trackers();
    void on_tracker_list_remove_button_clicked();
    void onScrapeToggled();
    void onBackupToggled();

    void on_add_tracker_response(int response, std::shared_ptr<Gtk::Dialog>& dialog);
    void on_edit_trackers_response(int response, std::shared_ptr<Gtk::Dialog>& dialog);

    void torrent_set_bool(tr_quark key, bool value);
    void torrent_set_int(tr_quark key, int value);
    void torrent_set_real(tr_quark key, double value);

    void refresh();

    void refreshInfo(std::vector<tr_torrent*> const& torrents);
    void refreshPeers(std::vector<tr_torrent*> const& torrents);
    void refreshTracker(std::vector<tr_torrent*> const& torrents);
    void refreshOptions(std::vector<tr_torrent*> const& torrents);

    void refreshPeerList(std::vector<tr_torrent*> const& torrents);
    void refreshWebseedList(std::vector<tr_torrent*> const& torrents);

    tr_torrent_id_t tracker_list_get_current_torrent_id() const;
    tr_torrent* tracker_list_get_current_torrent() const;

    std::vector<tr_torrent*> getTorrents() const;

private:
    DetailsDialog& dialog_;

    Gtk::CheckButton* honor_limits_check_ = nullptr;
    Gtk::CheckButton* up_limited_check_ = nullptr;
    Gtk::SpinButton* up_limit_sping_ = nullptr;
    Gtk::CheckButton* down_limited_check_ = nullptr;
    Gtk::SpinButton* down_limit_spin_ = nullptr;
    Gtk::ComboBox* bandwidth_combo_ = nullptr;

    Gtk::ComboBox* ratio_combo_ = nullptr;
    Gtk::SpinButton* ratio_spin_ = nullptr;
    Gtk::ComboBox* idle_combo_ = nullptr;
    Gtk::SpinButton* idle_spin_ = nullptr;
    Gtk::SpinButton* max_peers_spin_ = nullptr;

    sigc::connection honor_limits_check_tag_;
    sigc::connection up_limited_check_tag_;
    sigc::connection down_limited_check_tag_;
    sigc::connection down_limit_spin_tag_;
    sigc::connection up_limit_spin_tag_;
    sigc::connection bandwidth_combo_tag_;
    sigc::connection ratio_combo_tag_;
    sigc::connection ratio_spin_tag_;
    sigc::connection idle_combo_tag_;
    sigc::connection idle_spin_tag_;
    sigc::connection max_peers_spin_tag_;

    Gtk::Label* added_lb_ = nullptr;
    Gtk::Label* size_lb_ = nullptr;
    Gtk::Label* state_lb_ = nullptr;
    Gtk::Label* have_lb_ = nullptr;
    Gtk::Label* dl_lb_ = nullptr;
    Gtk::Label* ul_lb_ = nullptr;
    Gtk::Label* error_lb_ = nullptr;
    Gtk::Label* date_started_lb_ = nullptr;
    Gtk::Label* eta_lb_ = nullptr;
    Gtk::Label* last_activity_lb_ = nullptr;

    Gtk::Label* hash_lb_ = nullptr;
    Gtk::Label* privacy_lb_ = nullptr;
    Gtk::Label* origin_lb_ = nullptr;
    Gtk::Label* destination_lb_ = nullptr;
    Glib::RefPtr<Gtk::TextBuffer> comment_buffer_;

    std::unordered_map<std::string, Gtk::TreeRowReference> peer_hash_;
    std::unordered_map<std::string, Gtk::TreeRowReference> webseed_hash_;
    Glib::RefPtr<Gtk::ListStore> peer_store_;
    Glib::RefPtr<Gtk::ListStore> webseed_store_;
    Gtk::ScrolledWindow* webseed_view_ = nullptr;
    Gtk::TreeView* peer_view_ = nullptr;
    Gtk::CheckButton* more_peer_details_check_ = nullptr;

    Glib::RefPtr<Gtk::ListStore> tracker_store_;
    std::unordered_map<std::string, Gtk::TreeRowReference> tracker_hash_;
    Glib::RefPtr<Gtk::TreeModelFilter> trackers_filtered_;
    Gtk::Button* add_tracker_button_ = nullptr;
    Gtk::Button* edit_trackers_button_ = nullptr;
    Gtk::Button* remove_tracker_button_ = nullptr;
    Gtk::TreeView* tracker_view_ = nullptr;
    Gtk::CheckButton* scrape_check_ = nullptr;
    Gtk::CheckButton* all_check_ = nullptr;

    FileList* file_list_ = nullptr;
    Gtk::Label* file_label_ = nullptr;

    std::vector<tr_torrent_id_t> ids_;
    Glib::RefPtr<Session> const core_;
    sigc::connection periodic_refresh_tag_;

    Glib::Quark const TORRENT_ID_KEY = Glib::Quark("tr-torrent-id-key");
    Glib::Quark const TEXT_BUFFER_KEY = Glib::Quark("tr-text-buffer-key");
    Glib::Quark const URL_ENTRY_KEY = Glib::Quark("tr-url-entry-key");

    static guint last_page_;
    sigc::connection last_page_tag_;
};

guint DetailsDialog::Impl::last_page_ = 0;

std::vector<tr_torrent*> DetailsDialog::Impl::getTorrents() const
{
    std::vector<tr_torrent*> torrents;
    torrents.reserve(ids_.size());

    for (auto const id : ids_)
    {
        if (auto* torrent = core_->find_torrent(id); torrent != nullptr)
        {
            torrents.push_back(torrent);
        }
    }

    return torrents;
}

/****
*****
*****  OPTIONS TAB
*****
****/

namespace
{

void set_togglebutton_if_different(Gtk::ToggleButton* toggle, sigc::connection& tag, bool value)
{
    bool const currentValue = toggle->get_active();

    if (currentValue != value)
    {
        tag.block();
        toggle->set_active(value);
        tag.unblock();
    }
}

void set_int_spin_if_different(Gtk::SpinButton* spin, sigc::connection& tag, int value)
{
    int const currentValue = spin->get_value_as_int();

    if (currentValue != value)
    {
        tag.block();
        spin->set_value(value);
        tag.unblock();
    }
}

void set_double_spin_if_different(Gtk::SpinButton* spin, sigc::connection& tag, double value)
{
    double const currentValue = spin->get_value();

    if ((int)(currentValue * 100) != (int)(value * 100))
    {
        tag.block();
        spin->set_value(value);
        tag.unblock();
    }
}

void unset_combo(Gtk::ComboBox* combobox, sigc::connection& tag)
{
    tag.block();
    combobox->set_active(-1);
    tag.unblock();
}

} // namespace

void DetailsDialog::Impl::refreshOptions(std::vector<tr_torrent*> const& torrents)
{
    /***
    ****  Options Page
    ***/

    /* honor_limits_check */
    if (!torrents.empty())
    {
        bool const baseline = tr_torrentUsesSessionLimits(torrents.front());
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSessionLimits(torrent); });

        if (is_uniform)
        {
            set_togglebutton_if_different(honor_limits_check_, honor_limits_check_tag_, baseline);
        }
    }

    /* down_limited_check */
    if (!torrents.empty())
    {
        bool const baseline = tr_torrentUsesSpeedLimit(torrents.front(), TR_DOWN);
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSpeedLimit(torrent, TR_DOWN); });

        if (is_uniform)
        {
            set_togglebutton_if_different(down_limited_check_, down_limited_check_tag_, baseline);
        }
    }

    /* down_limit_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetSpeedLimit_KBps(torrents.front(), TR_DOWN);
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetSpeedLimit_KBps(torrent, TR_DOWN); });

        if (is_uniform)
        {
            set_int_spin_if_different(down_limit_spin_, down_limit_spin_tag_, baseline);
        }
    }

    /* up_limited_check */
    if (!torrents.empty())
    {
        bool const baseline = tr_torrentUsesSpeedLimit(torrents.front(), TR_UP);
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSpeedLimit(torrent, TR_UP); });

        if (is_uniform)
        {
            set_togglebutton_if_different(up_limited_check_, up_limited_check_tag_, baseline);
        }
    }

    /* up_limit_sping */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetSpeedLimit_KBps(torrents.front(), TR_UP);
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetSpeedLimit_KBps(torrent, TR_UP); });

        if (is_uniform)
        {
            set_int_spin_if_different(up_limit_sping_, up_limit_spin_tag_, baseline);
        }
    }

    /* bandwidth_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetPriority(torrents.front());
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetPriority(torrent); });

        if (is_uniform)
        {
            bandwidth_combo_tag_.block();
            gtr_priority_combo_set_value(*bandwidth_combo_, baseline);
            bandwidth_combo_tag_.unblock();
        }
        else
        {
            unset_combo(bandwidth_combo_, bandwidth_combo_tag_);
        }
    }

    /* ratio_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetRatioMode(torrents.front());
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetRatioMode(torrent); });

        if (is_uniform)
        {
            ratio_combo_tag_.block();
            gtr_combo_box_set_active_enum(*ratio_combo_, baseline);
            gtr_widget_set_visible(*ratio_spin_, baseline == TR_RATIOLIMIT_SINGLE);
            ratio_combo_tag_.unblock();
        }
    }

    /* ratio_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetRatioLimit(torrents.front());
        set_double_spin_if_different(ratio_spin_, ratio_spin_tag_, baseline);
    }

    /* idle_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetIdleMode(torrents.front());
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetIdleMode(torrent); });

        if (is_uniform)
        {
            idle_combo_tag_.block();
            gtr_combo_box_set_active_enum(*idle_combo_, baseline);
            gtr_widget_set_visible(*idle_spin_, baseline == TR_IDLELIMIT_SINGLE);
            idle_combo_tag_.unblock();
        }
    }

    /* idle_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetIdleLimit(torrents.front());
        set_int_spin_if_different(idle_spin_, idle_spin_tag_, baseline);
    }

    /* max_peers_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetPeerLimit(torrents.front());
        set_int_spin_if_different(max_peers_spin_, max_peers_spin_tag_, baseline);
    }
}

void DetailsDialog::Impl::torrent_set_bool(tr_quark key, bool value)
{
    tr_variant top;

    tr_variantInitDict(&top, 2);
    tr_variantDictAddStrView(&top, TR_KEY_method, "torrent-set"sv);
    tr_variant* const args = tr_variantDictAddDict(&top, TR_KEY_arguments, 2);
    tr_variantDictAddBool(args, key, value);
    tr_variant* const ids = tr_variantDictAddList(args, TR_KEY_ids, ids_.size());

    for (auto const id : ids_)
    {
        tr_variantListAddInt(ids, id);
    }

    core_->exec(&top);
    tr_variantFree(&top);
}

void DetailsDialog::Impl::torrent_set_int(tr_quark key, int value)
{
    tr_variant top;

    tr_variantInitDict(&top, 2);
    tr_variantDictAddStrView(&top, TR_KEY_method, "torrent-set"sv);
    tr_variant* const args = tr_variantDictAddDict(&top, TR_KEY_arguments, 2);
    tr_variantDictAddInt(args, key, value);
    tr_variant* const ids = tr_variantDictAddList(args, TR_KEY_ids, ids_.size());

    for (auto const id : ids_)
    {
        tr_variantListAddInt(ids, id);
    }

    core_->exec(&top);
    tr_variantFree(&top);
}

void DetailsDialog::Impl::torrent_set_real(tr_quark key, double value)
{
    tr_variant top;

    tr_variantInitDict(&top, 2);
    tr_variantDictAddStrView(&top, TR_KEY_method, "torrent-set"sv);
    tr_variant* const args = tr_variantDictAddDict(&top, TR_KEY_arguments, 2);
    tr_variantDictAddReal(args, key, value);
    tr_variant* const ids = tr_variantDictAddList(args, TR_KEY_ids, ids_.size());

    for (auto const id : ids_)
    {
        tr_variantListAddInt(ids, id);
    }

    core_->exec(&top);
    tr_variantFree(&top);
}

Gtk::Widget* DetailsDialog::Impl::options_page_new()
{
    guint row;

    row = 0;
    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Speed"));

    honor_limits_check_ = t->add_wide_checkbutton(row, _("Honor global _limits"), false);
    honor_limits_check_tag_ = honor_limits_check_->signal_toggled().connect(
        [this]() { torrent_set_bool(TR_KEY_honorsSessionLimits, honor_limits_check_->get_active()); });

    down_limited_check_ = Gtk::make_managed<Gtk::CheckButton>(
        fmt::format(_("Limit _download speed ({speed_units}):"), fmt::arg("speed_units", speed_K_str)),
        true);
    down_limited_check_->set_active(false);
    down_limited_check_tag_ = down_limited_check_->signal_toggled().connect(
        [this]() { torrent_set_bool(TR_KEY_downloadLimited, down_limited_check_->get_active()); });

    down_limit_spin_ = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(0, 0, INT_MAX, 5));
    down_limit_spin_tag_ = down_limit_spin_->signal_value_changed().connect(
        [this]() { torrent_set_int(TR_KEY_downloadLimit, down_limit_spin_->get_value_as_int()); });
    t->add_row_w(row, *down_limited_check_, *down_limit_spin_);

    up_limited_check_ = Gtk::make_managed<Gtk::CheckButton>(
        fmt::format(_("Limit _upload speed ({speed_units}):"), fmt::arg("speed_units", speed_K_str)),
        true);
    up_limited_check_tag_ = up_limited_check_->signal_toggled().connect(
        [this]() { torrent_set_bool(TR_KEY_uploadLimited, up_limited_check_->get_active()); });

    up_limit_sping_ = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(0, 0, INT_MAX, 5));
    up_limit_spin_tag_ = up_limit_sping_->signal_value_changed().connect(
        [this]() { torrent_set_int(TR_KEY_uploadLimit, up_limit_sping_->get_value_as_int()); });
    t->add_row_w(row, *up_limited_check_, *up_limit_sping_);

    bandwidth_combo_ = gtr_priority_combo_new();
    bandwidth_combo_tag_ = bandwidth_combo_->signal_changed().connect(
        [this]() { torrent_set_int(TR_KEY_bandwidthPriority, gtr_priority_combo_get_value(*bandwidth_combo_)); });
    t->add_row(row, _("Torrent _priority:"), *bandwidth_combo_);

    t->add_section_divider(row);
    t->add_section_title(row, _("Seeding Limits"));

    auto* h1 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD);
    ratio_combo_ = gtr_combo_box_new_enum({
        { _("Use global settings"), TR_RATIOLIMIT_GLOBAL },
        { _("Seed regardless of ratio"), TR_RATIOLIMIT_UNLIMITED },
        { _("Stop seeding at ratio:"), TR_RATIOLIMIT_SINGLE },
    });
    ratio_combo_tag_ = ratio_combo_->signal_changed().connect(
        [this]()
        {
            torrent_set_int(TR_KEY_seedRatioMode, gtr_combo_box_get_active_enum(*ratio_combo_));
            refresh();
        });
    h1->pack_start(*ratio_combo_, true, true, 0);
    ratio_spin_ = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(0, 0, 1000, .05));
    ratio_spin_->set_width_chars(7);
    ratio_spin_tag_ = ratio_spin_->signal_value_changed().connect(
        [this]() { torrent_set_real(TR_KEY_seedRatioLimit, ratio_spin_->get_value()); });
    h1->pack_start(*ratio_spin_, false, false, 0);
    t->add_row(row, _("_Ratio:"), *h1);

    auto* h2 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD);
    idle_combo_ = gtr_combo_box_new_enum({
        { _("Use global settings"), TR_IDLELIMIT_GLOBAL },
        { _("Seed regardless of activity"), TR_IDLELIMIT_UNLIMITED },
        { _("Stop seeding if idle for N minutes:"), TR_IDLELIMIT_SINGLE },
    });
    idle_combo_tag_ = idle_combo_->signal_changed().connect(
        [this]()
        {
            torrent_set_int(TR_KEY_seedIdleMode, gtr_combo_box_get_active_enum(*idle_combo_));
            refresh();
        });
    h2->pack_start(*idle_combo_, true, true, 0);
    idle_spin_ = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(1, 1, 40320, 5));
    idle_spin_tag_ = idle_spin_->signal_value_changed().connect(
        [this]() { torrent_set_int(TR_KEY_seedIdleLimit, idle_spin_->get_value_as_int()); });
    h2->pack_start(*idle_spin_, false, false, 0);
    t->add_row(row, _("_Idle:"), *h2);

    t->add_section_divider(row);
    t->add_section_title(row, _("Peer Connections"));

    max_peers_spin_ = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(1, 1, 3000, 5));
    t->add_row(row, _("_Maximum peers:"), *max_peers_spin_, max_peers_spin_);
    max_peers_spin_tag_ = max_peers_spin_->signal_value_changed().connect(
        [this]() { torrent_set_int(TR_KEY_peer_limit, max_peers_spin_->get_value_as_int()); });

    return t;
}

/****
*****
*****  INFO TAB
*****
****/

namespace
{

Glib::ustring activityString(int activity, bool finished)
{
    switch (activity)
    {
    case TR_STATUS_CHECK_WAIT:
        return _("Queued for verification");

    case TR_STATUS_CHECK:
        return _("Verifying local data");

    case TR_STATUS_DOWNLOAD_WAIT:
        return _("Queued for download");

    case TR_STATUS_DOWNLOAD:
        return C_("Verb", "Downloading");

    case TR_STATUS_SEED_WAIT:
        return _("Queued for seeding");

    case TR_STATUS_SEED:
        return C_("Verb", "Seeding");

    case TR_STATUS_STOPPED:
        return finished ? _("Finished") : _("Paused");

    default:
        g_assert_not_reached();
    }

    return {};
}

/* Only call gtk_text_buffer_set_text () if the new text differs from the old.
 * This way if the user has text selected, refreshing won't deselect it */
void gtr_text_buffer_set_text(Glib::RefPtr<Gtk::TextBuffer> const& b, Glib::ustring const& str)
{
    if (b->get_text() != str)
    {
        b->set_text(str);
    }
}

[[nodiscard]] std::string get_date_string(time_t t)
{
    return t == 0 ? _("N/A") : fmt::format(FMT_STRING("{:%x}"), fmt::localtime(t));
}

[[nodiscard]] std::string get_date_time_string(time_t t)
{
    return t == 0 ? _("N/A") : fmt::format(FMT_STRING("{:%c}"), fmt::localtime(t));
}

} // namespace

void DetailsDialog::Impl::refreshInfo(std::vector<tr_torrent*> const& torrents)
{
    auto const now = time(nullptr);
    Glib::ustring str;
    Glib::ustring const mixed = _("Mixed");
    Glib::ustring const no_torrent = _("No Torrents Selected");
    Glib::ustring stateString;
    uint64_t sizeWhenDone = 0;
    std::vector<tr_stat const*> stats;
    std::vector<tr_torrent_view> infos;

    stats.reserve(torrents.size());
    infos.reserve(torrents.size());
    for (auto* const torrent : torrents)
    {
        stats.push_back(tr_torrentStatCached(torrent));
        infos.push_back(tr_torrentView(torrent));
    }

    /* privacy_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else
    {
        bool const baseline = infos.front().is_private;
        bool const is_uniform = std::all_of(
            infos.begin(),
            infos.end(),
            [baseline](auto const& info) { return info.is_private == baseline; });

        if (is_uniform)
        {
            str = baseline ? _("Private to this tracker -- DHT and PEX disabled") : _("Public torrent");
        }
        else
        {
            str = mixed;
        }
    }

    privacy_lb_->set_text(str);

    /* added_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const baseline = stats.front()->addedDate;
        bool const is_uniform = std::all_of(
            stats.begin(),
            stats.end(),
            [baseline](auto const* stat) { return stat->addedDate == baseline; });

        if (is_uniform)
        {
            str = get_date_time_string(baseline);
        }
        else
        {
            str = mixed;
        }
    }

    added_lb_->set_text(str);

    /* origin_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const creator = tr_strvStrip(infos.front().creator != nullptr ? infos.front().creator : "");
        auto const date = infos.front().date_created;
        auto const datestr = get_date_string(date);
        bool const mixed_creator = std::any_of(
            infos.begin(),
            infos.end(),
            [&creator](auto const& info) { return creator != (info.creator != nullptr ? info.creator : ""); });
        bool const mixed_date = std::any_of(
            infos.begin(),
            infos.end(),
            [date](auto const& info) { return date != info.date_created; });

        bool const empty_creator = std::empty(creator);
        bool const empty_date = date == 0;

        if (mixed_creator || mixed_date)
        {
            str = mixed;
        }
        else if (!empty_creator && !empty_date)
        {
            str = fmt::format(_("Created by {creator} on {date}"), fmt::arg("creator", creator), fmt::arg("date", datestr));
        }
        else if (!empty_creator)
        {
            str = fmt::format(_("Created by {creator}"), fmt::arg("creator", creator));
        }
        else if (!empty_date)
        {
            str = fmt::format(_("Created on {date}"), fmt::arg("date", datestr));
        }
        else
        {
            str = _("N/A");
        }
    }

    origin_lb_->set_text(str);

    /* comment_buffer */
    if (infos.empty())
    {
        str.clear();
    }
    else
    {
        auto const baseline = Glib::ustring(infos.front().comment != nullptr ? infos.front().comment : "");
        bool const is_uniform = std::all_of(
            infos.begin(),
            infos.end(),
            [&baseline](auto const& info) { return baseline == (info.comment != nullptr ? info.comment : ""); });

        str = is_uniform ? baseline : mixed;
    }

    gtr_text_buffer_set_text(comment_buffer_, str);

    /* destination_lb */
    if (torrents.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const baseline = Glib::ustring(tr_torrentGetDownloadDir(torrents.front()));
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [&baseline](auto const* torrent) { return baseline == tr_torrentGetDownloadDir(torrent); });

        str = is_uniform ? baseline : mixed;
    }

    destination_lb_->set_text(str);

    /* state_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const activity = stats.front()->activity;
        bool const is_uniform = std::all_of(
            stats.begin(),
            stats.end(),
            [activity](auto const* st) { return activity == st->activity; });
        bool const allFinished = std::all_of(stats.begin(), stats.end(), [](auto const* st) { return st->finished; });

        str = is_uniform ? activityString(activity, allFinished) : mixed;
    }

    stateString = str;
    state_lb_->set_text(str);

    /* date started */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        time_t const baseline = stats.front()->startDate;
        bool const is_uniform = std::all_of(
            stats.begin(),
            stats.end(),
            [baseline](auto const* st) { return baseline == st->startDate; });

        if (!is_uniform)
        {
            str = mixed;
        }
        else if (baseline <= 0 || stats[0]->activity == TR_STATUS_STOPPED)
        {
            str = stateString;
        }
        else
        {
            str = tr_format_time_relative(now, baseline);
        }
    }

    date_started_lb_->set_text(str);

    /* eta */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        int const baseline = stats.front()->eta;
        bool const is_uniform = std::all_of(
            stats.begin(),
            stats.end(),
            [baseline](auto const* st) { return baseline == st->eta; });

        if (!is_uniform)
        {
            str = mixed;
        }
        else if (baseline < 0)
        {
            str = _("Unknown");
        }
        else
        {
            str = tr_format_time_relative(now, baseline);
        }
    }

    eta_lb_->set_text(str);

    /* size_lb */
    {
        auto const piece_count = std::accumulate(
            std::begin(infos),
            std::end(infos),
            uint64_t{},
            [](auto sum, auto const& info) { return sum + info.n_pieces; });

        if (piece_count == 0)
        {
            str.clear();
        }
        else
        {
            auto const total_size = std::accumulate(
                std::begin(infos),
                std::end(infos),
                uint64_t{},
                [](auto sum, auto const& info) { return sum + info.total_size; });

            auto const file_count = std::accumulate(
                std::begin(torrents),
                std::end(torrents),
                std::size_t{},
                [](auto sum, auto const* tor) { return sum + tr_torrentFileCount(tor); });

            str = fmt::format(
                ngettext("{total_size} in {file_count:L} file", "{total_size} in {file_count:L} files", file_count),
                fmt::arg("total_size", tr_strlsize(total_size)),
                fmt::arg("file_count", file_count));

            auto const piece_size = std::empty(infos) ? uint32_t{} : infos.front().piece_size;
            auto const piece_size_is_uniform = std::all_of(
                std::begin(infos),
                std::end(infos),
                [piece_size](auto const& info) { return info.piece_size == piece_size; });

            if (piece_size_is_uniform)
            {
                str += ' ';
                str += fmt::format(
                    ngettext(
                        "({piece_count} BitTorrent piece @ {piece_size})",
                        "({piece_count} BitTorrent pieces @ {piece_size})",
                        piece_count),
                    fmt::arg("piece_count", piece_count),
                    fmt::arg("piece_size", tr_formatter_mem_B(piece_size)));
            }
        }

        size_lb_->set_text(str);
    }

    /* have_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        uint64_t leftUntilDone = 0;
        uint64_t haveUnchecked = 0;
        uint64_t haveValid = 0;
        uint64_t available = 0;

        for (auto const* const st : stats)
        {
            haveUnchecked += st->haveUnchecked;
            haveValid += st->haveValid;
            sizeWhenDone += st->sizeWhenDone;
            leftUntilDone += st->leftUntilDone;
            available += st->sizeWhenDone - st->leftUntilDone + st->haveUnchecked + st->desiredAvailable;
        }

        {
            double const d = sizeWhenDone != 0 ? (100.0 * available) / sizeWhenDone : 0;
            double const ratio = 100.0 * (sizeWhenDone != 0 ? (haveValid + haveUnchecked) / (double)sizeWhenDone : 1);

            auto const avail = tr_strpercent(d);
            auto const buf2 = tr_strpercent(ratio);
            auto const total = tr_strlsize(haveUnchecked + haveValid);
            auto const unver = tr_strlsize(haveUnchecked);

            if (haveUnchecked == 0 && leftUntilDone == 0)
            {
                str = fmt::format(
                    _("{current_size} ({percent_done}%)"),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2));
            }
            else if (haveUnchecked == 0)
            {
                str = fmt::format(
                    // xgettext:no-c-format
                    _("{current_size} ({percent_done}% of {percent_available}% available)"),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2),
                    fmt::arg("percent_available", avail));
            }
            else
            {
                str = fmt::format(
                    // xgettext:no-c-format
                    _("{current_size} ({percent_done}% of {percent_available}% available; {unverified_size} unverified)"),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2),
                    fmt::arg("percent_available", avail),
                    fmt::arg("unverified_size", unver));
            }
        }
    }

    have_lb_->set_text(str);

    // dl_lb
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const downloaded_str = tr_strlsize(std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{ 0 },
            [](auto sum, auto const* st) { return sum + st->downloadedEver; }));

        auto const failed = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{ 0 },
            [](auto sum, auto const* st) { return sum + st->corruptEver; });

        if (failed != 0)
        {
            str = fmt::format(
                _("{downloaded_size} (+{discarded_size} discarded after failed checksum)"),
                fmt::arg("downloaded_size", downloaded_str),
                fmt::arg("discarded_size", tr_strlsize(failed)));
        }
        else
        {
            str = downloaded_str;
        }
    }

    dl_lb_->set_text(str);

    /* ul_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const uploaded = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{},
            [](auto sum, auto const* st) { return sum + st->uploadedEver; });
        auto const denominator = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{},
            [](auto sum, auto const* st) { return sum + st->sizeWhenDone; });
        str = fmt::format(
            _("{uploaded_size} (Ratio: {ratio})"),
            fmt::arg("uploaded_size", tr_strlsize(uploaded)),
            fmt::arg("ratio", tr_strlratio(tr_getRatio(uploaded, denominator))));
    }

    ul_lb_->set_text(str);

    /* hash_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else if (infos.size() == 1)
    {
        str = infos.front().hash_string;
    }
    else
    {
        str = mixed;
    }

    hash_lb_->set_text(str);

    /* error */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const baseline = Glib::ustring(stats.front()->errorString);
        bool const is_uniform = std::all_of(
            stats.begin(),
            stats.end(),
            [&baseline](auto const* st) { return baseline == st->errorString; });

        str = is_uniform ? baseline : mixed;
    }

    if (str.empty())
    {
        str = _("No errors");
    }

    error_lb_->set_text(str);

    /* activity date */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        time_t const latest = (*std::max_element(
                                   stats.begin(),
                                   stats.end(),
                                   [](auto const* lhs, auto const* rhs) { return lhs->activityDate < rhs->activityDate; }))
                                  ->activityDate;

        if (latest <= 0)
        {
            str = _("Never");
        }
        else if ((now - latest) < 5)
        {
            str = _("Active now");
        }
        else
        {
            str = tr_format_time_relative(now, latest);
        }
    }

    last_activity_lb_->set_text(str);
}

Gtk::Widget* DetailsDialog::Impl::info_page_new()
{
    guint row = 0;
    auto* t = Gtk::make_managed<HigWorkarea>();

    t->add_section_title(row, _("Activity"));

    /* size */
    size_lb_ = Gtk::make_managed<Gtk::Label>();
    size_lb_->set_single_line_mode(true);
    t->add_row(row, _("Torrent size:"), *size_lb_);

    /* have */
    have_lb_ = Gtk::make_managed<Gtk::Label>();
    have_lb_->set_single_line_mode(true);
    t->add_row(row, _("Have:"), *have_lb_);

    /* uploaded */
    ul_lb_ = Gtk::make_managed<Gtk::Label>();
    ul_lb_->set_single_line_mode(true);
    t->add_row(row, _("Uploaded:"), *ul_lb_);

    /* downloaded */
    dl_lb_ = Gtk::make_managed<Gtk::Label>();
    dl_lb_->set_single_line_mode(true);
    t->add_row(row, _("Downloaded:"), *dl_lb_);

    /* state */
    state_lb_ = Gtk::make_managed<Gtk::Label>();
    state_lb_->set_single_line_mode(true);
    t->add_row(row, _("State:"), *state_lb_);

    /* running for */
    date_started_lb_ = Gtk::make_managed<Gtk::Label>();
    date_started_lb_->set_single_line_mode(true);
    t->add_row(row, _("Running time:"), *date_started_lb_);

    /* eta */
    eta_lb_ = Gtk::make_managed<Gtk::Label>();
    eta_lb_->set_single_line_mode(true);
    t->add_row(row, _("Remaining time:"), *eta_lb_);

    /* last activity */
    last_activity_lb_ = Gtk::make_managed<Gtk::Label>();
    last_activity_lb_->set_single_line_mode(true);
    t->add_row(row, _("Last activity:"), *last_activity_lb_);

    /* error */
    error_lb_ = Gtk::make_managed<Gtk::Label>();
    error_lb_->set_selectable(true);
    error_lb_->set_ellipsize(Pango::ELLIPSIZE_END);
    error_lb_->set_line_wrap(true);
    error_lb_->set_lines(10);
    t->add_row(row, _("Error:"), *error_lb_);

    /* details */
    t->add_section_divider(row);
    t->add_section_title(row, _("Details"));

    /* destination */
    destination_lb_ = Gtk::make_managed<Gtk::Label>();
    destination_lb_->set_selectable(true);
    destination_lb_->set_ellipsize(Pango::ELLIPSIZE_END);
    t->add_row(row, _("Location:"), *destination_lb_);

    /* hash */
    hash_lb_ = Gtk::make_managed<Gtk::Label>();
    hash_lb_->set_selectable(true);
    hash_lb_->set_ellipsize(Pango::ELLIPSIZE_END);
    t->add_row(row, _("Hash:"), *hash_lb_);

    /* privacy */
    privacy_lb_ = Gtk::make_managed<Gtk::Label>();
    privacy_lb_->set_single_line_mode(true);
    t->add_row(row, _("Privacy:"), *privacy_lb_);

    /* origins */
    origin_lb_ = Gtk::make_managed<Gtk::Label>();
    origin_lb_->set_selectable(true);
    origin_lb_->set_ellipsize(Pango::ELLIPSIZE_END);
    t->add_row(row, _("Origin:"), *origin_lb_);

    /* added */
    added_lb_ = Gtk::make_managed<Gtk::Label>();
    added_lb_->set_single_line_mode(true);
    t->add_row(row, _("Added:"), *added_lb_);

    /* comment */
    comment_buffer_ = Gtk::TextBuffer::create();
    auto* tw = Gtk::make_managed<Gtk::TextView>(comment_buffer_);
    tw->set_wrap_mode(Gtk::WRAP_WORD);
    tw->set_editable(false);
    auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_size_request(350, 36);
    sw->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    sw->add(*tw);
    auto* fr = Gtk::make_managed<Gtk::Frame>();
    fr->set_shadow_type(Gtk::SHADOW_IN);
    fr->add(*sw);
    auto* w = t->add_tall_row(row, _("Comment:"), *fr);
    w->set_halign(Gtk::ALIGN_START);
    w->set_valign(Gtk::ALIGN_START);

    t->add_section_divider(row);
    return t;
}

/****
*****
*****  PEERS TAB
*****
****/

namespace
{

class WebseedModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    WebseedModelColumns()
    {
        add(key);
        add(was_updated);
        add(url);
        add(download_rate_double);
        add(download_rate_string);
    }

    Gtk::TreeModelColumn<std::string> key;
    Gtk::TreeModelColumn<bool> was_updated;
    Gtk::TreeModelColumn<Glib::ustring> url;
    Gtk::TreeModelColumn<double> download_rate_double;
    Gtk::TreeModelColumn<Glib::ustring> download_rate_string;
};

WebseedModelColumns const webseed_cols;

class PeerModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    PeerModelColumns()
    {
        add(key);
        add(was_updated);
        add(address);
        add(address_collated);
        add(download_rate_double);
        add(download_rate_string);
        add(upload_rate_double);
        add(upload_rate_string);
        add(client);
        add(progress);
        add(upload_request_count_int);
        add(upload_request_count_string);
        add(download_request_count_int);
        add(download_request_count_string);
        add(blocks_downloaded_count_int);
        add(blocks_downloaded_count_string);
        add(blocks_uploaded_count_int);
        add(blocks_uploaded_count_string);
        add(reqs_cancelled_by_client_count_int);
        add(reqs_cancelled_by_client_count_string);
        add(reqs_cancelled_by_peer_count_int);
        add(reqs_cancelled_by_peer_count_string);
        add(encryption_stock_id);
        add(flags);
        add(torrent_name);
    }

    Gtk::TreeModelColumn<std::string> key;
    Gtk::TreeModelColumn<bool> was_updated;
    Gtk::TreeModelColumn<Glib::ustring> address;
    Gtk::TreeModelColumn<Glib::ustring> address_collated;
    Gtk::TreeModelColumn<double> download_rate_double;
    Gtk::TreeModelColumn<Glib::ustring> download_rate_string;
    Gtk::TreeModelColumn<double> upload_rate_double;
    Gtk::TreeModelColumn<Glib::ustring> upload_rate_string;
    Gtk::TreeModelColumn<Glib::ustring> client;
    Gtk::TreeModelColumn<int> progress;
    Gtk::TreeModelColumn<int> upload_request_count_int;
    Gtk::TreeModelColumn<Glib::ustring> upload_request_count_string;
    Gtk::TreeModelColumn<int> download_request_count_int;
    Gtk::TreeModelColumn<Glib::ustring> download_request_count_string;
    Gtk::TreeModelColumn<int> blocks_downloaded_count_int;
    Gtk::TreeModelColumn<Glib::ustring> blocks_downloaded_count_string;
    Gtk::TreeModelColumn<int> blocks_uploaded_count_int;
    Gtk::TreeModelColumn<Glib::ustring> blocks_uploaded_count_string;
    Gtk::TreeModelColumn<int> reqs_cancelled_by_client_count_int;
    Gtk::TreeModelColumn<Glib::ustring> reqs_cancelled_by_client_count_string;
    Gtk::TreeModelColumn<int> reqs_cancelled_by_peer_count_int;
    Gtk::TreeModelColumn<Glib::ustring> reqs_cancelled_by_peer_count_string;
    Gtk::TreeModelColumn<Glib::ustring> encryption_stock_id;
    Gtk::TreeModelColumn<Glib::ustring> flags;
    Gtk::TreeModelColumn<Glib::ustring> torrent_name;
};

PeerModelColumns const peer_cols;

void initPeerRow(Gtk::TreeIter const& iter, std::string_view key, std::string_view torrent_name, tr_peer_stat const* peer)
{
    g_return_if_fail(peer != nullptr);

    char const* client = peer->client;
    if (client == nullptr || g_strcmp0(client, "Unknown Client") == 0)
    {
        client = "";
    }

    auto q = std::array<int, 4>{};
    auto const collated_name = sscanf(peer->addr, "%d.%d.%d.%d", &q[0], &q[1], &q[2], &q[3]) != 4 ?
        peer->addr :
        fmt::format(FMT_STRING("{:03d}.{:03d}.{:03d}.{:03d}"), q[0], q[1], q[2], q[3]);

    (*iter)[peer_cols.address] = peer->addr;
    (*iter)[peer_cols.address_collated] = collated_name;
    (*iter)[peer_cols.client] = client;
    (*iter)[peer_cols.encryption_stock_id] = peer->isEncrypted ? "lock" : "";
    (*iter)[peer_cols.key] = Glib::ustring{ std::data(key), std::size(key) };
    (*iter)[peer_cols.torrent_name] = Glib::ustring{ std::data(torrent_name), std::size(torrent_name) };
}

void refreshPeerRow(Gtk::TreeIter const& iter, tr_peer_stat const* peer)
{
    std::string up_speed;
    std::string down_speed;
    std::string up_count;
    std::string down_count;
    std::string blocks_to_peer;
    std::string blocks_to_client;
    std::string cancelled_by_peer;
    std::string cancelled_by_client;

    g_return_if_fail(peer != nullptr);

    if (peer->rateToPeer_KBps > 0.01)
    {
        up_speed = tr_formatter_speed_KBps(peer->rateToPeer_KBps);
    }

    if (peer->rateToClient_KBps > 0)
    {
        down_speed = tr_formatter_speed_KBps(peer->rateToClient_KBps);
    }

    if (peer->activeReqsToPeer > 0)
    {
        down_count = std::to_string(peer->activeReqsToPeer);
    }

    if (peer->activeReqsToClient > 0)
    {
        up_count = std::to_string(peer->activeReqsToClient);
    }

    if (peer->blocksToPeer > 0)
    {
        blocks_to_peer = std::to_string(peer->blocksToPeer);
    }

    if (peer->blocksToClient > 0)
    {
        blocks_to_client = std::to_string(peer->blocksToClient);
    }

    if (peer->cancelsToPeer > 0)
    {
        cancelled_by_client = std::to_string(peer->cancelsToPeer);
    }

    if (peer->cancelsToClient > 0)
    {
        cancelled_by_peer = std::to_string(peer->cancelsToClient);
    }

    (*iter)[peer_cols.progress] = (int)(100.0 * peer->progress);
    (*iter)[peer_cols.upload_request_count_int] = peer->activeReqsToClient;
    (*iter)[peer_cols.upload_request_count_string] = up_count;
    (*iter)[peer_cols.download_request_count_int] = peer->activeReqsToPeer;
    (*iter)[peer_cols.download_request_count_string] = down_count;
    (*iter)[peer_cols.download_rate_double] = peer->rateToClient_KBps;
    (*iter)[peer_cols.download_rate_string] = down_speed;
    (*iter)[peer_cols.upload_rate_double] = peer->rateToPeer_KBps;
    (*iter)[peer_cols.upload_rate_string] = up_speed;
    (*iter)[peer_cols.flags] = peer->flagStr;
    (*iter)[peer_cols.was_updated] = true;
    (*iter)[peer_cols.blocks_downloaded_count_int] = (int)peer->blocksToClient;
    (*iter)[peer_cols.blocks_downloaded_count_string] = blocks_to_client;
    (*iter)[peer_cols.blocks_uploaded_count_int] = (int)peer->blocksToPeer;
    (*iter)[peer_cols.blocks_uploaded_count_string] = blocks_to_peer;
    (*iter)[peer_cols.reqs_cancelled_by_client_count_int] = (int)peer->cancelsToPeer;
    (*iter)[peer_cols.reqs_cancelled_by_client_count_string] = cancelled_by_client;
    (*iter)[peer_cols.reqs_cancelled_by_peer_count_int] = (int)peer->cancelsToClient;
    (*iter)[peer_cols.reqs_cancelled_by_peer_count_string] = cancelled_by_peer;
}

} // namespace

void DetailsDialog::Impl::refreshPeerList(std::vector<tr_torrent*> const& torrents)
{
    auto& hash = peer_hash_;
    auto const& store = peer_store_;

    /* step 1: get all the peers */
    std::vector<tr_peer_stat*> peers;
    std::vector<int> peerCount;

    peers.reserve(torrents.size());
    peerCount.reserve(torrents.size());
    for (auto const* const torrent : torrents)
    {
        int count = 0;
        peers.push_back(tr_torrentPeers(torrent, &count));
        peerCount.push_back(count);
    }

    /* step 2: mark all the peers in the list as not-updated */
    for (auto const& row : store->children())
    {
        row[peer_cols.was_updated] = false;
    }

    auto make_key = [](tr_torrent const* tor, tr_peer_stat const* ps)
    {
        return fmt::format(FMT_STRING("{:d}.{:s}"), tr_torrentId(tor), ps->addr);
    };

    /* step 3: add any new peers */
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        auto const* tor = torrents.at(i);

        for (int j = 0; j < peerCount[i]; ++j)
        {
            auto const* s = &peers.at(i)[j];
            auto const key = make_key(tor, s);

            if (hash.find(key) == hash.end())
            {
                auto const iter = store->append();
                initPeerRow(iter, key, tr_torrentName(tor), s);
                hash.try_emplace(key, Gtk::TreeRowReference(store, store->get_path(iter)));
            }
        }
    }

    /* step 4: update the peers */
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        auto const* tor = torrents.at(i);

        for (int j = 0; j < peerCount[i]; ++j)
        {
            auto const* s = &peers.at(i)[j];
            auto const key = make_key(tor, s);
            refreshPeerRow(store->get_iter(hash.at(key).get_path()), s);
        }
    }

    /* step 5: remove peers that have disappeared */
    if (auto iter = store->children().begin(); iter)
    {
        while (iter)
        {
            if (iter->get_value(peer_cols.was_updated))
            {
                ++iter;
            }
            else
            {
                auto const key = iter->get_value(peer_cols.key);
                hash.erase(key);
                iter = store->erase(iter);
            }
        }
    }

    /* step 6: cleanup */
    for (size_t i = 0; i < peers.size(); ++i)
    {
        tr_torrentPeersFree(peers[i], peerCount[i]);
    }
}

void DetailsDialog::Impl::refreshWebseedList(std::vector<tr_torrent*> const& torrents)
{
    auto has_any_webseeds = bool{ false };
    auto& hash = webseed_hash_;
    auto const& store = webseed_store_;

    auto make_key = [](tr_torrent const* tor, char const* url)
    {
        return fmt::format(FMT_STRING("{:d}.{:s}"), tr_torrentId(tor), url);
    };

    /* step 1: mark all webseeds as not-updated */
    for (auto const& row : store->children())
    {
        row[webseed_cols.was_updated] = false;
    }

    /* step 2: add any new webseeds */
    for (auto const* const tor : torrents)
    {
        for (size_t j = 0, n = tr_torrentWebseedCount(tor); j < n; ++j)
        {
            has_any_webseeds = true;

            auto const* const url = tr_torrentWebseed(tor, j).url;
            auto const key = make_key(tor, url);

            if (hash.find(key) == hash.end())
            {
                auto const iter = store->append();
                (*iter)[webseed_cols.url] = url;
                (*iter)[webseed_cols.key] = key;
                hash.try_emplace(key, Gtk::TreeRowReference(store, store->get_path(iter)));
            }
        }
    }

    /* step 3: update the webseeds */
    for (auto const* const tor : torrents)
    {
        for (size_t j = 0, n = tr_torrentWebseedCount(tor); j < n; ++j)
        {
            auto const webseed = tr_torrentWebseed(tor, j);
            auto const key = make_key(tor, webseed.url);
            auto const iter = store->get_iter(hash.at(key).get_path());

            auto const KBps = double(webseed.download_bytes_per_second) / speed_K;
            auto const buf = webseed.is_downloading ? tr_formatter_speed_KBps(KBps) : std::string();

            (*iter)[webseed_cols.download_rate_double] = KBps;
            (*iter)[webseed_cols.download_rate_string] = buf;
            (*iter)[webseed_cols.was_updated] = true;
        }
    }

    /* step 4: remove webseeds that have disappeared */
    if (auto iter = store->children().begin(); iter)
    {
        while (iter)
        {
            if (iter->get_value(webseed_cols.was_updated))
            {
                ++iter;
            }
            else
            {
                auto const key = iter->get_value(webseed_cols.key);
                hash.erase(key);
                iter = store->erase(iter);
            }
        }
    }

    /* most of the time there are no webseeds...
       don't waste space showing an empty list */
    webseed_view_->set_visible(has_any_webseeds);
}

void DetailsDialog::Impl::refreshPeers(std::vector<tr_torrent*> const& torrents)
{
    refreshPeerList(torrents);
    refreshWebseedList(torrents);
}

bool DetailsDialog::Impl::onPeerViewQueryTooltip(int x, int y, bool keyboard_tip, Glib::RefPtr<Gtk::Tooltip> const& tooltip)
{
    Gtk::TreeModel::iterator iter;
    bool show_tip = false;

    if (peer_view_->get_tooltip_context_iter(x, y, keyboard_tip, iter))
    {
        auto const name = iter->get_value(peer_cols.torrent_name);
        auto const addr = iter->get_value(peer_cols.address);
        auto const flagstr = iter->get_value(peer_cols.flags);

        std::ostringstream gstr;
        gstr << "<b>" << Glib::Markup::escape_text(name) << "</b>\n" << addr << "\n \n";

        for (char const ch : flagstr)
        {
            char const* s = nullptr;

            switch (ch)
            {
            case 'O':
                s = _("Optimistic unchoke");
                break;

            case 'D':
                s = _("Downloading from this peer");
                break;

            case 'd':
                s = _("We would download from this peer if they would let us");
                break;

            case 'U':
                s = _("Uploading to peer");
                break;

            case 'u':
                s = _("We would upload to this peer if they asked");
                break;

            case 'K':
                s = _("Peer has unchoked us, but we're not interested");
                break;

            case '?':
                s = _("We unchoked this peer, but they're not interested");
                break;

            case 'E':
                s = _("Encrypted connection");
                break;

            case 'X':
                s = _("Peer was found through Peer Exchange (PEX)");
                break;

            case 'H':
                s = _("Peer was found through DHT");
                break;

            case 'I':
                s = _("Peer is an incoming connection");
                break;

            case 'T':
                s = _("Peer is connected over µTP");
                break;

            default:
                g_assert_not_reached();
            }

            if (s != nullptr)
            {
                gstr << ch << ": " << s << '\n';
            }
        }

        auto str = gstr.str();
        if (!str.empty()) /* remove the last linefeed */
        {
            str.resize(str.size() - 1);
        }

        tooltip->set_markup(str);

        show_tip = true;
    }

    return show_tip;
}

namespace
{

void setPeerViewColumns(Gtk::TreeView* peer_view)
{
    std::vector<Gtk::TreeModelColumnBase const*> view_columns;
    Gtk::TreeViewColumn* c;
    bool const more = gtr_pref_flag_get(TR_KEY_show_extra_peer_details);

    view_columns.push_back(&peer_cols.encryption_stock_id);
    view_columns.push_back(&peer_cols.upload_rate_string);

    if (more)
    {
        view_columns.push_back(&peer_cols.upload_request_count_string);
    }

    view_columns.push_back(&peer_cols.download_rate_string);

    if (more)
    {
        view_columns.push_back(&peer_cols.download_request_count_string);
    }

    if (more)
    {
        view_columns.push_back(&peer_cols.blocks_downloaded_count_string);
    }

    if (more)
    {
        view_columns.push_back(&peer_cols.blocks_uploaded_count_string);
    }

    if (more)
    {
        view_columns.push_back(&peer_cols.reqs_cancelled_by_client_count_string);
    }

    if (more)
    {
        view_columns.push_back(&peer_cols.reqs_cancelled_by_peer_count_string);
    }

    view_columns.push_back(&peer_cols.progress);
    view_columns.push_back(&peer_cols.flags);
    view_columns.push_back(&peer_cols.address);
    view_columns.push_back(&peer_cols.client);

    /* remove any existing columns */
    peer_view->remove_all_columns();

    for (auto const* const col : view_columns)
    {
        auto const* sort_col = col;

        if (*col == peer_cols.address)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Address"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.address_collated;
        }
        else if (*col == peer_cols.progress)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererProgress>();
            // % is percent done
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("%"), *r);
            c->add_attribute(r->property_text(), *col);
        }
        else if (*col == peer_cols.encryption_stock_id)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
            r->property_xalign() = 0.0F;
            r->property_yalign() = 0.5F;
            c = Gtk::make_managed<Gtk::TreeViewColumn>(Glib::ustring(), *r);
            c->add_attribute(r->property_icon_name(), *col);
            c->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
            c->set_fixed_width(20);
        }
        else if (*col == peer_cols.download_request_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Dn Reqs"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.download_request_count_int;
        }
        else if (*col == peer_cols.upload_request_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Up Reqs"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.upload_request_count_int;
        }
        else if (*col == peer_cols.blocks_downloaded_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Dn Blocks"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.blocks_downloaded_count_int;
        }
        else if (*col == peer_cols.blocks_uploaded_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Up Blocks"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.blocks_uploaded_count_int;
        }
        else if (*col == peer_cols.reqs_cancelled_by_client_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("We Cancelled"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.reqs_cancelled_by_client_count_int;
        }
        else if (*col == peer_cols.reqs_cancelled_by_peer_count_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("They Cancelled"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.reqs_cancelled_by_peer_count_int;
        }
        else if (*col == peer_cols.download_rate_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            r->property_xalign() = 1.0F;
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Down"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.download_rate_double;
        }
        else if (*col == peer_cols.upload_rate_string)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            r->property_xalign() = 1.0F;
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Up"), *r);
            c->add_attribute(r->property_text(), *col);
            sort_col = &peer_cols.upload_rate_double;
        }
        else if (*col == peer_cols.client)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Client"), *r);
            c->add_attribute(r->property_text(), *col);
        }
        else if (*col == peer_cols.flags)
        {
            auto* r = Gtk::make_managed<Gtk::CellRendererText>();
            c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Flags"), *r);
            c->add_attribute(r->property_text(), *col);
        }
        else
        {
            abort();
        }

        c->set_resizable(false);
        c->set_sort_column(*sort_col);
        peer_view->append_column(*c);
    }

    /* the 'expander' column has a 10-pixel margin on the left
       that doesn't look quite correct in any of these columns...
       so create a non-visible column and assign it as the
       'expander column. */
    c = Gtk::make_managed<Gtk::TreeViewColumn>();
    c->set_visible(false);
    peer_view->append_column(*c);
    peer_view->set_expander_column(*c);
}

} // namespace

void DetailsDialog::Impl::onMorePeerInfoToggled()
{
    tr_quark const key = TR_KEY_show_extra_peer_details;
    bool const value = more_peer_details_check_->get_active();
    core_->set_pref(key, value);
    setPeerViewColumns(peer_view_);
}

Gtk::Widget* DetailsDialog::Impl::peer_page_new()
{
    /* webseeds */

    webseed_store_ = Gtk::ListStore::create(webseed_cols);
    auto* v = Gtk::make_managed<Gtk::TreeView>(webseed_store_);
    v->signal_button_release_event().connect([v](GdkEventButton* event) { return on_tree_view_button_released(v, event); });

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->property_ellipsize() = Pango::ELLIPSIZE_END;
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Web Seeds"), *r);
        c->add_attribute(r->property_text(), webseed_cols.url);
        c->set_expand(true);
        c->set_sort_column(webseed_cols.url);
        v->append_column(*c);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        auto* c = Gtk::make_managed<Gtk::TreeViewColumn>(_("Down"), *r);
        c->add_attribute(r->property_text(), webseed_cols.download_rate_string);
        c->set_sort_column(webseed_cols.download_rate_double);
        v->append_column(*c);
    }

    webseed_view_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    webseed_view_->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    webseed_view_->set_shadow_type(Gtk::SHADOW_IN);
    webseed_view_->add(*v);

    /* peers */

    peer_store_ = Gtk::ListStore::create(peer_cols);
    auto m = Gtk::TreeModelSort::create(peer_store_);
    m->set_sort_column(peer_cols.progress, Gtk::SORT_DESCENDING);
    peer_view_ = Gtk::make_managed<Gtk::TreeView>(m);
    peer_view_->set_has_tooltip(true);

    peer_view_->signal_query_tooltip().connect(sigc::mem_fun(*this, &Impl::onPeerViewQueryTooltip));
    peer_view_->signal_button_release_event().connect([this](GdkEventButton* event)
                                                      { return on_tree_view_button_released(peer_view_, event); });

    setPeerViewColumns(peer_view_);

    auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    sw->set_shadow_type(Gtk::SHADOW_IN);
    sw->add(*peer_view_);

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, GUI_PAD);
    vbox->set_border_width(GUI_PAD_BIG);

    auto* v2 = Gtk::make_managed<Gtk::Paned>(Gtk::ORIENTATION_VERTICAL);
    v2->add(*webseed_view_);
    v2->add(*sw);
    vbox->pack_start(*v2, true, true);

    more_peer_details_check_ = Gtk::make_managed<Gtk::CheckButton>(_("Show _more details"), true);
    more_peer_details_check_->set_active(gtr_pref_flag_get(TR_KEY_show_extra_peer_details));
    more_peer_details_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onMorePeerInfoToggled));
    vbox->pack_start(*more_peer_details_check_, false, false);

    return vbox;
}

/****
*****
*****  TRACKER
*****
****/

namespace
{

auto constexpr ErrMarkupBegin = "<span color=\"red\">"sv;
auto constexpr ErrMarkupEnd = "</span>"sv;
auto constexpr TimeoutMarkupBegin = "<span color=\"#246\">"sv;
auto constexpr TimeoutMarkupEnd = "</span>"sv;
auto constexpr SuccessMarkupBegin = "<span color=\"#080\">"sv;
auto constexpr SuccessMarkupEnd = "</span>"sv;

std::array<std::string_view, 3> const text_dir_mark = { ""sv, "\u200E"sv, "\u200F"sv };

void appendAnnounceInfo(tr_tracker_view const& tracker, time_t const now, Gtk::TextDirection direction, std::ostream& gstr)
{
    if (tracker.hasAnnounced && tracker.announceState != TR_TRACKER_INACTIVE)
    {
        gstr << '\n';
        gstr << text_dir_mark[direction];
        auto const time_span_ago = tr_format_time_relative(now, tracker.lastAnnounceTime);

        if (tracker.lastAnnounceSucceeded)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the peer text
                ngettext(
                    "Got a list of {markup_begin}{peer_count} peer{markup_end} {time_span_ago}",
                    "Got a list of {markup_begin}{peer_count} peers{markup_end} {time_span_ago}",
                    tracker.lastAnnouncePeerCount),
                fmt::arg("markup_begin", SuccessMarkupBegin),
                fmt::arg("peer_count", tracker.lastAnnouncePeerCount),
                fmt::arg("markup_end", SuccessMarkupEnd),
                fmt::arg("time_span_ago", time_span_ago));
        }
        else if (tracker.lastAnnounceTimedOut)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the time_span
                _("Peer list request {markup_begin}timed out {time_span_ago}{markup_end}; will retry"),
                fmt::arg("markup_begin", TimeoutMarkupBegin),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_end", TimeoutMarkupEnd));
        }
        else
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the error
                _("Got an error '{markup_begin}{error}{markup_end}' {time_span_ago}"),
                fmt::arg("markup_begin", ErrMarkupBegin),
                fmt::arg("error", Glib::Markup::escape_text(tracker.lastAnnounceResult)),
                fmt::arg("markup_end", ErrMarkupEnd),
                fmt::arg("time_span_ago", time_span_ago));
        }
    }

    switch (tracker.announceState)
    {
    case TR_TRACKER_INACTIVE:
        gstr << '\n';
        gstr << text_dir_mark[direction];
        gstr << _("No updates scheduled");
        break;

    case TR_TRACKER_WAITING:
        gstr << '\n';
        gstr << text_dir_mark[direction];
        gstr << fmt::format(
            _("Asking for more peers {time_span_from_now}"),
            fmt::arg("time_span_from_now", tr_format_time_relative(now, tracker.nextAnnounceTime)));
        break;

    case TR_TRACKER_QUEUED:
        gstr << '\n';
        gstr << text_dir_mark[direction];
        gstr << _("Queued to ask for more peers");
        break;

    case TR_TRACKER_ACTIVE:
        gstr << '\n';
        gstr << text_dir_mark[direction];
        gstr << fmt::format(
            // {markup_begin} and {markup_end} should surround time_span_ago
            _("Asked for more peers {markup_begin}{time_span_ago}{markup_end}"),
            fmt::arg("markup_begin", "<small>"),
            fmt::arg("time_span_ago", tr_format_time_relative(now, tracker.lastAnnounceStartTime)),
            fmt::arg("markup_end", "</small>"));
        break;

    default:
        g_assert_not_reached();
    }
}

void appendScrapeInfo(tr_tracker_view const& tracker, time_t const now, Gtk::TextDirection direction, std::ostream& gstr)
{
    if (tracker.hasScraped)
    {
        gstr << '\n';
        gstr << text_dir_mark[direction];
        auto const time_span_ago = tr_format_time_relative(now, tracker.lastScrapeTime);

        if (tracker.lastScrapeSucceeded)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the seeder/leecher text
                _("Tracker had {markup_begin}{seeder_count} {seeder_or_seeders} and {leecher_count} {leecher_or_leechers}{markup_end} {time_span_ago}"),
                fmt::arg("seeder_count", tracker.seederCount),
                fmt::arg("seeder_or_seeders", ngettext("seeder", "seeders", tracker.seederCount)),
                fmt::arg("leecher_count", tracker.leecherCount),
                fmt::arg("leecher_or_leechers", ngettext("leecher", "leechers", tracker.leecherCount)),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_begin", SuccessMarkupBegin),
                fmt::arg("markup_end", SuccessMarkupEnd));
        }
        else
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the error text
                _("Got a scrape error '{markup_begin}{error}{markup_end}' {time_span_ago}"),
                fmt::arg("error", Glib::Markup::escape_text(tracker.lastScrapeResult)),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_begin", ErrMarkupBegin),
                fmt::arg("markup_end", ErrMarkupEnd));
        }
    }

    switch (tracker.scrapeState)
    {
    case TR_TRACKER_INACTIVE:
        break;

    case TR_TRACKER_WAITING:
        gstr << '\n';
        gstr << text_dir_mark[direction];
        gstr << fmt::format(
            _("Asking for peer counts in {time_span_from_now}"),
            fmt::arg("time_span_from_now", tr_format_time_relative(now, tracker.nextScrapeTime)));
        break;

    case TR_TRACKER_QUEUED:
        gstr << '\n';
        gstr << text_dir_mark[direction];
        gstr << _("Queued to ask for peer counts");
        break;

    case TR_TRACKER_ACTIVE:
        gstr << '\n';
        gstr << text_dir_mark[direction];
        gstr << fmt::format(
            _("Asked for peer counts {markup_begin}{time_span_ago}{markup_end}"),
            fmt::arg("markup_begin", "<small>"),
            fmt::arg("time_span_ago", tr_format_time_relative(now, tracker.lastScrapeStartTime)),
            fmt::arg("markup_end", "</small>"));
        break;

    default:
        g_assert_not_reached();
    }
}

void buildTrackerSummary(
    std::ostream& gstr,
    std::string const& key,
    tr_tracker_view const& tracker,
    bool showScrape,
    Gtk::TextDirection direction)
{
    // hostname
    gstr << text_dir_mark[direction];
    gstr << (tracker.isBackup ? "<i>" : "<b>");
    gstr << Glib::Markup::escape_text(!key.empty() ? fmt::format(FMT_STRING("{:s} - {:s}"), tracker.host, key) : tracker.host);
    gstr << (tracker.isBackup ? "</i>" : "</b>");

    if (!tracker.isBackup)
    {
        time_t const now = time(nullptr);

        appendAnnounceInfo(tracker, now, direction, gstr);

        if (showScrape)
        {
            appendScrapeInfo(tracker, now, direction, gstr);
        }
    }
}

class TrackerModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    TrackerModelColumns()
    {
        add(torrent_id);
        add(text);
        add(is_backup);
        add(tracker_id);
        add(favicon);
        add(was_updated);
        add(key);
    }

    Gtk::TreeModelColumn<tr_torrent_id_t> torrent_id;
    Gtk::TreeModelColumn<Glib::ustring> text;
    Gtk::TreeModelColumn<bool> is_backup;
    Gtk::TreeModelColumn<int> tracker_id;
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> favicon;
    Gtk::TreeModelColumn<bool> was_updated;
    Gtk::TreeModelColumn<std::string> key;
};

TrackerModelColumns const tracker_cols;

} // namespace

bool DetailsDialog::Impl::trackerVisibleFunc(Gtk::TreeModel::const_iterator const& iter)
{
    /* show all */
    if (all_check_->get_active())
    {
        return true;
    }

    /* don't show the backups... */
    return !iter->get_value(tracker_cols.is_backup);
}

tr_torrent_id_t DetailsDialog::Impl::tracker_list_get_current_torrent_id() const
{
    // if there's only one torrent in the dialog, always use it
    if (ids_.size() == 1)
    {
        return ids_.front();
    }

    // otherwise, use the selected tracker's torrent
    auto const sel = tracker_view_->get_selection();
    if (auto const iter = sel->get_selected(); iter)
    {
        return iter->get_value(tracker_cols.torrent_id);
    }

    return -1;
}

tr_torrent* DetailsDialog::Impl::tracker_list_get_current_torrent() const
{
    return core_->find_torrent(tracker_list_get_current_torrent_id());
}

namespace
{

void favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const& pixbuf, Gtk::TreeRowReference& reference)
{
    if (pixbuf != nullptr)
    {
        auto const path = reference.get_path();
        auto const model = reference.get_model();

        if (auto const iter = model->get_iter(path); iter)
        {
            (*iter)[tracker_cols.favicon] = pixbuf;
        }
    }
}

} // namespace

void DetailsDialog::Impl::refreshTracker(std::vector<tr_torrent*> const& torrents)
{
    std::ostringstream gstr;
    auto& hash = tracker_hash_;
    auto const& store = tracker_store_;
    auto* session = core_->get_session();
    bool const showScrape = scrape_check_->get_active();

    /* step 1: get all the trackers */
    auto trackers = std::multimap<tr_torrent const*, tr_tracker_view>{};
    for (auto const* tor : torrents)
    {
        for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
        {
            trackers.emplace(tor, tr_torrentTracker(tor, i));
        }
    }

    /* step 2: mark all the trackers in the list as not-updated */
    for (auto const& row : store->children())
    {
        row[tracker_cols.was_updated] = false;
    }

    /* step 3: add / update trackers */
    for (auto const& [tor, tracker] : trackers)
    {
        auto const torrent_id = tr_torrentId(tor);

        // build the key to find the row
        gstr.str({});
        gstr << torrent_id << '\t' << tracker.tier << '\t' << tracker.announce;
        if (hash.find(gstr.str()) == hash.end())
        {
            // if we didn't have that row, add it
            auto const iter = store->append();
            (*iter)[tracker_cols.torrent_id] = torrent_id;
            (*iter)[tracker_cols.tracker_id] = tracker.id;
            (*iter)[tracker_cols.key] = gstr.str();

            auto const p = store->get_path(iter);
            hash.try_emplace(gstr.str(), Gtk::TreeRowReference(store, p));
            gtr_get_favicon_from_url(
                session,
                tracker.announce,
                [ref = Gtk::TreeRowReference(store, p)](auto const& pixbuf) mutable { favicon_ready_cb(pixbuf, ref); });
        }
    }

    /* step 4: update the rows */
    auto const summary_name = std::string(std::size(torrents) == 1 ? tr_torrentName(torrents.front()) : "");
    for (auto const& [tor, tracker] : trackers)
    {
        auto const torrent_id = tr_torrentId(tor);

        // build the key to find the row
        gstr.str({});
        gstr << torrent_id << '\t' << tracker.tier << '\t' << tracker.announce;
        auto const iter = store->get_iter(hash.at(gstr.str()).get_path());

        // update the row
        gstr.str({});
        buildTrackerSummary(gstr, summary_name, tracker, showScrape, dialog_.get_direction());
        (*iter)[tracker_cols.text] = gstr.str();
        (*iter)[tracker_cols.is_backup] = tracker.isBackup;
        (*iter)[tracker_cols.tracker_id] = tracker.id;
        (*iter)[tracker_cols.was_updated] = true;
    }

    /* step 5: remove trackers that have disappeared */
    if (auto iter = store->children().begin(); iter)
    {
        while (iter)
        {
            if (iter->get_value(tracker_cols.was_updated))
            {
                ++iter;
            }
            else
            {
                auto const key = iter->get_value(tracker_cols.key);
                hash.erase(key);
                iter = store->erase(iter);
            }
        }
    }

    edit_trackers_button_->set_sensitive(tracker_list_get_current_torrent_id() > 0);
}

void DetailsDialog::Impl::onScrapeToggled()
{
    tr_quark const key = TR_KEY_show_tracker_scrapes;
    bool const value = scrape_check_->get_active();
    core_->set_pref(key, value);
    refresh();
}

void DetailsDialog::Impl::onBackupToggled()
{
    tr_quark const key = TR_KEY_show_backup_trackers;
    bool const value = all_check_->get_active();
    core_->set_pref(key, value);
    refresh();
}

void DetailsDialog::Impl::on_edit_trackers_response(int response, std::shared_ptr<Gtk::Dialog>& dialog)
{
    bool do_destroy = true;

    if (response == Gtk::RESPONSE_ACCEPT)
    {
        auto const torrent_id = GPOINTER_TO_INT(dialog->get_data(TORRENT_ID_KEY));
        auto const* const text_buffer = static_cast<Gtk::TextBuffer*>(dialog->get_data(TEXT_BUFFER_KEY));

        if (auto* const tor = core_->find_torrent(torrent_id); tor != nullptr)
        {
            if (tr_torrentSetTrackerList(tor, text_buffer->get_text(false).c_str()))
            {
                refresh();
            }
            else
            {
                Gtk::MessageDialog
                    w(*dialog, _("List contains invalid URLs"), false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_CLOSE, true);
                w.set_secondary_text(_("Please correct the errors and try again."));
                w.run();

                do_destroy = false;
            }
        }
    }

    if (do_destroy)
    {
        dialog.reset();
    }
}

namespace
{

std::string get_editable_tracker_list(tr_torrent const* tor)
{
    char* cstr = tr_torrentGetTrackerList(tor);
    auto str = std::string{ cstr != nullptr ? cstr : "" };
    tr_free(cstr);
    return str;
}

} // namespace

void DetailsDialog::Impl::on_edit_trackers()
{
    tr_torrent const* tor = tracker_list_get_current_torrent();

    if (tor != nullptr)
    {
        guint row;
        auto const torrent_id = tr_torrentId(tor);

        auto d = std::make_shared<Gtk::Dialog>(
            fmt::format(_("{torrent_name} - Edit Trackers"), fmt::arg("torrent_name", tr_torrentName(tor))),
            dialog_,
            Gtk::DIALOG_MODAL | Gtk::DIALOG_DESTROY_WITH_PARENT);
        d->add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
        d->add_button(_("_Save"), Gtk::RESPONSE_ACCEPT);
        d->signal_response().connect([this, d](int response) mutable { on_edit_trackers_response(response, d); });

        row = 0;
        auto* t = Gtk::make_managed<HigWorkarea>();
        t->add_section_title(row, _("Tracker Announce URLs"));

        auto* l = Gtk::make_managed<Gtk::Label>();
        l->set_markup(
            _("To add a backup URL, add it on the next line after a primary URL.\n"
              "To add a new primary URL, add it after a blank line."));
        l->set_justify(Gtk::JUSTIFY_LEFT);
        l->set_halign(Gtk::ALIGN_START);
        l->set_valign(Gtk::ALIGN_CENTER);
        t->add_wide_control(row, *l);

        auto* w = Gtk::make_managed<Gtk::TextView>();
        w->get_buffer()->set_text(get_editable_tracker_list(tor));
        auto* fr = Gtk::make_managed<Gtk::Frame>();
        fr->set_shadow_type(Gtk::SHADOW_IN);
        auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
        sw->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        sw->add(*w);
        fr->add(*sw);
        fr->set_size_request(500U, 166U);
        t->add_wide_tall_control(row, *fr);

        l = Gtk::make_managed<Gtk::Label>();
        l->set_markup(_("Also see Default Public Trackers in Edit > Preferences > Network"));
        l->set_justify(Gtk::JUSTIFY_LEFT);
        l->set_halign(Gtk::ALIGN_START);
        l->set_valign(Gtk::ALIGN_CENTER);
        t->add_wide_control(row, *l);

        gtr_dialog_set_content(*d, *t);

        d->set_data(TORRENT_ID_KEY, GINT_TO_POINTER(torrent_id));
        d->set_data(TEXT_BUFFER_KEY, gtr_get_ptr(w->get_buffer()));

        d->show();
    }
}

void DetailsDialog::Impl::on_tracker_list_selection_changed()
{
    int const n = tracker_view_->get_selection()->count_selected_rows();
    auto const* const tor = tracker_list_get_current_torrent();

    remove_tracker_button_->set_sensitive(n > 0);
    add_tracker_button_->set_sensitive(tor != nullptr);
    edit_trackers_button_->set_sensitive(tor != nullptr);
}

void DetailsDialog::Impl::on_add_tracker_response(int response, std::shared_ptr<Gtk::Dialog>& dialog)
{
    bool destroy = true;

    if (response == Gtk::RESPONSE_ACCEPT)
    {
        auto const* const e = static_cast<Gtk::Entry*>(dialog->get_data(URL_ENTRY_KEY));
        auto const torrent_id = GPOINTER_TO_INT(dialog->get_data(TORRENT_ID_KEY));
        auto const url = gtr_str_strip(e->get_text());

        if (!url.empty())
        {
            if (tr_urlIsValidTracker(url.c_str()))
            {
                tr_variant top;
                tr_variant* args;
                tr_variant* trackers;

                tr_variantInitDict(&top, 2);
                tr_variantDictAddStrView(&top, TR_KEY_method, "torrent-set"sv);
                args = tr_variantDictAddDict(&top, TR_KEY_arguments, 2);
                tr_variantDictAddInt(args, TR_KEY_id, torrent_id);
                trackers = tr_variantDictAddList(args, TR_KEY_trackerAdd, 1);
                tr_variantListAddStr(trackers, url.raw());

                core_->exec(&top);
                refresh();

                tr_variantFree(&top);
            }
            else
            {
                gtr_unrecognized_url_dialog(*dialog, url);
                destroy = false;
            }
        }
    }

    if (destroy)
    {
        dialog.reset();
    }
}

void DetailsDialog::Impl::on_tracker_list_add_button_clicked()
{
    tr_torrent const* tor = tracker_list_get_current_torrent();

    if (tor != nullptr)
    {
        guint row;

        auto w = std::make_shared<Gtk::Dialog>(
            fmt::format(_("{torrent_name} - Add Tracker"), fmt::arg("torrent_name", tr_torrentName(tor))),
            dialog_,
            Gtk::DIALOG_DESTROY_WITH_PARENT);
        w->add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
        w->add_button(_("_Add"), Gtk::RESPONSE_ACCEPT);
        w->signal_response().connect([this, w](int response) mutable { on_add_tracker_response(response, w); });

        row = 0;
        auto* t = Gtk::make_managed<HigWorkarea>();
        t->add_section_title(row, _("Tracker"));
        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_size_request(400, -1);
        gtr_paste_clipboard_url_into_entry(*e);
        w->set_data(URL_ENTRY_KEY, e);
        w->set_data(TORRENT_ID_KEY, GINT_TO_POINTER(tr_torrentId(tor)));
        t->add_row(row, _("_Announce URL:"), *e);
        gtr_dialog_set_content(*w, *t);

        w->show_all();
    }
}

void DetailsDialog::Impl::on_tracker_list_remove_button_clicked()
{
    auto* v = tracker_view_;
    auto sel = v->get_selection();

    if (auto const iter = sel->get_selected(); iter)
    {
        auto const torrent_id = iter->get_value(tracker_cols.torrent_id);
        auto const tracker_id = iter->get_value(tracker_cols.tracker_id);
        tr_variant top;
        tr_variant* args;
        tr_variant* trackers;

        tr_variantInitDict(&top, 2);
        tr_variantDictAddStrView(&top, TR_KEY_method, "torrent-set"sv);
        args = tr_variantDictAddDict(&top, TR_KEY_arguments, 2);
        tr_variantDictAddInt(args, TR_KEY_id, torrent_id);
        trackers = tr_variantDictAddList(args, TR_KEY_trackerRemove, 1);
        tr_variantListAddInt(trackers, tracker_id);

        core_->exec(&top);
        refresh();

        tr_variantFree(&top);
    }
}

Gtk::Widget* DetailsDialog::Impl::tracker_page_new()
{
    int const pad = (GUI_PAD + GUI_PAD_BIG) / 2;

    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, GUI_PAD);
    vbox->set_border_width(GUI_PAD_BIG);

    tracker_store_ = Gtk::ListStore::create(tracker_cols);

    trackers_filtered_ = Gtk::TreeModelFilter::create(tracker_store_);
    trackers_filtered_->set_visible_func(sigc::mem_fun(*this, &Impl::trackerVisibleFunc));

    auto* hbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD_BIG);

    tracker_view_ = Gtk::make_managed<Gtk::TreeView>(trackers_filtered_);
    tracker_view_->set_headers_visible(false);
    tracker_view_->signal_button_press_event().connect([this](GdkEventButton* event)
                                                       { return on_tree_view_button_pressed(tracker_view_, event); });
    tracker_view_->signal_button_release_event().connect([this](GdkEventButton* event)
                                                         { return on_tree_view_button_released(tracker_view_, event); });

    auto sel = tracker_view_->get_selection();
    sel->signal_changed().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_selection_changed));

    auto* c = Gtk::make_managed<Gtk::TreeViewColumn>();
    c->set_title(_("Trackers"));
    tracker_view_->append_column(*c);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        r->property_width() = 20 + (GUI_PAD_SMALL * 2);
        r->property_xpad() = GUI_PAD_SMALL;
        r->property_ypad() = pad;
        r->property_yalign() = 0.0F;
        c->pack_start(*r, false);
        c->add_attribute(r->property_pixbuf(), tracker_cols.favicon);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        r->property_ellipsize() = Pango::ELLIPSIZE_END;
        r->property_xpad() = GUI_PAD_SMALL;
        r->property_ypad() = pad;
        c->pack_start(*r, true);
        c->add_attribute(r->property_markup(), tracker_cols.text);
    }

    auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    sw->add(*tracker_view_);
    auto* w = Gtk::make_managed<Gtk::Frame>();
    w->set_shadow_type(Gtk::SHADOW_IN);
    w->add(*sw);

    hbox->pack_start(*w, true, true);

    auto* v = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, GUI_PAD);

    add_tracker_button_ = Gtk::make_managed<Gtk::Button>(_("_Add"), true);
    add_tracker_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_add_button_clicked));
    v->pack_start(*add_tracker_button_, false, false);

    edit_trackers_button_ = Gtk::make_managed<Gtk::Button>(_("_Edit"), true);
    edit_trackers_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_edit_trackers));
    v->pack_start(*edit_trackers_button_, false, false);

    remove_tracker_button_ = Gtk::make_managed<Gtk::Button>(_("_Remove"), true);
    remove_tracker_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_remove_button_clicked));
    v->pack_start(*remove_tracker_button_, false, false);

    hbox->pack_start(*v, false, false);

    vbox->pack_start(*hbox, true, true);

    scrape_check_ = Gtk::make_managed<Gtk::CheckButton>(_("Show _more details"), true);
    scrape_check_->set_active(gtr_pref_flag_get(TR_KEY_show_tracker_scrapes));
    scrape_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onScrapeToggled));
    vbox->pack_start(*scrape_check_, false, false);

    all_check_ = Gtk::make_managed<Gtk::CheckButton>(_("Show _backup trackers"), true);
    all_check_->set_active(gtr_pref_flag_get(TR_KEY_show_backup_trackers));
    all_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onBackupToggled));
    vbox->pack_start(*all_check_, false, false);

    return vbox;
}

/****
*****  DIALOG
****/

void DetailsDialog::Impl::refresh()
{
    auto const torrents = getTorrents();

    refreshInfo(torrents);
    refreshPeers(torrents);
    refreshTracker(torrents);
    refreshOptions(torrents);

    if (torrents.empty())
    {
        dialog_.response(Gtk::RESPONSE_CLOSE);
    }
}

void DetailsDialog::Impl::on_details_window_size_allocated(Gtk::Allocation& /*alloc*/)
{
    int w = 0;
    int h = 0;
    dialog_.get_size(w, h);
    gtr_pref_int_set(TR_KEY_details_window_width, w);
    gtr_pref_int_set(TR_KEY_details_window_height, h);
}

DetailsDialog::Impl::~Impl()
{
    periodic_refresh_tag_.disconnect();
    last_page_tag_.disconnect();
}

std::unique_ptr<DetailsDialog> DetailsDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    return std::unique_ptr<DetailsDialog>(new DetailsDialog(parent, core));
}

DetailsDialog::DetailsDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
    : Gtk::Dialog({}, parent)
    , impl_(std::make_unique<Impl>(*this, core))
{
}

DetailsDialog::~DetailsDialog() = default;

DetailsDialog::Impl::Impl(DetailsDialog& dialog, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
{
    /* create the dialog */
    dialog_.add_button(_("_Close"), Gtk::RESPONSE_CLOSE);
    dialog_.set_role("tr-info");

    /* return saved window size */
    dialog_.resize((int)gtr_pref_int_get(TR_KEY_details_window_width), (int)gtr_pref_int_get(TR_KEY_details_window_height));
    dialog_.signal_size_allocate().connect(sigc::mem_fun(*this, &Impl::on_details_window_size_allocated));

    dialog_.signal_response().connect(sigc::hide<0>(sigc::mem_fun(dialog_, &DetailsDialog::hide)));
    dialog_.set_border_width(GUI_PAD);

    auto* n = Gtk::make_managed<Gtk::Notebook>();
    n->set_border_width(GUI_PAD);

    n->append_page(*info_page_new(), _("Information"));
    n->append_page(*peer_page_new(), _("Peers"));
    n->append_page(*tracker_page_new(), _("Trackers"));

    auto* v = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
    file_list_ = Gtk::make_managed<FileList>(core, 0);
    file_label_ = Gtk::make_managed<Gtk::Label>(_("File listing not available for combined torrent properties"));
    v->pack_start(*file_list_, true, true, 0);
    v->pack_start(*file_label_, true, true, 0);
    v->set_border_width(GUI_PAD_BIG);
    n->append_page(*v, _("Files"));

    n->append_page(*options_page_new(), _("Options"));

    gtr_dialog_set_content(dialog_, *n);
    periodic_refresh_tag_ = Glib::signal_timeout().connect_seconds(
        [this]() { return refresh(), true; },
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);

    n->set_current_page(last_page_);
    last_page_tag_ = n->signal_switch_page().connect([](Widget*, guint page) { DetailsDialog::Impl::last_page_ = page; });
}

void DetailsDialog::set_torrents(std::vector<tr_torrent_id_t> const& ids)
{
    impl_->set_torrents(ids);
}

void DetailsDialog::Impl::set_torrents(std::vector<tr_torrent_id_t> const& ids)
{
    Glib::ustring title;
    auto const len = ids.size();

    ids_ = ids;

    if (len == 1)
    {
        int const id = ids.front();
        auto const* tor = core_->find_torrent(id);
        title = fmt::format(_("{torrent_name} Properties"), fmt::arg("torrent_name", tr_torrentName(tor)));

        file_list_->set_torrent(id);
        file_list_->show();
        file_label_->hide();
    }
    else
    {
        title = fmt::format(
            ngettext("Properties - {torrent_count:L} Torrent", "Properties - {torrent_count:L} Torrents", len),
            fmt::arg("torrent_count", len));

        file_list_->clear();
        file_list_->hide();
        file_label_->show();
    }

    dialog_.set_title(title);

    refresh();
}
