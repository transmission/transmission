// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Torrent.h"

#include "IconCache.h"
#include "Percents.h"
#include "Utils.h"

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include <glibmm/i18n.h>
#include <glibmm/value.h>

#include <fmt/core.h>

#include <array>
#include <functional>
#include <utility>

using namespace std::string_view_literals;

namespace
{

template<typename T>
Glib::Value<T>& column_value_cast(Glib::ValueBase& value, Gtk::TreeModelColumn<T> const& /*column*/)
{
    return static_cast<Glib::Value<T>&>(value);
}

template<typename T, typename U, typename = std::enable_if_t<!std::is_floating_point_v<T>>>
void update_cache_value(T& value, U&& new_value, Torrent::ChangeFlags& changes, Torrent::ChangeFlag flag)
{
    using std::rel_ops::operator!=;

    if (value != new_value)
    {
        value = std::forward<U>(new_value);
        changes.set(flag);
    }
}

template<typename T, typename U, typename = std::enable_if_t<std::is_floating_point_v<T>>>
void update_cache_value(T& value, U new_value, T epsilon, Torrent::ChangeFlags& changes, Torrent::ChangeFlag flag)
{
    if (std::fabs(value - new_value) >= epsilon)
    {
        value = new_value;
        changes.set(flag);
    }
}

unsigned int build_torrent_trackers_hash(tr_torrent const& torrent)
{
    auto hash = uint64_t(0);

    for (auto i = size_t(0), n = tr_torrentTrackerCount(&torrent); i < n; ++i)
    {
        for (auto const ch : std::string_view{ tr_torrentTracker(&torrent, i).announce })
        {
            hash = (hash << 4U) ^ (hash >> 28U) ^ static_cast<unsigned char>(ch);
        }
    }

    return hash;
}

std::string_view get_mime_type(tr_torrent const& torrent)
{
    auto const n_files = tr_torrentFileCount(&torrent);

    if (n_files == 0)
    {
        return UnknownMimeType;
    }

    if (n_files > 1)
    {
        return DirectoryMimeType;
    }

    auto const name = std::string_view(tr_torrentFile(&torrent, 0).name);

    return name.find('/') != std::string_view::npos ? DirectoryMimeType : tr_get_mime_type_for_filename(name);
}

} // namespace

Torrent::Columns::Columns()
{
    add(self);
    add(name_collated);
}

class Torrent::Impl
{
public:
    struct Cache
    {
        Glib::ustring error_message;
        Glib::ustring name;
        Glib::ustring name_collated;

        std::string_view mime_type;

        uint64_t have_unchecked = {};
        uint64_t have_valid = {};
        uint64_t left_until_done = {};
        uint64_t size_when_done = {};
        uint64_t total_size = {};
        uint64_t uploaded_ever = {};

        size_t queue_position = {};

        time_t added_date = {};
        time_t eta = {};

        tr_torrent_activity activity = {};

        unsigned int trackers = {};
        int active_peer_count = {};
        int active_peers_down = {};
        int active_peers_up = {};
        int error_code = {};

        Percents activity_percent_done = {};
        Percents metadata_percent_complete = {};
        Percents percent_complete = {};
        Percents percent_done = {};
        Percents recheck_progress = {};
        Percents seed_ratio_percent_done = {};

        uint16_t peers_connected = {};
        uint16_t peers_getting_from_us = {};
        uint16_t peers_sending_to_us = {};
        uint16_t webseeds_sending_to_us = {};

        float ratio = {};
        float seed_ratio = {};
        float speed_down = {};
        float speed_up = {};

        tr_priority_t priority = {};

        bool active = {};
        bool finished = {};
        bool has_metadata = {};
        bool has_seed_ratio = {};
        bool stalled = {};
    };

public:
    Impl(Torrent& torrent, tr_torrent* raw_torrent);

    tr_torrent* get_raw_torrent()
    {
        return raw_torrent_;
    }

    Cache& get_cache()
    {
        return cache_;
    }

    ChangeFlags update_cache();

    void notify_property_changes(ChangeFlags changes) const;

    void get_value(int column, Glib::ValueBase& value) const;

