/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <gtk/gtk.h>

GtkWidget* gtr_prefs_dialog_new(GtkWindow* parent, GObject* core);

/* if you add a key here,  you /must/ add its
 * default in tr_prefs_init_defaults(void) */

#define PREF_KEY_BLOCKLIST_UPDATES_ENABLED "blocklist-updates-enabled"
#define PREF_KEY_COMPACT_VIEW "compact-view"
#define PREF_KEY_DIR_WATCH_ENABLED "watch-dir-enabled"
#define PREF_KEY_DIR_WATCH "watch-dir"
#define PREF_KEY_FILTERBAR "show-filterbar"
#define PREF_KEY_INHIBIT_HIBERNATION "inhibit-desktop-hibernation"
#define PREF_KEY_MAIN_WINDOW_HEIGHT "main-window-height"
#define PREF_KEY_MAIN_WINDOW_IS_MAXIMIZED "main-window-is-maximized"
#define PREF_KEY_MAIN_WINDOW_WIDTH "main-window-width"
#define PREF_KEY_MAIN_WINDOW_X "main-window-x"
#define PREF_KEY_MAIN_WINDOW_Y "main-window-y"
#define PREF_KEY_OPEN_DIALOG_FOLDER "open-dialog-dir"
#define PREF_KEY_OPTIONS_PROMPT "show-options-window"
#define PREF_KEY_SHOW_BACKUP_TRACKERS "show-backup-trackers"
#define PREF_KEY_SHOW_MORE_PEER_INFO "show-extra-peer-details"
#define PREF_KEY_SHOW_MORE_TRACKER_INFO "show-tracker-scrapes"
#define PREF_KEY_SHOW_TRAY_ICON "show-notification-area-icon"
#define PREF_KEY_SORT_MODE "sort-mode"
#define PREF_KEY_SORT_REVERSED "sort-reversed"
#define PREF_KEY_STATUSBAR "show-statusbar"
#define PREF_KEY_STATUSBAR_STATS "statusbar-stats"
#define PREF_KEY_TOOLBAR "show-toolbar"
#define PREF_KEY_TORRENT_ADDED_NOTIFICATION_ENABLED "torrent-added-notification-enabled"
#define PREF_KEY_TORRENT_COMPLETE_NOTIFICATION_ENABLED "torrent-complete-notification-enabled"
#define PREF_KEY_TORRENT_COMPLETE_SOUND_COMMAND "torrent-complete-sound-command"
#define PREF_KEY_TORRENT_COMPLETE_SOUND_ENABLED "torrent-complete-sound-enabled"
#define PREF_KEY_TRASH_CAN_ENABLED "trash-can-enabled"
#define PREF_KEY_USER_HAS_GIVEN_INFORMED_CONSENT "user-has-given-informed-consent"

enum
{
    MAIN_WINDOW_REFRESH_INTERVAL_SECONDS = 2,
    SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS = 2
};
