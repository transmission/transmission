// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"
#include "Torrent.h"

#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/filter.h>
#else
#include <glibmm/object.h>
#endif

class TorrentFilter : public IF_GTKMM4(Gtk::Filter, Glib::Object)
{
#if !GTKMM_CHECK_VERSION(4, 0, 0)
    enum class Change{
        DIFFERENT,
        LESS_STRICT,
        MORE_STRICT,
    };
#endif

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

    bool match(Torrent const& torrent) const;

    void update(Torrent::ChangeFlags changes);

#if !GTKMM_CHECK_VERSION(4, 0, 0)
    sigc::signal<void()>& signal_changed();
#endif

    static Glib::RefPtr<TorrentFilter> create();

    static bool match_activity(Torrent const& torrent, Activity type);
    static bool match_tracker(Torrent const& torrent, Tracker type, Glib::ustring const& host);
    static bool match_text(Torrent const& torrent, Glib::ustring const& text);

protected:
#if GTKMM_CHECK_VERSION(4, 0, 0)
    bool match_vfunc(Glib::RefPtr<Glib::ObjectBase> const& item) override;
    Match get_strictness_vfunc() override;
#else
    void changed(Change change);
#endif

private:
    TorrentFilter();

private:
    Activity activity_type_ = Activity::ALL;
    Tracker tracker_type_ = Tracker::ALL;
    Glib::ustring tracker_host_;
    Glib::ustring text_;

#if !GTKMM_CHECK_VERSION(4, 0, 0)
    sigc::signal<void()> signal_changed_;
#endif
};