    [[nodiscard]] Glib::RefPtr<Gio::Icon> get_icon() const;
    [[nodiscard]] Glib::ustring get_short_status_text() const;
    [[nodiscard]] Glib::ustring get_long_progress_text() const;
    [[nodiscard]] Glib::ustring get_long_status_text() const;

private:
    [[nodiscard]] Glib::ustring get_short_transfer_text() const;
    [[nodiscard]] Glib::ustring get_error_text() const;
    [[nodiscard]] Glib::ustring get_activity_text() const;

private:
    Torrent& torrent_;
    tr_torrent* const raw_torrent_;

    Cache cache_;
};

Torrent::Impl::Impl(Torrent& torrent, tr_torrent* raw_torrent)
    : torrent_(torrent)
    , raw_torrent_(raw_torrent)
{
    if (raw_torrent_ != nullptr)
    {
        update_cache();
    }
}

Torrent::ChangeFlags Torrent::Impl::update_cache()
{
    auto result = ChangeFlags();

    auto const* const stats = tr_torrentStat(raw_torrent_);
    g_return_val_if_fail(stats != nullptr, Torrent::ChangeFlags());

    auto seed_ratio = 0.0;
    auto const has_seed_ratio = tr_torrentGetSeedRatio(raw_torrent_, &seed_ratio);

    update_cache_value(cache_.name, tr_torrentName(raw_torrent_), result, ChangeFlag::NAME);
    update_cache_value(cache_.speed_up, stats->pieceUploadSpeed_KBps, 0.01F, result, ChangeFlag::SPEED_UP);
    update_cache_value(cache_.speed_down, stats->pieceDownloadSpeed_KBps, 0.01F, result, ChangeFlag::SPEED_DOWN);
    update_cache_value(cache_.active_peers_up, stats->peersGettingFromUs, result, ChangeFlag::ACTIVE_PEERS_UP);
    update_cache_value(
        cache_.active_peers_down,
        stats->peersSendingToUs + stats->webseedsSendingToUs,
        result,
        ChangeFlag::ACTIVE_PEERS_DOWN);
    update_cache_value(cache_.recheck_progress, Percents(stats->recheckProgress), result, ChangeFlag::RECHECK_PROGRESS);
    update_cache_value(
        cache_.active,
        stats->peersSendingToUs > 0 || stats->peersGettingFromUs > 0 || stats->activity == TR_STATUS_CHECK,
        result,
        ChangeFlag::ACTIVE);
    update_cache_value(cache_.activity, stats->activity, result, ChangeFlag::ACTIVITY);
    update_cache_value(
        cache_.activity_percent_done,
        Percents(std::clamp(
            stats->activity == TR_STATUS_SEED && has_seed_ratio ? stats->seedRatioPercentDone : stats->percentDone,
            0.0F,
            1.0F)),
        result,
        ChangeFlag::PERCENT_DONE);
    update_cache_value(cache_.finished, stats->finished, result, ChangeFlag::FINISHED);
    update_cache_value(cache_.priority, tr_torrentGetPriority(raw_torrent_), result, ChangeFlag::PRIORITY);
    update_cache_value(cache_.queue_position, stats->queuePosition, result, ChangeFlag::QUEUE_POSITION);
    update_cache_value(cache_.trackers, build_torrent_trackers_hash(*raw_torrent_), result, ChangeFlag::TRACKERS);
    update_cache_value(cache_.error_code, stats->error, result, ChangeFlag::ERROR_CODE);
    update_cache_value(cache_.error_message, stats->errorString, result, ChangeFlag::ERROR_MESSAGE);
    update_cache_value(
        cache_.active_peer_count,
        stats->peersSendingToUs + stats->peersGettingFromUs + stats->webseedsSendingToUs,
        result,
        ChangeFlag::ACTIVE_PEER_COUNT);
    update_cache_value(cache_.mime_type, get_mime_type(*raw_torrent_), result, ChangeFlag::MIME_TYPE);
    update_cache_value(cache_.has_metadata, tr_torrentHasMetadata(raw_torrent_), result, ChangeFlag::HAS_METADATA);
    update_cache_value(cache_.stalled, stats->isStalled, result, ChangeFlag::STALLED);
    update_cache_value(cache_.ratio, stats->ratio, 0.01F, result, ChangeFlag::RATIO);

    update_cache_value(cache_.added_date, stats->addedDate, result, ChangeFlag::ADDED_DATE);
    update_cache_value(cache_.eta, stats->eta, result, ChangeFlag::ETA);
    update_cache_value(cache_.percent_complete, Percents(stats->percentComplete), result, ChangeFlag::PERCENT_COMPLETE);
    update_cache_value(
        cache_.seed_ratio_percent_done,
        Percents(stats->seedRatioPercentDone),
        result,
        ChangeFlag::SEED_RATIO_PERCENT_DONE);
    update_cache_value(cache_.total_size, tr_torrentTotalSize(raw_torrent_), result, ChangeFlag::TOTAL_SIZE);

    update_cache_value(cache_.has_seed_ratio, has_seed_ratio, result, ChangeFlag::LONG_PROGRESS);
    update_cache_value(cache_.have_unchecked, stats->haveUnchecked, result, ChangeFlag::LONG_PROGRESS);
    update_cache_value(cache_.have_valid, stats->haveValid, result, ChangeFlag::LONG_PROGRESS);
    update_cache_value(cache_.left_until_done, stats->leftUntilDone, result, ChangeFlag::LONG_PROGRESS);
    update_cache_value(cache_.percent_done, Percents(stats->percentDone), result, ChangeFlag::LONG_PROGRESS);
    update_cache_value(cache_.seed_ratio, static_cast<float>(seed_ratio), 0.01F, result, ChangeFlag::LONG_PROGRESS);
    update_cache_value(cache_.size_when_done, stats->sizeWhenDone, result, ChangeFlag::LONG_PROGRESS);
    update_cache_value(cache_.uploaded_ever, stats->uploadedEver, result, ChangeFlag::LONG_PROGRESS);

    update_cache_value(
        cache_.metadata_percent_complete,
        Percents(stats->metadataPercentComplete),
        result,
        ChangeFlag::LONG_STATUS);
    update_cache_value(cache_.peers_connected, stats->peersConnected, result, ChangeFlag::LONG_STATUS);
    update_cache_value(cache_.peers_getting_from_us, stats->peersGettingFromUs, result, ChangeFlag::LONG_STATUS);
    update_cache_value(cache_.peers_sending_to_us, stats->peersSendingToUs, result, ChangeFlag::LONG_STATUS);
    update_cache_value(cache_.webseeds_sending_to_us, stats->webseedsSendingToUs, result, ChangeFlag::LONG_STATUS);

    if (result.test(ChangeFlag::NAME))
    {
        cache_.name_collated = fmt::format("{}\t{}", cache_.name.lowercase(), tr_torrentView(raw_torrent_).hash_string);
    }

    return result;
}

