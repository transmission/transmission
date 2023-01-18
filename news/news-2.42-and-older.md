## [Transmission 2.42](https://trac.transmissionbt.com/query?milestone=2.42&group=component&order=severity) (2011-10-19)
### All Platforms
 * Fix error connecting to UDP trackers from big-endian computers
 * Fix RPC error when editing UDP trackers
 * Fix build failure when a C++ compiler is not installed
### Mac
 * Fix a potential crash on 10.5 Leopard
 * Fix bugs with the tracker and file inspector tables
### GTK+
 * Support GTK+ 3.2
 * Fix crasher on systems not running DBus
### Qt
 * Updated Lithuanian translation
### Web Client
 * Fix bug which broke Opera support

## [Transmission 2.41](https://trac.transmissionbt.com/query?milestone=2.41&group=component&order=severity) (2011-10-08)
### Mac
 * Fix crasher on 10.5 Leopard

## [Transmission 2.40](https://trac.transmissionbt.com/query?milestone=2.40&group=component&order=severity) (2011-10-08)
### All Platforms
 * Torrent queuing
 * Improved webseed support
 * Fix crash when removing a magnetized transfer
 * Fix adding transfers over RPC when a subfolder does not exist
 * Other minor fixes
### Mac
 * Lion: Use popovers for the global and per-torrent action menus
 * Lion: Animations in the inspector's file list and the message window
 * Support sorting transfers by size
 * No longer keep track of recently opened torrent files
 * Apply group locations when adding transfers through the web client/RPC
 * Minor interface tweaks and behavior adjustments
### GTK+
 * Add GTK+ 3 support
 * Make popup notification and system sounds system-configurable
 * Add a settings option to hard-delete files instead of using the recycle bin
 * Raise the minimum library requirements for GTK+ to 2.22 and glib to 2.28
### Qt
 * Add popup notification for finished torrents
 * Fix non-UTF-8 display issue in the "New Torrent" dialog
### Daemon
 * SSL support in transmission-remote
### Web Client
 * Speed improvements
 * Add filtering by tracker
 * Allow preference changes on mobile devices
 * Allow compact view on mobile devices
 * Stop ratio functionality
 * Compact view interface improvements
### Utils
 * Fix transmission-edit bug when adding a tracker to a single-tracker torrent
 * Fix transmission-create bug when specifying a directory with a leading "./"

## [Transmission 2.33](https://trac.transmissionbt.com/query?milestone=2.33&group=component&order=severity) (2011-07-20)
### All Platforms
 * Improved webseed support
 * Better support for moving and deleting files
 * Fix "Too many open files" bug
 * Apply blocklists towards DHT communication
 * Fix displayed availability
### Mac
 * Minor Lion interface tweaks
### GTK+
 * Remove deprecated GConf2 dependency
### Qt
 * Fix high CPU issues
 * Fix wrong torrent count on tracker filterbar
 * Update Spanish translation

## [Transmission 2.32](https://trac.transmissionbt.com/query?milestone=2.32&group=component&order=severity) (2011-06-28)
### All Platforms
 * Fix error caused by some "open-file-limit" configuration settings
 * Fix 2.30 problem seeding to some peers
 * Fix bug converting torrent file text contents to UTF-8
 * Better µTP support on systems running uClibc
 * Other small bug fixes
### Mac
 * Improved tabbing behavior
 * Lion compatibility
 * Minor interface tweaks and stability fixes
### GTK+
 * Fix 2.30 error opening torrents from a web browser
 * Remove GNOME desktop proxy support
### Web Client
 * Fix bug when adding torrents
 * Add torrents by info hash in the add dialog
 * Sorting by ratio
 * Allow drag-and-drop to add links

## [Transmission 2.31](https://trac.transmissionbt.com/query?milestone=2.31&group=component&order=severity) (2011-05-17)
### All Platforms
 * This release fixes a 2.30 packaging error

## [Transmission 2.30](https://trac.transmissionbt.com/query?milestone=2.30&group=component&order=severity) (2011-05-16)
### All Platforms
 * µTP support
 * UDP tracker support
 * Multiscrape support
 * Download scarcest pieces first
 * The "lazy bitfield" feature has been superseded by the "Fast Extension" BEP6
 * Scripts are passed the environment
### Mac
 * An Intel Mac is now required
 * Ability to remove all completed (finished seeding) transfers
 * The Web Interface is published over Wide-Area Bonjour
 * Enhanced grouping rules
 * Interface tweaks
### GTK+
 * Added 256 x 256 icon by Andreas Nilsson
 * Register as a magnet link handler in the .desktop file
### Web Client
 * Peer and Network preferences

## [Transmission 2.22](https://trac.transmissionbt.com/query?milestone=2.22&group=component&order=severity) (2011-03-04)
### All Platforms
 * Faster endgame
 * Fix bug where leechers might be disconnected while seeding in some situations
 * Fix 2.20 bug that sometimes showed inaccurate upload/download speeds
 * Support for unsorted blocklists
 * Fix IPv6 DHT
 * Re-add support to automatically close idle peers
 * Fix bug where the resume file did not save the time checked for the last piece
### Mac
 * Fixes for Dutch, German, and Russian localizations
### GTK+
 * Fix setting individual idle seeding time
### Qt
 * Fix loading localizations on Linux
### CLI
 * Fix bandwidth display issue

## [Transmission 2.21](https://trac.transmissionbt.com/query?milestone=2.21&group=component&order=severity) (2011-02-08)
### All Platforms
 * Fix compile error in the 2.20 tarball
### GTK+
 * Several updated translations
### Qt
 * Updated Spanish translation

## [Transmission 2.20](https://trac.transmissionbt.com/query?milestone=2.20&group=component&order=severity) (2011-02-06)
### All Platforms
 * Remember downloaded files when a drive is unplugged
 * File re-verification is no longer needed in some situations
 * Fix "Too many open files" error
 * Show the total downloading and seeding time per torrent
 * Fix webseeds
 * Better support for IPv6-only trackers
 * Add the ability to shutdown Transmission sessions via RPC
 * NAT-PMP and UPnP now also map the UDP port
 * Update the DHT code to dht-0.18
 * Faster parsing of bencoded data
 * Improve support for running scripts when a torrent finishes downloading
 * Fix reannounce interval when trackers return a 404 error
 * Fix checksum error on platforms running uClibc 0.9.27 or older
 * Fix memmem() errors on Solaris
### Mac
 * Fix issues in the German and Spanish localizations
 * Interface tweaks
 * Support ZIP and other compression formats in the blocklist downloader
### GTK+
 * Add "Add" and "Remove" buttons to the tracker list
 * Add filesize column to the files list
 * Several minor bug fixes and interface improvements
### Qt
 * Accept info_hash values in the "Add url..." dialog
### Daemon
 * Add "reannounce to tracker" option to transmission-remote
 * transmission-remote can now read auth info from environment variables
 * Fix configuration file bug with transmission-daemon running on Macs
### Web Client
 * Right-clicking a torrent now works with Firefox / Firegestures / Ubuntu
### Utils
 * Fix error when replacing substrings in tracker announce URLs
 * Webseeds are now displayed in transmission-show

## [Transmission 2.13](https://trac.transmissionbt.com/query?milestone=2.13&group=component&order=severity) (2010-12-09)
### All Platforms
 * Fix "No Announce Scheduled" tracker error
 * Fix build error on Solaris 10
 * Fix RPC documentation errors
 * Fix minor packaging errors
### Mac
 * Add option to run a script when torrents finish downloading
 * Simplify editing the blocklist URL in the preferences window
### GTK+
 * Unblur the statusbar icons
 * Fix truncation error in the Torrent Properties dialog
