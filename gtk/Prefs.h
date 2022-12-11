// This file copyright (C) 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // int64_t
#include <list>
#include <string>
#include <string_view>

#include <glibmm.h>
#include <gtkmm.h>

#include <libtransmission/transmission.h> /* tr_variant, tr_session */
#include <libtransmission/quark.h>

namespace ClientPrefs
{

using namespace std::string_view_literals;

namespace Key
{

inline char const* const BlocklistUpdatesEnabled = "blocklist-updates-enabled";
inline char const* const CompactView = "compact-view";
inline char const* const InhibitDesktopHibernation = "inhibit-desktop-hibernation";
inline char const* const OpenDialogDir = "open-dialog-dir";
inline char const* const ShowBackupTrackers = "show-backup-trackers";
inline char const* const ShowExtraPeerDetails = "show-extra-peer-details";
inline char const* const ShowFilterbar = "show-filterbar";
inline char const* const ShowNotificationAreaIcon = "show-notification-area-icon";
inline char const* const ShowOptionsWindow = "show-options-window";
inline char const* const ShowStatusbar = "show-statusbar";
inline char const* const ShowToolbar = "show-toolbar";
inline char const* const ShowTrackerScrapes = "show-tracker-scrapes";
inline char const* const SortMode = "sort-mode";
inline char const* const SortReversed = "sort-reversed";
inline char const* const StatusbarStats = "statusbar-stats";
inline char const* const TorrentAddedNotificationEnabled = "torrent-added-notification-enabled";
inline char const* const TorrentCompleteNotificationEnabled = "torrent-complete-notification-enabled";
inline char const* const TorrentCompleteSoundCommand = "torrent-complete-sound-command";
inline char const* const TorrentCompleteSoundEnabled = "torrent-complete-sound-enabled";
inline char const* const TrashCanEnabled = "trash-can-enabled";
inline char const* const UserHasGivenInformedConsent = "user-has-given-informed-consent";
inline char const* const WatchDir = "watch-dir";
inline char const* const WatchDirEnabled = "watch-dir-enabled";

} // namespace Key

namespace WindowKeyPrefix
{

inline auto constexpr Details = "details-window"sv;
inline auto constexpr Main = "main-window"sv;
inline auto constexpr MessageLog = "message-log-window"sv;

} // namespace WindowKeyPrefix

namespace WindowKeySuffix
{

inline auto constexpr Height = "-height"sv;
inline auto constexpr Maximized = "-maximized"sv;
inline auto constexpr Width = "-width"sv;

} // namespace WindowKeySuffix

namespace DirKeyPrefix
{

inline auto constexpr Download = "download-dir"sv;
inline auto constexpr Relocate = "relocate-dir"sv;

} // namespace DirKeyPrefix

namespace DirKeySuffix
{

inline auto constexpr Recents = "-recents"sv;

} // namespace DirKeySuffix

namespace SortMode
{

inline char const* const Activity = "activity";
inline char const* const Age = "age";
inline char const* const Name = "name";
inline char const* const Progress = "progress";
inline char const* const Queue = "queue";
inline char const* const Ratio = "ratio";
inline char const* const Size = "size";
inline char const* const State = "state";
inline char const* const TimeLeft = "time-left";

} // namespace SortMode

namespace StatusMode
{

inline char const* const SessionRatio = "session-ratio";
inline char const* const SessionTransfer = "session-transfer";
inline char const* const TotalRatio = "total-ratio";
inline char const* const TotalTransfer = "total-transfer";

} // namespace StatusMode

void bind_window_state(Gtk::Window& window, Glib::RefPtr<Gio::Settings> const& settings, std::string_view key_prefix);

std::list<std::string> get_recent_dirs(Glib::RefPtr<Gio::Settings> const& settings, std::string_view key_prefix);
void save_recent_dir(Glib::RefPtr<Gio::Settings> const& settings, std::string_view key_prefix, std::string_view dir);

} // namespace ClientPrefs

void gtr_pref_init(std::string_view config_dir);

int64_t gtr_pref_int_get(tr_quark const key);
void gtr_pref_int_set(tr_quark const key, int64_t value);

double gtr_pref_double_get(tr_quark const key);
void gtr_pref_double_set(tr_quark const key, double value);

bool gtr_pref_flag_get(tr_quark const key);
void gtr_pref_flag_set(tr_quark const key, bool value);

std::string gtr_pref_string_get(tr_quark const key);
void gtr_pref_string_set(tr_quark const key, std::string_view value);

void gtr_pref_save(tr_session*);
tr_variant* gtr_pref_get_all();