void Torrent::Impl::notify_property_changes(ChangeFlags changes) const
{
    // Updating the model triggers off resort/refresh, so don't notify unless something's actually changed
    if (changes.none())
    {
        return;
    }

    gtr_object_notify_emit(torrent_);
}

void Torrent::Impl::get_value(int column, Glib::ValueBase& value) const
{
    static auto const& columns = get_columns();

    if (column == columns.self.index())
    {
        column_value_cast(value, columns.self).set(&torrent_);
    }
    else if (column == columns.name_collated.index())
    {
        column_value_cast(value, columns.name_collated).set(cache_.name_collated);
    }
}

Glib::RefPtr<Gio::Icon> Torrent::Impl::get_icon() const
{
    return gtr_get_mime_type_icon(cache_.mime_type);
}

Glib::ustring Torrent::Impl::get_short_status_text() const
{
    switch (cache_.activity)
    {
    case TR_STATUS_STOPPED:
        return cache_.finished ? _("Finished") : _("Paused");

    case TR_STATUS_CHECK_WAIT:
        return _("Queued for verification");

    case TR_STATUS_DOWNLOAD_WAIT:
        return _("Queued for download");

    case TR_STATUS_SEED_WAIT:
        return _("Queued for seeding");

    case TR_STATUS_CHECK:
        return fmt::format(
            // xgettext:no-c-format
            _("Verifying local data ({percent_done}% tested)"),
            fmt::arg("percent_done", cache_.recheck_progress.to_string()));

    case TR_STATUS_DOWNLOAD:
    case TR_STATUS_SEED:
        return fmt::format(
            FMT_STRING("{:s} {:s}"),
            get_short_transfer_text(),
            fmt::format(_("Ratio: {ratio}"), fmt::arg("ratio", tr_strlratio(cache_.ratio))));

    default:
        return {};
    }
}