### Qt
 * Fix crash when opening the Torrent Properties dialog on magnet links
 * Fix "undo" error when making changes in the Torrent Properties dialog
 * Add Brazilian Portuguese Translation
 * Add Spanish (LAC) Translation

## [Transmission 2.12](https://trac.transmissionbt.com/query?milestone=2.12&group=component&order=severity) (2010-11-14)
### All Platforms
 * Make the blocklist URL user-configurable
 * Several minor bug fixes and CPU speed improvements
 * Use slightly less bandwidth in DHT messages
 * Fix 2.10 build issue on uClibc systems
### Mac
 * Sort the file list alphabetically
 * Ensure the proper extension is used when saving the torrent file
 * Allow the Quick Look command when the inspector window has focus
### GTK+
 * Fix 2.11 crash when opening the Properties dialog on a magnet link torrent
 * Fix 2.00 regression which failed to inhibit hibernation on laptops
### Qt
 * Fix 2.10 build issue on Ubuntu
### CLI
 * Fix 2.10 crash
### Web Client
 * Upgrade to jQuery 1.4.3
 * Fix 2.11 regression when using the web client on IE7 or IE8

## [Transmission 2.11](https://trac.transmissionbt.com/query?milestone=2.11&group=component&order=severity) (2010-10-16)
### All Platforms
 * Fix a 2.10 crash
 * Fix minor build issues on various platforms, particularly embedded systems
 * Fix issue when saving bencoded files on small hardware running uClibc
 * Fix minor rounding issue when displaying percentages
 * Fix the optimistic unchoke interval
### Mac
 * Swipe to change inspector tab
### GTK+
 * Fix drag-and-dropping a magnet link
 * Fix hiding dialogs when hiding Transmission in the notification area
### Qt
 * Fix crash in the file tree
### Daemon
 * Fix missing status message when using "transmission-remote --add"
### Web Client
 * Add Transmission website links to the action menu

## [Transmission 2.10](https://trac.transmissionbt.com/query?milestone=2.10&group=component&order=severity) (2010-10-07)
### All Platforms
 * Support for automatically stopping idle seeding torrents
 * Small memory cache to reduce disk IO
 * gzip compression support added to the RPC server
 * Add support for a cookies.txt file
 * Minor CPU optimizations
### Mac
 * Display file sizes and speeds in base 10 on Snow Leopard
### GTK+
 * Files and folders can be opened by clicking on them in the files list
 * Update the interface when session changes are made via RPC
 * Fix the Details dialog to fit on a netbook screen
### Qt
 * Tracker announce list editing
 * New filterbar
 * Improved display for showing a torrent's tracker announces
 * Better DBUS integration
 * Support adding torrents via drag-and-drop
 * Add Desktop Notification for added/complete torrents
 * Other minor improvements
### Web Client
 * Peer list added to the inspector
 * Compact view mode
 * Support filtering by active and finished
 * Support sorting by size
### Daemon
 * Allow the .pidfile location to be set in settings.json
### Utils
 * New command-line utility "transmission-edit" for editing torrent files
 * New command-line utility "transmission-show" for viewing torrent files
 * New command-line utility "transmission-create" for creating torrent files

## [Transmission 2.04](https://trac.transmissionbt.com/query?milestone=2.04&group=component&order=severity) (2010-08-06)
### All Platforms
 * Improved shutdown time in some situations
 * Fix display bug in tr_truncd()
### Mac
 * Fix bug that caused Local Peer Discovery to always be disabled on startup
### GTK+
 * Fix inaccurate "active torrent" counts in the filterbar
 * Fix display bug with magnet link names
### Qt
 * Fix crash when accessing a password-protected remote session
### Web Client
 * Fix bad redirect

## [Transmission 2.03](https://trac.transmissionbt.com/query?milestone=2.03&group=component&order=severity) (2010-07-21)
### Mac
 * Fix 2.02 bug where new transfers could not be added and the inspector would not appear

## [Transmission 2.02](https://trac.transmissionbt.com/query?milestone=2.02&group=component&order=severity) (2010-07-19)
### All Platforms
 * Fix NAT-PMP port forwarding issue on some systems
 * Fix filename errors for torrents whose files are encoded in ISO-8859-1
 * Fix rare crash on shutdown
 * Fix the RPC server's redirect URL to allow HTTPS proxies like stunnel
 * Replace less-portable calls with posix nanosleep()
### Mac
 * Use F_NOCACHE to keep "inactive memory" in check
### GTK+
 * Fix crash when opening the Properties dialog on a magnet link without metainfo
 * Fix crash when removing multiple torrents at once
 * Allow individual torrents' download speed limits to be set to zero
 * Fix translation error with some error messages
### Qt
 * Fix CPU spike when opening the Properties dialog
 * Fix compilation issue with Qt < 4.5

## [Transmission 2.01](https://trac.transmissionbt.com/query?milestone=2.01&group=component&order=severity) (2010-06-26)
### All Platforms
 * Better tracker announce management when completed torrents are moved from the incomplete directory
 * Speed up moving local data from one disk to another
 * Better parsing of nonstandard magnet links
### Mac
 * If the seed ratio is already met when download completes, still perform the Growl notification and download-complete sound
 * Fix the Help buttons in the preferences window
### GTK+
 * Faster torrent file parsing
 * Fix the magnet link options dialog does not respect setting
 * Add an error popup if "Add URL" fails
### Qt
 * Fix crash after getting magnet torrent metadata
 * Fix torrent ratio goals
 * Fix "add torrent" dialog bug on KDE desktops that popped up previous torrents
 * Fix 2.00 bug that prevented multiple instances from being run
 * Fix remote mode bug that kept the torrent list from being shown
 * Support encryption settings in the preferences dialog
 * Use flagStr in the status field of the peer list
 * Request a full refresh when changing the session source
 * Fix the torrent list jumping to the top when a torrent is removed
### Web Client
 * Fix display bug caused by removal of a torrent hidden by the current filter

## [Transmission 2.00](https://trac.transmissionbt.com/query?milestone=2.00&group=component&order=severity) (2010-06-15)
### All Platforms
 * "Local Peer Discovery" for finding peers on the local network
 * Optimize download requests for the bandwidth available
 * Smarter heuristics when deciding the order to connect to peers
 * Faster verification of local data
 * Faster startup
 * Support more blocklist file formats
 * Use IEC standard units (KiB, MiB, GiB) instead of (KB, MB, GB)
 * Better handling of 404 tracker errors
### Mac
 * Compact View replaces Minimal View, taking up considerably less space
 * Show an Add Transfer window when adding magnet links
 * "Resume All" now ignores finished transfers
 * Allow trackers to be pasted into the Create Window
 * European Portuguese localization
 * Removed Traditional Chinese localization because of lack of localizer
### GTK+
 * New filterbar to filter by tracker, private/public, etc.
 * Compact View replaces Minimal View, taking up considerably less space
 * Show the Torrent Options dialog when adding magnet links
 * "Set Location" now supports moving multiple torrents at once
 * The Properties window now fits on low resolution screens
 * Add favicon support to the Properties dialog's Tracker tab
### Qt
 * Show the Torrent Options dialog when adding magnet links
 * Show all active trackers in the tracker display list
 * Show file sizes in the file tree
 * Added a confirm dialog when removing torrents
 * Properties and torrent options no longer jump around while editing
 * Allow setting locations for remote sessions
 * Miscellaneous UI fixes
### Daemon
 * Let users specify a script to be invoked when a torrent finishes downloading
 * Better support for adding per-torrent settings when adding a new torrent
 * Optional pidfile support
 * Option to start torrents paused
 * Option to delete torrent files from watch directory
### Web Client
 * The context menu now works when multiple rows are selected
 * Show ETA for seeding torrents that have a seed ratio set

