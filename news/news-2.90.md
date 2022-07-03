## [Transmission 2.90](https://trac.transmissionbt.com/query?milestone=2.90&group=component&order=severity) (2016-02-28)
### All Platforms
 * Fix renaming torrent files with common prefix
 * Fix some more thread safety bugs in the tr_list datatype
 * Fix infinite loop when removing torrent data
 * Add support for CyaSSL/WolfSSL and PolarSSL cryptographic backends; bump OpenSSL minimum to v0.9.7
 * Initial CMake build system support
 * Many improvements to support Windows builds with MSVS and MinGW; drop XP/2003 support, only Vista and up now
 * Allow building against system UTP and DHT libraries
 * Fix several memory leaks and buffer overflows
 * Support miniupnpc API v14
 * Fix "prefetch-enabled" value type in settings.json (boolean instead of integer)
 * Fix some issues discovered by static analysis (cppcheck, coverity)
 * Fix invalid JSON encoding for non-printable characters
 * Fix multi-threaded locale use when encoding/decoding JSON data
 * Fix encrypted communication with libevent 2.1+
 * Prevent completed pieces modification by webseeds
 * Require absolute paths in RPC requests
 * Fix and unify torrent origin display in GTK+, Qt and web clients
 * Fix crash on session shutdown (evdns_getaddrinfo_cancel)
 * Retry if RPC server fails to bind to specified address
 * Improve error checking on metadata retrieval
 * Improve UTF-8 validity checking (merge changes from LLVM)
 * Don't build transmission-cli by default (it's long deprecated)
### Mac Client
 * UI fixes for OS X 10.9+
 * Trim potential URIs from clipboard
 * Allow downloading files from http servers (not https) on OS X 10.11+
 * Change Sparkle Update URL to use HTTPS instead of HTTP (addresses Sparkle vulnerability)
 * Fix global options popover layout
 * Fix building with Xcode 7+
 * Drop OS X 10.6 support
### GTK+ Client
 * Fix overshoot and undershoot indicators display with GTK+ 3.16+ in main window
 * Don't require DISPLAY if started with `--version` argument
### Qt Client
 * Improve performance in Torrent Properties dialog for torrents with lots of files
 * Prevent entering file renaming mode with mouse double-click
 * Add context menu on files tab of Torrent Properties dialog resembling that of Mac client
 * Remove torrent file from watch directory even if "show options dialog" is not set
 * Use theme-provided icons in system tray and About dialog
 * Fix initial watch directory scan
 * Improve filter bar look and feel; lots of other small visual fixes; RTL layout fixes
 * Show message to the user when duplicate torrent is being added
 * Improve magnets handling in main window
 * Display notifications via tray icon if D-Bus is not available
 * Show notice on top of filtered torrents list; clear whole filter on notice double-click
 * Add proper compiler flags to indicate C++11 use
 * Fix translation files loading
 * Add Chinese (China), German, Indonesian, Italian (Italy), Korean, Polish (Poland), Ukrainian translations; update existing translations
### Daemon
 * Run as service on Windows when in background mode
 * Rework directory watching, add support for native mechanisms on BSD/Darwin (kqueue) and Windows (ReadDirectoryChanges)
 * Don't make assumptions of remote path validity in transmission-remote
### Web Client
 * Content Security Policy enhancements
 * Enable "resume now" for queued torrents
 * Mark appropriate fields in preferences dialog as HTML5 number fields
 * Update to jQuery 1.11.2, jQueryUI 1.11.4; use jQueryUI menus instead of custom ones