Glib::ustring Torrent::Impl::get_long_progress_text() const
{
    Glib::ustring gstr;

    bool const isDone = cache_.left_until_done == 0;
    auto const haveTotal = cache_.have_unchecked + cache_.have_valid;
    bool const isSeed = cache_.have_valid >= cache_.total_size;

    if (!isDone) // downloading
    {
        // 50 MB of 200 MB (25%)
        gstr += fmt::format(
            _("{current_size} of {complete_size} ({percent_done}%)"),
            fmt::arg("current_size", tr_strlsize(haveTotal)),
            fmt::arg("complete_size", tr_strlsize(cache_.size_when_done)),
            fmt::arg("percent_done", cache_.percent_done.to_string()));
    }
    else if (!isSeed && cache_.has_seed_ratio) // partial seed, seed ratio
    {
        // 50 MB of 200 MB (25%), uploaded 30 MB (Ratio: X%, Goal: Y%)
        gstr += fmt::format(
            // xgettext:no-c-format
            _("{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio}, Goal: {seed_ratio})"),
            fmt::arg("current_size", tr_strlsize(haveTotal)),
            fmt::arg("complete_size", tr_strlsize(cache_.total_size)),
            fmt::arg("percent_complete", cache_.percent_complete.to_string()),
            fmt::arg("uploaded_size", tr_strlsize(cache_.uploaded_ever)),
            fmt::arg("ratio", tr_strlratio(cache_.ratio)),
            fmt::arg("seed_ratio", tr_strlratio(cache_.seed_ratio)));
    }
    else if (!isSeed) // partial seed, no seed ratio
    {
        gstr += fmt::format(
            // xgettext:no-c-format
            _("{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio})"),
            fmt::arg("current_size", tr_strlsize(haveTotal)),
            fmt::arg("complete_size", tr_strlsize(cache_.total_size)),
            fmt::arg("percent_complete", cache_.percent_complete.to_string()),
            fmt::arg("uploaded_size", tr_strlsize(cache_.uploaded_ever)),
            fmt::arg("ratio", tr_strlratio(cache_.ratio)));
    }
    else if (cache_.has_seed_ratio) // seed, seed ratio
    {
        gstr += fmt::format(
            _("{complete_size}, uploaded {uploaded_size} (Ratio: {ratio}, Goal: {seed_ratio})"),
            fmt::arg("complete_size", tr_strlsize(cache_.total_size)),
            fmt::arg("uploaded_size", tr_strlsize(cache_.uploaded_ever)),
            fmt::arg("ratio", tr_strlratio(cache_.ratio)),
            fmt::arg("seed_ratio", tr_strlratio(cache_.seed_ratio)));
    }
    else // seed, no seed ratio
    {
        gstr += fmt::format(
            _("{complete_size}, uploaded {uploaded_size} (Ratio: {ratio})"),
            fmt::arg("complete_size", tr_strlsize(cache_.total_size)),
            fmt::arg("uploaded_size", tr_strlsize(cache_.uploaded_ever)),
            fmt::arg("ratio", tr_strlratio(cache_.ratio)));
    }

    // add time remaining when applicable
    if (cache_.activity == TR_STATUS_DOWNLOAD || (cache_.has_seed_ratio && cache_.activity == TR_STATUS_SEED))
    {
        gstr += " - ";

        if (cache_.eta < 0)
        {
            gstr += _("Remaining time unknown");
        }
        else
        {
            gstr += tr_format_time_left(cache_.eta);
        }
    }

    return gstr;
}

Glib::ustring Torrent::Impl::get_long_status_text() const
{
    auto status_str = get_error_text();
    if (status_str.empty())
    {
        status_str = get_activity_text();
    }

    switch (cache_.activity)
    {
    case TR_STATUS_CHECK_WAIT:
    case TR_STATUS_CHECK:
    case TR_STATUS_DOWNLOAD_WAIT:
    case TR_STATUS_SEED_WAIT:
    case TR_STATUS_STOPPED:
        break;

    default:
        if (auto const buf = get_short_transfer_text(); !std::empty(buf))
        {
            status_str += fmt::format(FMT_STRING(" - {:s}"), buf);
        }
    }

    return status_str;
}