## [Transmission 1.93](https://trac.transmissionbt.com/query?milestone=1.93&group=component&order=severity) (2010-05-01)
### All Platforms
 * Fix rare crash when some peers disconnected unexpectedly
 * Fix bug which didn't parse scheduled speed limit dates correctly
 * Fix bug that broke magnet links whose names contained slashes
 * Fix crash when updating the blocklist when the disk is full
 * Fix slow file preallocation on Unix systems not using ext3/ext4
 * Fix regression which broke the "bind-address-ipv4" configuration setting
 * For better security, Web client connections are disabled by default
 * Update to miniupnpc-1.4
 * Transmission builds out-of-the-box with Curl 7.15.5. (Hello CentOS!)
### GTK+
 * Use the size for the system tray icon
### Qt
 * Fix bug that crashed when removing more than one torrent at once
 * Fix bug when parsing the remote password from the command line
 * Add support for the "incomplete directory" in the preferences dialog
 * Don't show "time remaining" for paused torrents
### Daemon
 * Fix bug parsing RPC requests when setting which files to not download
 * Fix possible crash when using inotify for the daemon's watchdir
 * Fix bugs in the configure script
 * Fix bug updating the blocklist over RPC
### Web Client
 * Fix bug that broke the "reverse sort order" menu checkbox

## [Transmission 1.92](https://trac.transmissionbt.com/query?milestone=1.92&group=component&order=severity) (2010-03-14)
### All Platforms
 * Fix possible data corruption issue caused by data sent by bad peers during endgame
 * Fix potential buffer overflow when adding maliciously-crafted magnet links
 * Fix announces to IPv6 trackers
 * Fix DNS problems on some platforms, including Debian
 * Fix issues with the incomplete directory functionality
 * Fix port forwarding error on some routers by updating libnatpmp and miniupnp
### Mac
 * Fix bug where setting low priority in the add window resulted in high priority
### GTK+
 * Fix directory selection error in GTK+ 2.19
 * Small GUI improvements: HIG correctness, remove deprecated GTK+ calls, etc.
### Daemon
 * Fix 1.91 build error on Mac and FreeBSD
 * Standardize the daemon's watchdir feature to behave like the other clients'
### Web Client
 * Statistics dialog
 * Fix error in "trash data & remove from list" that didn't trash all data
 * Fix display of ratios and time
 * Update to jQuery 1.4.2

## [Transmission 1.91](https://trac.transmissionbt.com/query?milestone=1.91&group=component&order=severity) (2010-02-21)
### All Platforms
 * Fix 1.90 crash-on-startup bug that affected some users
 * Fix 1.90 bug that caused the "turtle mode" state to be forgotten between sessions
 * Fix 1.83 crash when adding a torrent by URL from an ftp source via the web client
 * For the BitTorrent spec's "downloaded=X" passage, use the de facto standard
### Mac
 * Fix 1.90 bug when removing trackers

## [Transmission 1.90](https://trac.transmissionbt.com/query?milestone=1.90&group=component&order=severity) (2010-02-16)
### All Platforms
 * Add an option to disable the .part suffix for incomplete files
 * Add priority selector to GUI clients' add torrent windows
 * Fix 1.81 bug that broke HTTP requests to sites that gave HTTP 301 redirects
 * Fix 1.8x bug in announcing "finished downloading" and "stopping" at the same time
 * Fix 1.8x bug in announcing partial seeds
 * Try harder to make announces finish, even if the tracker responds slowly
 * Fix bug that didn't honor download speed limits of 0
 * Use fallocate64() for fast file preallocation on systems that support it
 * Magnet link improvements
 * Don't let "Disk is full" errors cause loss of configuration files
 * Faster parsing of bencoded data, such as torrent files
### Mac
 * Display information for all selected transfers in the inspector's tracker and peer tabs
 * Add a filter to the message log
 * Fix potential crash when updating the blocklist
 * Fix bug that caused the speed limit scheduler to not be applied after sleep
 * Remove excessive file selection for the per-torrent action menu
 * Smaller interface tweaks
### GTK+
 * Give more helpful error messages if "Set Location" or "Add Magnet Link" fail
 * Add optional support for libappindicator
 * Minor build fixes
### Daemon
 * Add transmission-remote support for port testing and blocklist updating
 * Add transmission-daemon support for incomplete-dir, dht, and seedratio
 * If settings.json is corrupt, give an error telling where the problem is
 * Add option to specify where log messages should be written
### Web Client
 * Add a tracker tab to the inspector
 * Fix 1.8x display error when showing magnet link information
### Qt
 * Fix bug that prevented torrents from being added via web browsers

## [Transmission 1.83](https://trac.transmissionbt.com/query?milestone=1.83&group=component&order=severity) (2010-01-28)
### All Platforms
 * Fix 1.80 announce error that caused uploads and downloads to periodically freeze
 * Fix 1.80 announce timeout error that caused "no response from tracker" message
 * Fix 1.80 "file not found" error message that stopped some torrents
 * Fix 1.82 crash when adding new torrents via their ftp URL
 * Fix 1.80 crash when receiving invalid request messages from peers
 * Fix 1.82 error when updating the blocklist

## [Transmission 1.82](https://trac.transmissionbt.com/query?milestone=1.82&group=component&order=severity) (2010-01-23)
### All Platforms
 * The 1.81 announce's "Host" HTTP header didn't contain the host's port number

## [Transmission 1.81](https://trac.transmissionbt.com/query?milestone=1.81&group=component&order=severity) (2010-01-22)
### All Platforms
 * Fix 1.80 bug that misparsed some magnet links
 * Fix 1.80 bug that caused startup to be very sluggish for some people
### Mac
 * Fix dragging URLs onto the dock icon
 * Fix auto-grouping by file name

## [Transmission 1.80](https://trac.transmissionbt.com/query?milestone=1.80&group=component&order=severity) (2010-01-20)
### All Platforms
 * Add support for magnet links
 * Add support for trackerless torrents
 * Add optional "incomplete directory" where partial downloads are stored
 * Add .part to incomplete files' filenames
 * Find more peers by announcing to each tier in a torrent's tracker list, rather than only one
 * Fix vulnerability from maliciously-crafted torrent files that could overwrite other files
 * Improved IPv6 support
 * Improved PEX sharing with other peers
 * Faster management of large peer lists
 * DHT improvements to bootstrapping and IPv6
 * Newly-added torrents without local data don't wait in the "Verify Local Data" queue anymore
 * Add an OS hint to not cache local data during torrent verification
 * Use less CPU when making encrypted handshakes to peers
 * Better filtering of bad IP addresses
 * Fix bug that gave "too many open files" error messages
 * Fix bug that could crash Transmission on shutdown
 * Fix bug that could unpause or repause a torrent on startup
 * When uploading, improve disk IO performance by prefetching the data in batches
 * Portability fixes for embedded systems
 * Other small bug fixes and improvements
### Mac
 * Redesigned trackers inspector tab with favicons and copy-paste functionality (paste lists of multiple trackers)
 * Message log stores all messages and does real filtering
 * Quick Look restored on Snow Leopard
 * Moving data and incomplete folder are now handled by libtransmission
 * Improved reveal in Finder functionality on Snow Leopard
 * Various smaller behavior and interface tweaks
 * German and Simplified Chinese localizations
 * Removed Turkish localization because of lack of localizer
### GTK+
 * Support org.gnome.SessionManager interface for inhibiting hibernation
 * Added support for adding torrents by URL or magnet link
 * Add optional "download complete" sound using the XDG sound naming spec
 * When creating a torrent, make it easier to auto-add that new torrent
 * New statusbar "Ratio" icon submitted by jimmac
 * Fix minor memory leaks
 * GNOME HIG improvements
