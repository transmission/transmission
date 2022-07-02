## [Transmission 2.80](https://trac.transmissionbt.com/query?milestone=2.80&group=component&order=severity) (2013-06-25)
### All Platforms
 * Support renaming a transfer's files and folders
 * Remove the most frequent thread locks in libtransmission (ie, fewer beachballs)
 * Show the free disk space available when adding torrent
 * Faster reading and parsing of local data files
 * Better use of the OS's filesystem cache
 * Lengthen the prefetch cache for data sent to peers
 * Other small speedups
 * Replace the previous JSON parser with jsonsl to resolve DFSG licensing issue
 * Fix fails-to-build when compiling with -Werror=format-security
 * Improved unit tests in libtransmission
 * Tarballs are now released only in .xz format
### Mac Client
 * Use VDKQueue for watching for torrent files
### GTK+ Client
 * Simplify the tracker filter pulldown's interface (now matches the Qt client)
 * Synced preferences text & shortcuts
 * Remove deprecated calls to gdk_threads_enter()
 * Silence a handful of console warnings
### Qt Client
 * More efficient updates when receiving information from the server
 * Add an option to play a sound when a torrent finishes downloading
 * Add an option to start up iconified into the notification area
 * Fix an issue with the tray icon preventing hibernation/logout
 * Other CPU speedups
 * Open the correct folder when clicking on single-file torrents
 * Synced preferences text & shortcuts
 * Fix non Latin-1 unit strings
### Daemon
 * Add support for specifying recently-active torrents in transmission-remote
### Web Client
 * Extend the cookie lifespan so that settings like sort order don't get lost
### Utils
 * Support user-defined piece sizes in transmission-create