Glib::ustring Torrent::Impl::get_short_transfer_text() const
{
    if (cache_.has_metadata && cache_.active_peers_down > 0)
    {
        return fmt::format(
            _("{download_speed} ▼  {upload_speed} ▲"),
            fmt::arg("upload_speed", tr_formatter_speed_KBps(cache_.speed_up)),
            fmt::arg("download_speed", tr_formatter_speed_KBps(cache_.speed_down)));
    }

    if (cache_.has_metadata && cache_.active_peers_up > 0)
    {
        return fmt::format(_("{upload_speed} ▲"), fmt::arg("upload_speed", tr_formatter_speed_KBps(cache_.speed_up)));
    }

    if (cache_.stalled)
    {
        return _("Stalled");
    }

    return {};
}

Glib::ustring Torrent::Impl::get_error_text() const
{
    switch (cache_.error_code)
    {
    case TR_STAT_TRACKER_WARNING:
        return fmt::format(_("Tracker warning: '{warning}'"), fmt::arg("warning", cache_.error_message));

    case TR_STAT_TRACKER_ERROR:
        return fmt::format(_("Tracker Error: '{error}'"), fmt::arg("error", cache_.error_message));

    case TR_STAT_LOCAL_ERROR:
        return fmt::format(_("Local error: '{error}'"), fmt::arg("error", cache_.error_message));

    default:
        return {};
    }
}

Glib::ustring Torrent::Impl::get_activity_text() const
{
    switch (cache_.activity)
    {
    case TR_STATUS_STOPPED:
    case TR_STATUS_CHECK_WAIT:
    case TR_STATUS_CHECK:
    case TR_STATUS_DOWNLOAD_WAIT:
    case TR_STATUS_SEED_WAIT:
        return get_short_status_text();

    case TR_STATUS_DOWNLOAD:
        if (!cache_.has_metadata)
        {
            return fmt::format(
                ngettext(
                    // xgettext:no-c-format
                    "Downloading metadata from {active_count} connected peer ({percent_done}% done)",
                    "Downloading metadata from {active_count} connected peers ({percent_done}% done)",
                    cache_.peers_connected),
                fmt::arg("active_count", cache_.peers_connected),
                fmt::arg("percent_done", cache_.metadata_percent_complete.to_string()));
        }

        if (cache_.peers_sending_to_us != 0 && cache_.webseeds_sending_to_us != 0)
        {
            return fmt::format(
                ngettext(
                    "Downloading from {active_count} of {connected_count} connected peer and webseed",
                    "Downloading from {active_count} of {connected_count} connected peers and webseeds",
                    cache_.peers_connected + cache_.webseeds_sending_to_us),
                fmt::arg("active_count", cache_.peers_sending_to_us + cache_.webseeds_sending_to_us),
                fmt::arg("connected_count", cache_.peers_connected + cache_.webseeds_sending_to_us));
        }

        if (cache_.webseeds_sending_to_us != 0)
        {
            return fmt::format(
                ngettext(
                    "Downloading from {active_count} webseed",
                    "Downloading from {active_count} webseeds",
                    cache_.webseeds_sending_to_us),
                fmt::arg("active_count", cache_.webseeds_sending_to_us));
        }

        return fmt::format(
            ngettext(
                "Downloading from {active_count} of {connected_count} connected peer",
                "Downloading from {active_count} of {connected_count} connected peers",
                cache_.peers_connected),
            fmt::arg("active_count", cache_.peers_sending_to_us),
            fmt::arg("connected_count", cache_.peers_connected));

    case TR_STATUS_SEED:
        return fmt::format(
            ngettext(
                "Seeding to {active_count} of {connected_count} connected peer",
                "Seeding to {active_count} of {connected_count} connected peers",
                cache_.peers_connected),
            fmt::arg("active_count", cache_.peers_getting_from_us),
            fmt::arg("connected_count", cache_.peers_connected));

    default:
        g_assert_not_reached();
        return {};
    }
}

Torrent::Torrent()
    : Glib::ObjectBase(typeid(Torrent))
{
}

Torrent::Torrent(tr_torrent* torrent)
    : Glib::ObjectBase(typeid(Torrent))
    , impl_(std::make_unique<Impl>(*this, torrent))
{
    g_assert(torrent != nullptr);
}

Glib::ustring const& Torrent::get_name_collated() const
{
    return impl_->get_cache().name_collated;
}