### Daemon
 * When running as a daemon, send log messages to syslog
 * Reload settings.json when receiving SIGHUP
 * transmission-remote now allows per-torrent speed limits to be set
### Web Client
 * Add speed limit "turtle mode" support
 * Double-clicking a torrent opens/closes the torrent inspector
 * Add "Start When Added" checkbox when adding torrents
 * Add Select All / Deselect All buttons to the file inspector
 * Add version information to the preferences dialog
 * Ensure the context menu goes away when clicking on torrents
 * Fix bug that obscured part of the context menu

## [Transmission 1.77](https://trac.transmissionbt.com/query?milestone=1.77&group=component&order=severity) (2010-01-04)
### All Platforms
 * Protect against potential data loss by maliciously-crafted torrent files
 * Fix minor build issues and packaging issues on various platforms
 * Fix 1.7x error that could unpause or repause a torrent on startup
 * Minor CPU speedups
### GTK+
 * Fix crash on shutdown
 * Fix GIcon memory leak

## [Transmission 1.76](https://trac.transmissionbt.com/query?milestone=1.76&group=component&order=severity) (2009-10-24)
### All Platforms
 * Fix potential data loss when moving torrents to where they already are
 * Fix minor protocol error that didn't send a port message to some peers
 * Fix minor manpage errors
### Mac
 * Fix a potential crasher on Snow Leopard
 * When creating a multi-tracker torrent, give each tracker its own tier
 * Fix display glitch when changing sort to "Queue Order"
### Daemon
 * Fix potential data loss when using "transmission-remote --find"
 * Fix ratio-limit bug on some uClibc systems
 * Fix invalid JSON "nan" error on optware
### GTK+
 * Fix crash in the Preferences dialog when testing to see if the port is open
 * Fix crash on exit when a torrent's Properties dialog is open
 * Fix tracker address display error in the torrent Properties dialog
 * Fix tray menu's main window status when Transmission is started minimized
 * Fix broken SIGINT (ctrl-c) handling
 * Fix 1.61 build failure on systems with new versions of glib but older versions of gtk
### Qt
 * Fix crash when removing expired torrents from the display
 * Fix client from closing, rather than closing to the system tray, when clicking X
 * Cannot open a torrent in KDE4 with right-clicking

## [Transmission 1.75](https://trac.transmissionbt.com/query?milestone=1.75&group=component&order=severity) (2009-09-13)
### All Platforms
 * Don't wait so long on unresponsive trackers if there are other trackers to try
 * Adding corrupt/invalid torrents could crash Transmission
 * Fix 1.74 bug that caused a high CPU load on startup
 * Fix 1.74 bug that stopped multitracker if a single tracker sent an error message
 * Fix bug in converting other charsets to UTF-8
 * Handle HTTP redirects more gracefully
 * Faster verification of local data for torrents with small piece size
 * Fix 1.74 build error when compiling without DHT
### Mac
 * Fix libcurl build issue that caused tracker connectivity problems on Snow Leopard
 * Fix error when creating a torrent file while still changing the announce address
### GTK+
 * Fix "sort by time remaining"
 * Fix the turtle toggle button on old versions of GTK+
 * Fix startup error if another copy of the Transmission GTK client is running
 * Fix clang build issue

## [Transmission 1.74](https://trac.transmissionbt.com/query?milestone=1.74&group=component&order=severity) (2009-08-24)
### All Platforms
 * Better data recovery in the case of an OS or Transmission crash
 * If a data file is moved, stop the torrent instead of redownloading it
 * Fix bug that didn't list some peers in the resume file and in PEX
 * More helpful torrent error messages
 * DHT now honors the bind-address-ipv4 configuration option
 * Fix Debian build error with miniupnpc
 * Fix Cygwin build error with strtold
 * Update to a newer snapshot of miniupnpc
### Mac
 * 64-bit compatibility
 * Queuing system will not exclude transfers with tracker warnings
 * Links to original torrent files are no longer maintained
 * Fix bug where changing the global per-torrent peer connection limit did not affect the current session
 * Fix bug where changing settings through RPC would result in wrong values being saved for three fields
### GTK+
 * Fix crash that occurred when adding torrents on some desktops
 * Synchronize the statusbar's and torrent list's speeds
 * Fix the Properties dialog's "Origin" field for multiple torrents
### Qt
 * New Russian Translation
 * If Transmission was minimized, clicking on it the icon tray didn't raise it
### Daemon
 * Speed Limit mode support added to transmission-remote
 * Add a "session stats" readout to transmission-remote
### Web Client
 * Progress bar shows seeding progress
 * Fix bug that displayed "%nan" when verifying a torrent
 * "Pause All" only appeared to pause torrents with peers until refresh

## [Transmission 1.73](https://trac.transmissionbt.com/query?milestone=1.73&group=component&order=severity) (2009-07-18)
### All Platforms
 * Fix bug where user-configured peer limits could be exceeded
 * Use less memory in some high-peer situations
 * Better estimation of time left to download
 * Support supportcrypto and requirecrypto flags in http tracker announces
 * Update to newer snapshots of libnatpmp and miniupnpc
 * Make DHT a compile-time option
### GTK+
 * Use GDK-safe versions of g_idle_add() and g_timeout_add*()
 * Save some space in GTK+ >= 2.16.0 by not building SexyIconEntry
### Qt
 * Fix bug that crashed Qt client when setting alternative up/down speeds
### Daemon
 * Add umask support
### Web Client
 * Inspector and Add Torrent buttons for iPhone/iPod Touch
 * Add location field to inspector
### CLI
 * Some torrent files created with transmission-cli were invalid

## [Transmission 1.72](https://trac.transmissionbt.com/query?milestone=1.72&group=component&order=severity) (2009-06-16)
### All Platforms
 * Fix 1.70 crash with assertion "tv->tv_usec >= 0"
 * Fix 1.70 crash with assertion "tr_peerIoSupportsFEXT( msgs->peer->io )"
 * Better DHT announce management
 * Fix error in reporting webseed counts via RPC
 * Better file preallocation on embedded systems
### Mac
 * Fix problem where a small set of users could not add torrents
### GTK+
 * Fix 1.70 crash when setting options in the Properties dialog
 * Fix a rare crash in desktop notifications
 * Can now sort the file list by priority, download, and completeness
 * Adding a torrent from a browser sometimes didn't work
 * Various usability improvements
### Daemon
 * Remote didn't always send the right Encoding header in requests

## [Transmission 1.71](https://trac.transmissionbt.com/query?milestone=1.71&group=component&order=severity) (2009-06-07)
### All Platforms
 * Fix 1.70 bug that caused high CPU use in high-peer swarms
 * Fix 1.70 build problems with DHT and libevent
### Daemon
 * Fix watchdir issue on OSes that don't have inotify
### GTK+
 * Fix 1.70 intltool build problem
 * Fix crash when the OS's stock mime-type icons are misconfigured
 * Handle very long torrent file lists faster
### Web Client
 * Fix 1.70 bug where some torrents appeared to be duplicates

## [Transmission 1.70](https://trac.transmissionbt.com/query?milestone=1.70&group=component&order=severity) (2009-06-04)
### All Platforms
 * Distributed hash table (DHT) support for public torrents
 * Add option for moving and finding a torrent's data on the disk
 * JSON RPC messages can be ~85% shorter, saving bandwidth and CPU
 * When available, use the system's copy of libevent instead of building one
 * Automatically pause torrents when verifying
 * Fix small bugs and memory leaks
### Mac
 * Holding down the option key on launch will pause all transfers
