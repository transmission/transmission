// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "FilterBase.h"
#include "Torrent.h"

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

class TorrentFilter : public FilterBase<Torrent>
{
public:
    enum class Activity
    {
        ALL,
        DOWNLOADING,
        SEEDING,
        ACTIVE,
        PAUSED,
        FINISHED,
        VERIFYING,
        ERROR,
    };

    enum class Tracker
    {
        ALL,
        HOST,
    };

public:
    void set_activity(Activity type);
    void set_tracker(Tracker type, Glib::ustring const& host);
    void set_text(Glib::ustring const& text);

    bool match_activity(Torrent const& torrent) const;
    bool match_tracker(Torrent const& torrent) const;
    bool match_text(Torrent const& torrent) const;

    // FilterBase<Torrent>
    bool match(Torrent const& torrent) const override;
    bool matches_all() const override;

    void update(Torrent::ChangeFlags changes);

    static Glib::RefPtr<TorrentFilter> create();

    static bool match_activity(Torrent const& torrent, Activity type);
    static bool match_tracker(Torrent const& torrent, Tracker type, Glib::ustring const& host);
    static bool match_text(Torrent const& torrent, Glib::ustring const& text);

private:
    TorrentFilter();

private:
    Activity activity_type_ = Activity::ALL;
    Tracker tracker_type_ = Tracker::ALL;
    Glib::ustring tracker_host_;
    Glib::ustring text_;
};
