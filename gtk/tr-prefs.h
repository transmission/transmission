/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_PREFS_H
#define TR_PREFS_H

#include <gtk/gtk.h>

GtkWidget * tr_prefs_dialog_new( GObject *   core,
                                 GtkWindow * parent );

/* if you add a key here,  you /must/ add its
 * default in tr_prefs_init_defaults( void ) */

#define PREF_KEY_OPTIONS_PROMPT             "show-options-window"
#define PREF_KEY_OPEN_DIALOG_FOLDER         "open-dialog-dir"
#define PREF_KEY_INHIBIT_HIBERNATION        "inhibit-desktop-hibernation"
#define PREF_KEY_DIR_WATCH                  "watch-dir"
#define PREF_KEY_DIR_WATCH_ENABLED          "watch-dir-enabled"
#define PREF_KEY_SHOW_TRAY_ICON             "show-notification-area-icon"
#define PREF_KEY_SHOW_DESKTOP_NOTIFICATION  "show-desktop-notification"
#define PREF_KEY_SHOW_MORE_TRACKER_INFO     "show-tracker-scrapes"
#define PREF_KEY_SHOW_BACKUP_TRACKERS       "show-backup-trackers"
#define PREF_KEY_START                      "start-added-torrents"
#define PREF_KEY_TRASH_ORIGINAL             "trash-original-torrent-files"
#define PREF_KEY_ASKQUIT                    "prompt-before-exit"
#define PREF_KEY_SORT_MODE                  "sort-mode"
#define PREF_KEY_SORT_REVERSED              "sort-reversed"
#define PREF_KEY_FILTER_MODE                "filter-mode"
#define PREF_KEY_MINIMAL_VIEW               "minimal-view"
#define PREF_KEY_FILTERBAR                  "show-filterbar"
#define PREF_KEY_STATUSBAR                  "show-statusbar"
#define PREF_KEY_STATUSBAR_STATS            "statusbar-stats"
#define PREF_KEY_TOOLBAR                    "show-toolbar"
#define PREF_KEY_BLOCKLIST_UPDATES_ENABLED  "blocklist-updates-enabled"
#define PREF_KEY_MAIN_WINDOW_LAYOUT_ORDER   "main-window-layout-order"
#define PREF_KEY_MAIN_WINDOW_HEIGHT         "main-window-height"
#define PREF_KEY_MAIN_WINDOW_WIDTH          "main-window-width"
#define PREF_KEY_MAIN_WINDOW_X              "main-window-x"
#define PREF_KEY_MAIN_WINDOW_Y              "main-window-y"
#define PREF_KEY_MAIN_WINDOW_IS_MAXIMIZED   "main-window-is-maximized"

#endif