### Web Client
 * Big speed improvements, especially with large torrents
 * Fix 1.61 bug that broke adding torrents via the web client
 * Add the ability to upload multiple torrents at once
 * Torrents added by URL were always paused, regardless of preferences
 * Comments and announce addresses were cut off in the inspector
 * The "data remaining" field wasn't updated when the number reached 0
 * Smaller design adjustments
### GTK+
 * Make it clearer that the status bar's ratio mode button is a button
 * Torrent comment box did not scroll, so long comments were partially hidden
### Qt
 * Initial torrent list was sometimes incorrect
 * Add-torrent-and-delete-source deleted the source even if there was an error
 * Prefs dialog didn't show or modify "Stop seeding torrents at ratio"

## [Transmission 1.54](https://trac.transmissionbt.com/query?milestone=1.54&group=component&order=severity) (2009-06-04)
### All Platforms
 * Fix small bugs and memory leaks
### Web Client
 * Fix 1.53 bug that broke adding torrents via the web client
 * Torrents added by URL were always paused, regardless of preferences
 * Comments and announce addresses were cut off in the inspector
 * The "data remaining" field wasn't updated when the number reached 0
 * Smaller design adjustments
### GTK+
 * Fix intltool build error
 * Make it clearer that the status bar's ratio mode button is a button
 * Torrent comment box did not scroll, so long comments were partially hidden

## [Transmission 1.61](https://trac.transmissionbt.com/query?milestone=1.61&group=component&order=severity) (2009-05-11)
### All Platforms
 * Close potential CSRF security hole for Web Client users
 * Fix locale error in the JSON parser
### Qt
 * Various small bug fixes to the beta Qt client
### Web Client
 * Fix 1.60 error when serving Web Client files on some embedded platforms
 * Add response header to allow clients to cache static files
### Daemon
 * transmission-remote was unable to select torrents by their SHA1 hash

## [Transmission 1.53](https://trac.transmissionbt.com/query?milestone=1.53&group=component&order=severity) (2009-05-11)
### All Platforms
 * Close potential CSRF security hole for Web Client users
 * Fix locale error in the JSON parser

## [Transmission 1.60](https://trac.transmissionbt.com/query?milestone=1.60&group=component&order=severity) (2009-05-04)
### All Platforms
 * Seed ratio limiting
 * Transfer prioritization
 * Option to specify if transfers are included in the global bandwidth limits
 * Random port, with optional randomization on startup
 * Improvements to UPnP port forwarding
 * Support for file preallocation on XFS filesystems
### Mac
 * Requires Mac OS X 10.5 Leopard or newer
 * Groups (moved to preferences) can be auto-assigned to transfers when adding based on multiple criteria
 * Groups can have a default location when adding transfers
 * The speed limit scheduler can now be applied to only specific days
 * Bonjour support for the web interface
 * File filter field in the inspector
 * Option to include beta releases when auto-updating (using modified Sparkle 1.5)
 * Portuguese localization
### Qt
 * New beta Qt client!
### GTK+
 * Speed Limit: Second set of bandwidth limits that can be toggled or scheduled
 * Properties dialog now lets you edit/view multiple torrents at once
 * Allow sorting of the torrent list by size and by ETA
 * Show the file icon in the list
### Daemon
 * Watch folder for auto-adding torrents
 * Many new features in the RPC/JSON interface
 * Allow users to specify a specific address when listening for peers
### Web Client
 * File selection and prioritization
 * Add option to verify local data
 * Fix "Remove Data" bug

## [Transmission 1.52](https://trac.transmissionbt.com/query?milestone=1.52&group=component&order=severity) (2009-04-12)
### Mac
 * Improve interface responsiveness when downloading
### GTK+ and Daemon
 * Always honor the XDG setting for the download directory
### GTK+
 * Fix formatting error when showing speeds measured in MB/s
 * Fix bug that caused some scheduled speed limit time settings to be lost
 * Use the new blocklist URL when updating the blocklist
### Web Client
 * On the server, better filtering of bad URLs
 * On the server, faster json serialization
 * Fix minor web client 301 redirect error
 * Better Internet Explorer support

## [Transmission 1.51](https://trac.transmissionbt.com/query?milestone=1.51&group=component&order=severity) (2009-02-26)
### All Platforms
 * Fix configure script issues with non-gcc compilers and user-defined CFLAGS environment variables
 * Decrease CPU usage in some situations
 * Close a rare race condition on startup
 * More efficient use of libcurl when curl 7.18.0 or newer is present
### GTK+
 * Play nicely with Ubuntu's new notification server
 * Add Pause All and Resume All buttons
### Web Client
 * Support for Internet Explorer
 * Layout fixes when viewed on an iPhone/iPod touch

## [Transmission 1.50](https://trac.transmissionbt.com/query?milestone=1.50&group=component&order=severity) (2009-02-13)
### All Platforms
 * IPv6 support for peers, and for trackers with explicit IPv6 addresses
 * Improved connectivity for encrypted peers
 * Fix 1.42 error that made tracker announces slower over time
 * Fix a Mac-centric peer connection bug from 1.41
 * Use less CPU cycles when managing very fast peers
 * Better handling of non-UTF-8 torrent files
 * When removing local data, only remove data from the torrent
 * Close potential DoS vulnerability in 1.41
 * Many other bug fixes
### GTK+
 * Various usability improvements
 * Better Gnome HIG compliance in the statusbar, properties dialog, and more
### Daemon
 * Lots of new options added to transmission-remote
 * Fix 1.42 whitelist bug
 * Make i18n support optional for cli and daemon clients
### CLI
 * Support session.json settings, just as the Daemon and GTK+ clients do
### Web Client
 * Torrents can now be added by URL
 * Add the ability to "remove local data" from the web client

## [Transmission 1.42](https://trac.transmissionbt.com/query?milestone=1.42&group=component&order=severity) (2008-12-24)
### All Platforms
 * Fix 1.41 lockup issue

