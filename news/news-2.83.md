## [Transmission 2.83](https://trac.transmissionbt.com/query?milestone=2.83&group=component&order=severity) (2014-05-18)
### All Platforms
 * Licensing change: the GNU GPLv2 code can now be used under GNU GPL v2 or v3
 * Fix network hanging issues that could occur when both UTP and DHT were enabled
 * Fix 2.82 file descriptor leak when importing a blocklist
 * Disallow torrents that contain "/../" in the path
 * Fix 2.82 bug that didn't retain peers between sessions
 * Fix potential dangling memory error in UDP tracker DNS lookups
 * Remember a torrent's "queued" state between Transmission sessions
 * Updated third party libraries: DHT updated to v0.22; miniupnpc updated to v1.9
 * Autoconf script fixes: better detection of ccache, minupnpc
 * Fix the X-Transmission-Session-Id header to be valid with the SPDY protocol
 * Fix thread safety bugs in the tr_list datatype
 * When determining free disk space on NetBSD>=6, support its Quota feature
 * Windows portability improvements
### Mac Client
 * Share option in File menu and context menu
 * Show all torrents when the filter bar is hidden
 * Show zero-byte files correctly
 * Coalesce multiple Speed Limit Auto Enabled/Disabled notifications
 * Turkish localization
 * Removed Brazilian Portuguese localization because of lack of localizer (European Portuguese localization remains)
### GTK+ Client
 * Fix threading issue on shutdown
### Qt Client
 * Fix toggle-downloading-by-pressing-spacebar in the file list
 * Fix "Open URL" crash from dangling pointer
 * Support launching downloaded files from inside Transmission
 * On Windows, use native Windows icons
 * Improved network status info and tooltip in the status bar
 * Fix "Open Torrent" dialog crash in Qt 5.2
### Daemon
 * On systemd systems, fix config reloading via 'systemctl reload'
 * Use libevent's event loop
 * Fix discrepancy in curl SSL setup between tr-daemon and tr-remote
 * Fix broken OS X build
### Web Client
 * Support file renaming in the web client
 * Fix incorrect torrent state being displayed for magnet links
 * Make URLs in the torrent's comment field clickable (and sanitize them to prevent cross-scripting)
