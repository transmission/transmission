// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "TorrentFilter.h"

#include "FilterBase.hh"
#include "Utils.h"

#include "libtransmission/transmission.h"

#include "lib/base/tr-macros.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

TorrentFilter::TorrentFilter()
    : Glib::ObjectBase(typeid(TorrentFilter))
{
}

void TorrentFilter::set_mode(ShowMode const mode)
{
    if (show_mode_ == mode)
    {
        return;
    }

    auto change = Change::DIFFERENT;

    if (show_mode_ == ShowMode::ShowAll)
    {
        change = Change::MORE_STRICT;
    }
    else if (mode == ShowMode::ShowAll)
    {
        change = Change::LESS_STRICT;
    }

    show_mode_ = mode;
    changed(change);
}

void TorrentFilter::set_tracker(Tracker type, Glib::ustring const& host)
{
    if (tracker_type_ == type && tracker_host_ == host)
    {
        return;
    }

    auto change = Change::DIFFERENT;
    if (tracker_type_ != type)
    {
        if (tracker_type_ == Tracker::ALL)
        {
            change = Change::MORE_STRICT;
        }
        else if (type == Tracker::ALL)
        {
            change = Change::LESS_STRICT;
        }
    }
    else // tracker_host_ != host
    {
        if (tracker_host_.empty() || host.find(tracker_host_) != Glib::ustring::npos)
        {
            change = Change::MORE_STRICT;
        }
        else if (host.empty() || tracker_host_.find(host) != Glib::ustring::npos)
        {
            change = Change::LESS_STRICT;
        }
    }

    tracker_type_ = type;
    tracker_host_ = host;
    changed(change);
}

void TorrentFilter::set_text(Glib::ustring const& text)
{
    auto const normalized_text = gtr_str_strip(text.casefold());
    if (text_ == normalized_text)
    {
        return;
    }

    auto change = Change::DIFFERENT;
    if (text_.empty() || normalized_text.find(text_) != Glib::ustring::npos)
    {
        change = Change::MORE_STRICT;
    }
    else if (normalized_text.empty() || text_.find(normalized_text) != Glib::ustring::npos)
    {
        change = Change::LESS_STRICT;
    }

    text_ = normalized_text;
    changed(change);
}

bool TorrentFilter::match_mode(Torrent const& torrent) const
{
    return match_mode(torrent, show_mode_);
}

bool TorrentFilter::match_tracker(Torrent const& torrent) const
{
    return match_tracker(torrent, tracker_type_, tracker_host_);
}

bool TorrentFilter::match_text(Torrent const& torrent) const
{
    return match_text(torrent, text_);
}

bool TorrentFilter::match(Torrent const& torrent) const
{
    return match_mode(torrent) && match_tracker(torrent) && match_text(torrent);
}

bool TorrentFilter::matches_all() const
{
    return show_mode_ == ShowMode::ShowAll && tracker_type_ == Tracker::ALL && text_.empty();
}

void TorrentFilter::update(Torrent::ChangeFlags changes)
{
    using Flag = Torrent::ChangeFlag;

    bool refilter_needed = false;

    if (show_mode_ != ShowMode::ShowAll)
    {
        static auto TR_CONSTEXPR23 ShowModeFlags = std::array<std::pair<ShowMode, Torrent::ChangeFlags>, 7U>{ {
            { ShowMode::ShowActive, Flag::ACTIVE_PEER_COUNT | Flag::ACTIVITY },
            { ShowMode::ShowDownloading, Flag::ACTIVITY },
            { ShowMode::ShowError, Flag::ERROR_CODE },
            { ShowMode::ShowFinished, Flag::FINISHED },
            { ShowMode::ShowPaused, Flag::ACTIVITY },
            { ShowMode::ShowSeeding, Flag::ACTIVITY },
            { ShowMode::ShowVerifying, Flag::ACTIVITY },
        } };

        auto const iter = std::ranges::find_if(ShowModeFlags, [key = show_mode_](auto const& row) { return row.first == key; });
        refilter_needed = iter != std::ranges::end(ShowModeFlags) && changes.test(iter->second);
    }

    if (!refilter_needed)
    {
        refilter_needed = tracker_type_ != Tracker::ALL && changes.test(Flag::TRACKERS);
    }

    if (!refilter_needed)
    {
        refilter_needed = !text_.empty() && changes.test(Flag::NAME);
    }

    if (refilter_needed)
    {
        changed(Change::DIFFERENT);
    }
}

Glib::RefPtr<TorrentFilter> TorrentFilter::create()
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new TorrentFilter());
}

bool TorrentFilter::match_mode(Torrent const& torrent, ShowMode const mode)
{
    auto activity = tr_torrent_activity();

    switch (mode)
    {
    case ShowMode::ShowAll:
        return true;

    case ShowMode::ShowDownloading:
        activity = torrent.get_activity();
        return activity == TR_STATUS_DOWNLOAD || activity == TR_STATUS_DOWNLOAD_WAIT;

    case ShowMode::ShowSeeding:
        activity = torrent.get_activity();
        return activity == TR_STATUS_SEED || activity == TR_STATUS_SEED_WAIT;

    case ShowMode::ShowActive:
        return torrent.get_active_peer_count() > 0 || torrent.get_activity() == TR_STATUS_CHECK;

    case ShowMode::ShowPaused:
        return torrent.get_activity() == TR_STATUS_STOPPED;

    case ShowMode::ShowFinished:
        return torrent.get_finished();

    case ShowMode::ShowVerifying:
        activity = torrent.get_activity();
        return activity == TR_STATUS_CHECK || activity == TR_STATUS_CHECK_WAIT;

    case ShowMode::ShowError:
        return torrent.get_error_code() != tr_stat::Error::Ok;
    }

    g_assert_not_reached();
    return true;
}

bool TorrentFilter::match_tracker(Torrent const& torrent, Tracker type, Glib::ustring const& host)
{
    if (type == Tracker::ALL)
    {
        return true;
    }

    g_assert(type == Tracker::HOST);

    auto const& raw_torrent = torrent.get_underlying();

    for (auto i = size_t{ 0 }, n = tr_torrentTrackerCount(&raw_torrent); i < n; ++i)
    {
        if (auto const tracker = tr_torrentTracker(&raw_torrent, i); std::data(tracker.sitename) == host)
        {
            return true;
        }
    }

    return false;
}

bool TorrentFilter::match_text(Torrent const& torrent, Glib::ustring const& text)
{
    bool ret = false;

    if (text.empty())
    {
        ret = true;
    }
    else
    {
        auto const& raw_torrent = torrent.get_underlying();

        /* test the torrent name... */
        ret = torrent.get_name().casefold().find(text) != Glib::ustring::npos;

        /* test the files... */
        for (auto i = size_t{ 0 }, n = tr_torrentFileCount(&raw_torrent); i < n && !ret; ++i)
        {
            ret = Glib::ustring(tr_torrentFile(&raw_torrent, i).name).casefold().find(text) != Glib::ustring::npos;
        }
    }

    return ret;
}