## [Transmission 1.41](https://trac.transmissionbt.com/query?milestone=1.41&group=component&order=severity) (2008-12-23)
### All Platforms
 * Better peer management in large swarms
 * Support BitTorrent Enhancement Proposal (BEP) [#21](https://github.com/transmission/transmission/pull/21) "Extension for Partial Seeds"
 * Partial support for BEP [#6](https://github.com/transmission/transmission/pull/6) "Fast Extension" (reject, have all/none)
 * Honor the peer's BEP [#10](https://github.com/transmission/transmission/pull/10) reqq key, when available
 * Fix 1.40 "Got HTTP Status Code: 0" error message
 * Fix 1.40 "lazy bitfield" error
 * Fix 1.40 "jumpy upload speed" bug
 * Fix handshake peer_id error
 * Correctly handle Windows-style newlines in Bluetack blocklists
 * More accurate bandwidth measurement
 * File selection & priority was reset when editing a torrent's tracker list
 * Fix autoconf/automake build warnings
### GTK+
 * In the Details dialog's peer tabs, rows were sometimes duplicated
 * Minor bug fixes, usability changes, and locale improvements
 * Three new translations: Afrikaans, Asturian, Bosnian
 * Sixteen updated translations
### Daemon
 * Fix 1.40 bug in handling IP whitelist
 * Minor bug fixes and output cleanup
 * Windows portability
### CLI
 * Fix minor free-memory-read bug

## [Transmission 1.40](https://trac.transmissionbt.com/query?milestone=1.40&group=component&order=severity) (2008-11-09)
### All Platforms
 * Tracker communication uses fewer resources
 * More accurate bandwidth limits
 * Reduce disk fragmentation by preallocating files
 * Stability, security, and performance improvements to the RPC/Web UI server
 * Support compression when serving Web UI and RPC responses
 * Simplify the RPC whitelist
 * Fix bug that prevented handshakes with encrypted BitComet peers
 * Fix 1.3x bug that could re-download some data unnecessarily
 * Lazy bitfields
### Mac
 * Option to automatically update the blocklist weekly
 * In the file inspector tab, show progress and size for folders
 * Scrollbars correctly appear when the main window auto-resizes
 * Sparkle updated to 1.5b6
### GTK+
 * Option to automatically update the blocklist weekly
 * Added off-hour bandwidth scheduling
 * Simplify file/priority selection in the details dialog
 * Fix a couple of crashes
 * 5 new translations: Australian, Basque, Kurdish, Kurdish (Sorani), Malay
 * 43 updated translations
### Web Client
 * The Web Client is now out of beta
 * Minor display fixes
 * On iPhone/iPod touch, launching from the home screen hides the address bar
### Daemon
 * Added the ability to get detailed peer information on torrents
 * Fix bug that didn't handle --config-dir and TRANSMISSION_HOME correctly
 * Windows portability

## [Transmission 1.34](https://trac.transmissionbt.com/query?milestone=1.34&group=component&order=severity) (2008-09-16)
### All Platforms
 * Don't reconnect too frequently to the same peer
 * Webseed bug fix
 * Fix bug that caused some transfers to not be scraped
 * Fix BSD build issues
 * Handle OpenTracker's `downloaded' key in announce responses
### Mac
 * Fix memory leak when updating blocklist
 * Connect to the web interface when the application's path contains a space
### GTK+
 * Minor display fixes
 * 15 updated translations + 1 new language
### Daemon
 * Minor display fixes
### Web Client
 * Minor display fixes
### CLI
 * Fix crash when creating a torrent file

## [Transmission 1.33](https://trac.transmissionbt.com/query?milestone=1.33&group=component&order=severity) (2008-08-30)
### All Platforms
 * Fix crash from malicious torrent files with a piece size of zero
 * When creating new torrent files, change behavior that caused "Multi tracker torrents are not allowed" warning on one tracker
 * Fix Unicode handling in the JSON parser/generator
 * Fix memory error when reading KTorrent's PEX messages
 * Fix small memory leaks
### Mac
 * Rephrase "data not fully available" to "remaining time unknown"
 * Fix bug where torrent file creation would fail because an extra blank tracker address was inserted
### Daemon
 * Fix crash when adding nonexistent torrents via transmission-remote
### GTK+
 * Fix crash from malicious torrent files with large creator fields
 * Fix error where some torrents opened via a web browser didn't appear

## [Transmission 1.32](https://trac.transmissionbt.com/query?milestone=1.32&group=component&order=severity) (2008-08-08)
### All Platforms
 * Fix bug introduced in 1.30 that caused poor tracker communication
 * Creating torrent files for files >= 2 GB will result in 2 MB pieces
### Mac
 * Fix bug where the proxy type was changed from SOCKS5 to SOCKS4 on launch

## [Transmission 1.31](https://trac.transmissionbt.com/query?milestone=1.31&group=component&order=severity) (2008-08-06)
### All Platforms
 * Fix a crash caused by certain torrent files

## [Transmission 1.30](https://trac.transmissionbt.com/query?milestone=1.30&group=component&order=severity) (2008-08-05)
### All Platforms
 * The "Clutch" web interface is now bundled with Transmission (as a beta)
 * Ability to add and remove tracker addresses
 * Ability to create torrent files with multiple tracker addresses
 * Added support for HTTP/FTP Seeding (GetRight style)
 * Added proxy support for communicating with trackers
 * Allow torrent creation with no tracker address (required by some trackers)
 * New JSON-RPC protocol for clients to interact with the backend
### Daemon
 * transmission-daemon and transmission-remote were rewritten from scratch
 * remote and daemon now support per-file priority & download flag settings
### Mac
 * Quick Look integration in the main window and inspector's file tab
 * Transfers can be dragged to different groups
 * Option to only show the add window when manually adding transfers
 * Status strings are toggled from the action button (they are no longer clickable)
 * Colors in pieces bar and pieces box more accurately reflect their corresponding values
 * The port checker now uses our own portcheck.transmissionbt.com
 * Turkish localization
### GTK+
 * Add options to inhibit hibernation and to toggle the tray icon
 * Lots of small bug fixes and usability improvements
 * Dozens of updated translations

## [Transmission 1.22](https://trac.transmissionbt.com/query?milestone=1.22&group=component&order=severity) (2008-06-13)
### All Platforms
 * Fix two separate BitTorrent unchoke bugs that could affect performance
 * Transmission now builds correctly on Sun Studio
 * Minor man page fixes
### Mac
 * Fix bug where "Program Started" in the Statistics window would sometimes display as 0
### GTK+
 * Fix crash when quitting while the stats window is still up
 * Added Latvian, Malayalam, Serbian, and Telugu translations
 * Updated Czech, Spanish, Romanian, Russian, Dutch, Polish, Italian,
    Portuguese, Catalan, Danish, German, Swedish, Traditional Chinese,
    Finnish, and Chinese (simplified) translations
### CLI
 * Fix scraping torrents with the -s command-line argument

## [Transmission 1.21](https://trac.transmissionbt.com/query?milestone=1.21&group=component&order=severity) (2008-05-21)
### All Platforms
 * Fix rare crash-on-startup bug
 * Handle corrupt announce URLs in torrent files more gracefully
 * Fix minor memory leak when closing torrents
### Mac
 * Fix visual glitch with the pieces bar
 * Italian localization included
### GTK+
 * Updated Catalan, Danish, German, Spanish, Finnish, Hebrew, Italian, Dutch,
     Polish, Romanian, Thai, Turkish, and Traditional Chinese translations

## [Transmission 1.20](https://trac.transmissionbt.com/query?milestone=1.20&group=component&order=severity) (2008-05-09)
### All Platforms
 * Support https tracker connections
 * IP blocking using the Bluetack Level1 blocklist
 * Better support of multitracker torrents
 * Faster UPnP port mapping on startup
 * Ability to reset global statistics
 * Various bug fixes
### Mac
 * Display of decimal numbers matches system international settings
 * Updated Dock badge images
### GTK+
 * Inhibit hibernation when downloading
 * Use XDG basedir spec for configuration files
 * Various bug fixes and usability improvements
 * Vastly improved translations thanks to the Ubuntu translation team

## [Transmission 1.11](https://trac.transmissionbt.com/query?milestone=1.11&group=component&order=severity) (2008-04-04)
### Mac
 * Fix scrape address visual glitch with the tracker inspector tab
 * Only make the progress status string clickable on multi-file torrents
 * Traditional Chinese localization, updated Russian localization
### GTK+
 * Fix a couple of minor display issues

## [Transmission 1.10](https://trac.transmissionbt.com/query?milestone=1.10&group=component&order=severity) (2008-03-28)
### All Platforms
 * Stop torrents when downloading and the disk becomes full
 * Removing a torrent also removes it from the cache
 * Smaller memory footprint per-torrent and per-peer
 * Various backend bug fixes & improvements
### Mac
 * Window when adding torrents to select files and other settings
 * Leopard: Collapsable group dividers
 * Use the file icon as the per-torrent action button
 * Tracker tab in the inspector
 * Message log specifies the torrent/activity the message relates to
 * Updated images in the inspector
 * Optional display of remaining time while seeding in Minimal View
 * Improved accuracy for displaying the remaining disk space warning
### GTK+
 * Window for selecting files & priorities when opening torrents
 * Display a system tray popup when a torrent finishes downloading
 * Watch folder for auto-adding torrents
 * Improved preferences dialog and message log window
 * Tracker tab in the Details window
 * Dozens of usability, Gnome HIG, and i18n improvements
 * Support KDE button ordering
 * Option to delete a torrent and its downloaded files
### Daemon
 * Ability to force a "Verify Local Data" for a torrent

## [Transmission 1.06](https://trac.transmissionbt.com/query?milestone=1.06&group=component&order=severity) (2008-02-26)
### All Platforms
 * Improvements and bug fixes to "Verify Local Data"
 * Use less CPU
 * Fix support for multitracker torrents
 * Updated UPnP code to miniupnpc-1.0
 * Fix two shutdown memory errors
### GTK+
 * Fix small memory leak
 * GUI error when verifying local data
### CLI
 * Torrent creation error on non-absolute pathnames

## [Transmission 1.05](https://trac.transmissionbt.com/query?milestone=1.05&group=component&order=severity) (2008-02-08)
### All Platforms
 * Fix 1.04 crash when parsing bencoded data
 * Packaging improvements
### Mac
 * Fix bug remembering reordered groups
### GTK+
 * Fix glitch that occurred when specifying which files to download
 * Fix "Sort by Progress"
 * Various interface and HIG improvements
 * Updated Swedish, Italian translations
### Daemon
 * Fix "transmission-remote -x"
 * Fix PEX enable/disable bug

## [Transmission 1.04](https://trac.transmissionbt.com/query?milestone=1.04&group=component&order=severity) (2008-01-31)
### All Platforms
 * Fix (potential) remote crash bug with extension protocol
 * Fix bug when verifying a torrent and pressing `pause'

## [Transmission 1.03](https://trac.transmissionbt.com/query?milestone=1.03&group=component&order=severity) (2008-01-29)
### All Platforms
 * Fix bug setting maximum peer limits
 * Fix overflow issue with very large torrents
 * Fix LTEP handshake bug
 * Fix handshake bug with mainline BitTorrent
 * Fix bug when talking to lighttpd-based trackers
### GTK+
 * Various packaging, HiG, and interface improvements

## [Transmission 1.02](https://trac.transmissionbt.com/query?milestone=1.02&group=component&order=severity) (2008-01-22)
### All Platforms
 * Fix 1.00 bug that choked some models of routers
 * Fix 1.00 crash in peer handshake
 * Fix 1.00 bug that sometimes froze the app for a long time
 * Minor improvements to the command-line client
### GTK+
 * Fix crash when removing a torrent while its details window is open
 * Better compliance with the Gnome interface guidelines
 * I18N fixes
 * Updated Dutch translation
 * Various other interface additions and improvements

## [Transmission 1.01](https://trac.transmissionbt.com/query?milestone=1.01&group=component&order=severity) (2008-01-11)
### All Platforms
 * Fix 1.00 freezing issue
 * Fix 1.00 assertion failure
 * Improve initial connection speed
 * Added connection throttle to avoid router overload
 * Improve reconnection to peers with imperfect network connections
 * Fix crashes on architectures that require strict alignment
### Mac
 * Leopard: Double-click icon to reveal in Finder, progress string to toggle selected and total, and anywhere else to toggle the inspector
 * Leopard: Better behavior with Time Machine
 * Fix bugs with Clutch support
### GTK+
 * New Brazilian Portuguese, Chinese, Dutch, and Turkish translations
 * Fix 1.00 desktop internationalization error

## [Transmission 1.00](https://trac.transmissionbt.com/query?milestone=1.00&group=component&order=severity) (2008-01-04)
### All Platforms
 * Port forwarding now performed by MiniUPnP and libnatpmp
 * Ability to set global and per-torrent number of connections
 * Option to prefer not using encryption
 * Fix tracker connection error
 * PEX is now configured globally
 * Updated icon
### Mac
 * Redesigned Leopard-like look
 * Group labeling, filtering, and sorting
 * Statistics window
 * Pieces Bar (return of Advanced Bar)
 * Display "not available" overlay on the regular bar
 * Display remaining time for seeding transfers
 * Sort by total activity
 * Connectable from the Clutch web interface
 * Leopard: Time Machine will ignore incomplete downloads
 * Leopard: Fix bug where text fields would reject localized decimal values
 * Leopard: Fix bug where bandwidth rates chosen from the action menu would not apply the first time
### GTK+
 * Redesigned main window interface
 * Minimal Mode for showing more torrents in less desktop space
 * Torrent filtering
 * Port forwarding tester in Preferences
 * Statistics window
 * Sort by total activity, progress, state, and tracker
 * Various other interface additions and improvements
### CLI
 * Restore `scrape' feature

## [Transmission 0.96](https://trac.transmissionbt.com/query?milestone=0.96&group=component&order=severity) (2007-12-10)
### All Platforms
 * Fix 0.95 data corruption error
 * Fix 0.95 bug that broke UPnP
### Mac
 * Fix bug where dragging non-torrent files over the main window could result in excessive memory usage

## [Transmission 0.95](https://trac.transmissionbt.com/query?milestone=0.95&group=component&order=severity) (2007-12-04)
### All Platforms
 * Fix router errors caused by sending too many tracker requests at once
 * Fix bug that let speed-limited torrents upload too quickly
 * Faster average upload speeds
 * Faster connection to peers after starting a torrent
 * Fix memory corruption error
 * Disable SWIFT for ratio-based trackers
### Mac
 * Leopard: Fix for NAT-PMP port mapping
### GTK+
 * Fix Nokia 770 crash

## [Transmission 0.94](https://trac.transmissionbt.com/query?milestone=0.94&group=component&order=severity) (2007-11-25)
### All Platforms
 * Faster average download speeds
 * Automatically ban peers that send us too many corrupt pieces
 * Fix a crash that occurred if a peer sent us an incomplete message
 * Fix portmapping crash
 * Fix bug that left files open after their torrents were stopped
 * Fix 0.93 file permissions bug
 * Fix tracker redirect error
 * Fix LTEP PEX bug

## [Transmission 0.93](https://trac.transmissionbt.com/query?milestone=0.93&group=component&order=severity) (2007-11-12)
### All Platforms
 * Fix "router death" bug that impaired internet connectivity
 * Fix bug that could cause good peer connections to be lost
 * Tweak request queue code to improve download speeds
 * Better handling of very large files on 32bit systems
 * Consume less battery power on laptops
 * Fix minor IPC parsing error

## [Transmission 0.92](https://trac.transmissionbt.com/query?milestone=0.92&group=component&order=severity) (2007-11-05)
### All Platforms
 * Fix 0.90 data corruption bugs
 * Fix 0.90 possible delay when quitting
 * Fix 0.90 small memory leaks
### Mac
 * Leopard: Fix bug with typing values in Inspector->Options
 * Leopard: Fix bug with toggling Minimal View
### GTK+
 * Better support for large files on some Linux systems
 * Fix localization error in torrent inspector's dates

## [Transmission 0.91](https://trac.transmissionbt.com/query?milestone=0.91&group=component&order=severity) (2007-10-28)
### All Platforms
 * Fix 0.90 speed limits
 * Fix 0.90 problems announcing to some trackers
 * Fix 0.90 socket connection leak
 * Fix 0.90 IPC crash
 * Fix 0.90 cache bug that could cause "verify local files" to fail
 * Fix 0.90 build errors on OpenBSD and on older C compilers
### Mac
 * Fix a crash caused by custom sound files
 * Add Dutch localization, re-add Russian localization, fix Korean localization
### GTK+
 * Fix 0.90 packaging errors
 * Fix 0.90 crash-on-start with assertion failure: "destination != (void*)0"

## [Transmission 0.90](https://trac.transmissionbt.com/query?milestone=0.90&group=component&order=severity) (2007-10-23)
### All Platforms
 * Encryption support, with option to ignore unencrypted peers
 * Only report downloaded, verified good pieces in tracker `download' field
 * Improved compliance with BitTorrent spec
 * MSE Tracker Extension support
 * Significant rewrite of the libtransmission back-end
### Mac
 * Per-torrent action menu
 * Redesigned inspector with additional statistics and ability to be resized vertically in Peers and Files tabs
 * Redesigned message log
 * Optimizations to decrease memory usage
 * Sort and filter by tracker
 * Icon enhanced to support size of 512 x 512
 * Various smaller interface additions and improvements
### GTK+
 * Various interface improvements
 * Better compliance with the Gnome interface guidelines

## [Transmission 0.82](https://trac.transmissionbt.com/query?milestone=0.82&group=component&order=severity) (2007-09-09)
### All Platforms
 * Fixed bug that could limit transfer speeds
 * Fixed bug that corrupted torrents > 4 GB
 * Fixed bug that could allow bad peers to send too many pieces
 * For peers supporting both Azureus' and LibTorrent's extensions, allow negotiation to decide which to use
 *  Other minor fixes

## [Transmission 0.81](https://trac.transmissionbt.com/query?milestone=0.81&group=component&order=severity) (2007-08-22)
### All Platforms
 * Fix 0.80 assertion crashes
 * Fix a bug that miscounted how many peers Transmission wants connected
 * Clarify misleading error messages
 * Fix memory leaks
### Mac
 * Multiple fixes to creating torrents and adding new torrents
 * Updated Russian and Spanish translations
### GTK+
 * Updated Dutch, Portuguese, French, and Slovakian translations
### CLI
 * Better support for cli-driven torrent creation
 * Fix a bug that misparsed command-line arguments

## [Transmission 0.80](https://trac.transmissionbt.com/query?milestone=0.80&group=component&order=severity) (2007-08-07)
### All Platforms
 * Ability to selectively download and prioritize files
 * Torrent file creation
 * Speed and CPU load improvements
 * Fix to UPnP
 * Rechecking torrents is now done one-at-a-time to avoid heavy disk load
 * Better rechecking of torrents that have many files
 * Many miscellaneous improvements and bug fixes
 * Partial licensing change -- see the LICENSE file for details
### Mac
 * Overlay when dragging torrent files, URLs, and data files onto window
 * Ability to set an amount of time to consider a transfer stalled
 * More progress bar colors
 * Various smaller interface improvements
 * Italian, Korean, and Russian translations
### GTK+
 * Added Torrent Inspector dialog
 * Added Update Tracker button
 * Various smaller interface improvements

## [Transmission 0.72](https://trac.transmissionbt.com/query?milestone=0.72&group=component&order=severity) (2007-04-30)
 * Reset download/upload amounts when sending "started"
 * Fix rare XML parsing bug

## [Transmission 0.71](https://trac.transmissionbt.com/query?milestone=0.71&group=component&order=severity) (2007-04-23)
### All Platforms
 * Send port info when sending requests
 * Calculate ratio differently when seeding without ever downloading
 * Add additional error messages and debug info
 * Improved UPnP support
### Mac
 * Fix error when using default incomplete folder
 * Disable the stop ratio once it is reached (while seeding)
 * Small interface adjustments

## [Transmission 0.70](https://trac.transmissionbt.com/query?milestone=0.70&group=component&order=severity) (2007-04-18)
### All Platforms
 * New icon
 * Automatic port mapping (NAT-PMP and UPnP IGD)
 * Peer exchange (PEX) compatible with Azureus and uTorrent
 * Multitracker support
 * Better handling of tracker announce interval
 * Fixes bug where absurdly huge upload/download totals could be sent
 * Automatic tracker scraping
 * Cache connected peers
 * Many miscellaneous bug fixes and small improvements
### Mac
 * Requires 10.4 Tiger or newer
 * Download and seeding queues that can be user-ordered
 * Speed Limit: Second set of bandwidth limits that can be toggled or scheduled
 * Individual torrent bandwidth limits
 * Separate complete and incomplete download folders
 * Filter and search bar
 * Expanded Inspector with many additional views and stats
 * Fragment status view in Inspector shows downloaded or availability
 * Watch folder to auto add torrent files
 * Auto resizing of the main window
 * Minimal view to take up less space
 * Seeding bar shows progress in finishing seeding
 * Sounds when downloading and seeding complete
 * Warnings for directory unavailable and not enough space
 * Message log window
 * New toolbar icons
 * Built-in help files
 * French, Spanish, German, and Greek translations
### GTK+
 * New Spanish, Polish, Russian, Bulgarian, Romanian, Swedish, and Finnish translations
 * Message window
 * Better window manager integration
 * Add file view to properties dialog

## Transmission 0.6.1 (2006-06-25)
### Mac
 * Fixes a bug in the updater that could cause repeated hits to the appcast
### GTK+
 * Fixes drag-and-drop
 * Adds Italian and French translations

## [Transmission 0.6](https://trac.transmissionbt.com/query?milestone=0.6&group=component&order=severity) (2006-06-21)
### All Platforms
 * Ability to limit download rate
 * Automatic banning of peers who send bad data
 * Can keep a copy of the torrent file so the original can be deleted
 * Many bug fixes
### Mac
 * Reworked interface
 * Rate limits can be changed directly from the main window
 * Ability to automatically stop seeding at a given ratio
 * Allows sorting of the transfers list
 * Extended Info Inspector
 * Automatic updating with Sparkle
### GTK+
 * Add torrents via command line. If Transmission is already running, add them to running copy
 * Improved long filename handling

## [Transmission 0.5](https://trac.transmissionbt.com/query?milestone=0.5&group=component&order=severity) (2006-02-11)
### All Platforms
 * Only uses one port for all torrents
 * Rewritten choking algorithm
 * Remembers download and upload sizes from one launch to another
### Mac
 * Dock badging
 * Shows the file icon in the list
 * Shows ratio once download is completed
 * Automatic check for update
 * Fixes a display bug on Intel Macs
### GTK+
 * New GTK+ interface
### BeOS
 * New BeOS interface

## [Transmission 0.4](https://trac.transmissionbt.com/query?milestone=0.4&group=component&order=severity) (2005-11-18)
### All Platforms
 * Uses less CPU downloading torrents with many pieces
 * The UI could freeze when the hard drive was having a hard time - fixed
 * Fixes for difficult trackers, which require a 'key' parameter or a User Agent field
### Mac
 * Cleaner look, unified toolbar
 * Added a document icon for torrent files
 * Added a Pause/Resume button for each torrent, and a "Reveal in Finder" button
 * Added a contextual menu
 * Sometimes torrents kept "Stopping..." forever - fixed
 * Several minor improvements or fixes: allows column reordering,
    fixed resizing on Panther, remember the position of the window,
    fixed display of Unicode filenames, added menubar items and
    keyboard shortcuts, made the simple progress bar switch to green
    when seeding

## [Transmission 0.3](https://trac.transmissionbt.com/query?milestone=0.3&group=component&order=severity) (2005-10-19)
### All Platforms
 * Fixed "Sometimes sends incorrect messages and looses peers"
 * Fixed "Crashes with many torrents or torrents with many files"
 * Enhancements in the "End game" mode
 * Is nicer to the trackers
 * Asks for the rarest pieces first
### Mac
 * Universal binary for PPC and x86
 * Fixed "Progress increases every time I pause then resume"
 * Fixed "Sometimes crashes at exit"
 * Cleaner icon
 * Show all sizes in human-readable form
 * Keep downloading in the background when the window is closed
 * Miscellaneous bug fixes and internal enhancements

## [Transmission 0.2](https://trac.transmissionbt.com/query?milestone=0.2&group=component&order=severity) (2005-09-22)
### All Platforms
 * Bug fixes
### Mac
 * Users can now choose where the downloads are sent

## [Transmission 0.1](https://trac.transmissionbt.com/query?milestone=0.1&group=component&order=severity) (2005-09-15)
 * First version