tr_torrent_id_t Torrent::get_id() const
{
    return tr_torrentId(impl_->get_raw_torrent());
}

tr_torrent& Torrent::get_underlying() const
{
    return *impl_->get_raw_torrent();
}

float Torrent::get_speed_up() const
{
    return impl_->get_cache().speed_up;
}

float Torrent::get_speed_down() const
{
    return impl_->get_cache().speed_down;
}

int Torrent::get_active_peers_up() const
{
    return impl_->get_cache().active_peers_up;
}

int Torrent::get_active_peers_down() const
{
    return impl_->get_cache().active_peers_down;
}

Percents Torrent::get_recheck_progress() const
{
    return impl_->get_cache().recheck_progress;
}

bool Torrent::get_active() const
{
    return impl_->get_cache().active;
}

tr_torrent_activity Torrent::get_activity() const
{
    return impl_->get_cache().activity;
}

bool Torrent::get_finished() const
{
    return impl_->get_cache().finished;
}

tr_priority_t Torrent::get_priority() const
{
    return impl_->get_cache().priority;
}

size_t Torrent::get_queue_position() const
{
    return impl_->get_cache().queue_position;
}

unsigned int Torrent::get_trackers() const
{
    return impl_->get_cache().trackers;
}

int Torrent::get_error_code() const
{
    return impl_->get_cache().error_code;
}

Glib::ustring const& Torrent::get_error_message() const
{
    return impl_->get_cache().error_message;
}

int Torrent::get_active_peer_count() const
{
    return impl_->get_cache().active_peer_count;
}

uint64_t Torrent::get_total_size() const
{
    return impl_->get_cache().total_size;
}

float Torrent::get_ratio() const
{
    return impl_->get_cache().ratio;
}

time_t Torrent::get_eta() const
{
    return impl_->get_cache().eta;
}

time_t Torrent::get_added_date() const
{
    return impl_->get_cache().added_date;
}

Percents Torrent::get_percent_complete() const
{
    return impl_->get_cache().percent_complete;
}

Percents Torrent::get_seed_ratio_percent_done() const
{
    return impl_->get_cache().seed_ratio_percent_done;
}

Glib::RefPtr<Gio::Icon> Torrent::get_icon() const
{
    return impl_->get_icon();
}

Glib::ustring Torrent::get_name() const
{
    return impl_->get_cache().name;
}

Percents Torrent::get_percent_done() const
{
    return impl_->get_cache().activity_percent_done;
}

Glib::ustring Torrent::get_short_status_text() const
{
    return impl_->get_short_status_text();
}

Glib::ustring Torrent::get_long_progress_text() const
{
    return impl_->get_long_progress_text();
}

Glib::ustring Torrent::get_long_status_text() const
{
    return impl_->get_long_status_text();
}

bool Torrent::get_sensitive() const
{
    return impl_->get_cache().activity != TR_STATUS_STOPPED;
}

Torrent::ChangeFlags Torrent::update()
{
    auto result = impl_->update_cache();
    impl_->notify_property_changes(result);
    return result;
}

Glib::RefPtr<Torrent> Torrent::create(tr_torrent* torrent)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new Torrent(torrent));
}

Torrent::Columns const& Torrent::get_columns()
{
    static Columns const columns;
    return columns;
}

int Torrent::get_item_id(Glib::RefPtr<Glib::ObjectBase const> const& item)
{
    if (auto const torrent = gtr_ptr_dynamic_cast<Torrent const>(item); torrent != nullptr)
    {
        return torrent->get_id();
    }

    return 0;
}

void Torrent::get_item_value(Glib::RefPtr<Glib::ObjectBase const> const& item, int column, Glib::ValueBase& value)
{
    if (auto const torrent = gtr_ptr_dynamic_cast<Torrent const>(item); torrent != nullptr)
    {
        torrent->impl_->get_value(column, value);
    }
}

int Torrent::compare_by_id(Glib::RefPtr<Torrent const> const& lhs, Glib::RefPtr<Torrent const> const& rhs)
{
    return gtr_compare_generic(lhs->get_id(), rhs->get_id());
}

bool Torrent::less_by_id(Glib::RefPtr<Torrent const> const& lhs, Glib::RefPtr<Torrent const> const& rhs)
{
    return lhs->get_id() < rhs->get_id();
}
