// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "TorrentFilter.h"

#include "FilterBase.hh"
#include "Utils.h"

#include <libtransmission/transmission.h>

TorrentFilter::TorrentFilter()
    : Glib::ObjectBase(typeid(TorrentFilter))
{
}

void TorrentFilter::set_activity(Activity type)
{
    if (activity_type_ == type)
    {
        return;
    }

    auto change = Change::DIFFERENT;
    if (activity_type_ == Activity::ALL)
    {
        change = Change::MORE_STRICT;
    }
    else if (type == Activity::ALL)
    {
        change = Change::LESS_STRICT;
    }

    activity_type_ = type;
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

bool TorrentFilter::match_activity(Torrent const& torrent) const
{
    return match_activity(torrent, activity_type_);
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
    return match_activity(torrent) && match_tracker(torrent) && match_text(torrent);
}

bool TorrentFilter::matches_all() const
{
    return activity_type_ == Activity::ALL && tracker_type_ == Tracker::ALL && text_.empty();
}

void TorrentFilter::update(Torrent::ChangeFlags changes)
{
    using Flag = Torrent::ChangeFlag;

    bool refilter_needed = false;

    if (activity_type_ != Activity::ALL)
    {
        static auto const activity_flags = std::map<Activity, Torrent::ChangeFlags>({
            { Activity::DOWNLOADING, Flag::ACTIVITY },
            { Activity::SEEDING, Flag::ACTIVITY },
            { Activity::ACTIVE, Flag::ACTIVE_PEER_COUNT | Flag::ACTIVITY },
            { Activity::PAUSED, Flag::ACTIVITY },
            { Activity::FINISHED, Flag::FINISHED },
            { Activity::VERIFYING, Flag::ACTIVITY },
            { Activity::ERROR, Flag::ERROR_CODE },
        });

        auto const activity_flags_it = activity_flags.find(activity_type_);
        refilter_needed = activity_flags_it != activity_flags.end() && changes.test(activity_flags_it->second);
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

bool TorrentFilter::match_activity(Torrent const& torrent, Activity type)
{
    auto activity = tr_torrent_activity();

    switch (type)
    {
    case Activity::ALL:
        return true;

    case Activity::DOWNLOADING:
        activity = torrent.get_activity();
        return activity == TR_STATUS_DOWNLOAD || activity == TR_STATUS_DOWNLOAD_WAIT;

    case Activity::SEEDING:
        activity = torrent.get_activity();
        return activity == TR_STATUS_SEED || activity == TR_STATUS_SEED_WAIT;

    case Activity::ACTIVE:
        return torrent.get_active_peer_count() > 0 || torrent.get_activity() == TR_STATUS_CHECK;

    case Activity::PAUSED:
        return torrent.get_activity() == TR_STATUS_STOPPED;

    case Activity::FINISHED:
        return torrent.get_finished();

    case Activity::VERIFYING:
        activity = torrent.get_activity();
        return activity == TR_STATUS_CHECK || activity == TR_STATUS_CHECK_WAIT;

    case Activity::ERROR:
        return torrent.get_error_code() != 0;

    default:
        g_assert_not_reached();
        return true;
    }
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
