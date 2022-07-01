## [Transmission 2.50](https://trac.transmissionbt.com/query?milestone=2.50&group=component&order=severity) (2012-02-14)
### All Platforms
 * Fix crash when adding some magnet links
 * Improved support for downloading webseeds with large files
 * Gracefully handle incorrectly-compressed data from webseed downloads
 * Fairer bandwidth distribution across connected peers
 * Use less CPU when calculating undownloaded portions of large torrents
 * Use the Selection Algorithm, rather than sorting, to select peer candidates
 * Use base-10 units when displaying bandwidth speed and disk space
 * If the OS has its own copy of natpmp, prefer it over our bundled version
 * Fix Fails-To-Build error on Solaris 10 from use of mkdtemp()
 * Fix Fails-To-Build error on FreeBSD from use of alloca()
 * Fix Fails-To-Build error when building without a C++ compiler for libuTP
### Mac
 * Requires Mac OS X 10.6 Snow Leopard or newer
 * Animated rows in the main window (Lion only)
 * Quarantine downloaded files (to protect against malware)
 * The inspector no longer floats above other windows (by popular demand)
 * Mist notifications: basic notification support for users without Growl
 * Support pasting a torrent file URL into the main window (Lion only)
 * Minor interface tweaks and bug fixes
### GTK+
 * Fix regression that broke the "--minimized" command-line argument
 * Instead of notify-send, use the org.freedesktop.Notifications DBus API
 * Fix a handful of small memory leaks
### Qt
 * Fix FTB when building without libuTP support on Debian
### Web Client
 * Filtering by state and tracker
 * Sorting by size
 * Larger, easier-to-press toolbar buttons
 * Fix the torrent size and time remaining in the inspector's details tab
 * Bundle jQuery and the stylesheets to avoid third-party CDNs
 * Upgrade to jQuery 1.7.1
 * Fix runtime errors in IE 8, IE 9, and Opera
 * Revise CSS stylesheets to use SASS
 * Minor interface tweaks
### Daemon
 * Fix corrupted status string in transmission-remote
