## [Transmission 2.82](https://trac.transmissionbt.com/query?milestone=2.82&group=component&order=severity) (2013-08-08)
### All Platforms
 * Fix webseed crash
 * Fix crash when adding UDP trackers whose host's canonical name couldn't be found
 * Fix crash when sending handshakes to some peers immediately after adding a magnet link
 * Fix crash when parsing incoming encrypted handshakes when the user is removing the related torrent
 * Add safeguard to prevent zombie processes after running a script when a torrent finishes downloading
 * Fix "bad file descriptor" error
 * Queued torrents no longer show up as paused after exiting & restarting
 * Fix 2.81 compilation error on OpenBSD
 * Don't misidentify Tixati as BitTornado
### Mac Client
 * Fix bug that had slow download speeds until editing preferences
### GTK+ Client
 * Fix crash that occurred in some cases after using Torrent > Set Location
 * Fix crash where on_app_exit() got called twice in a row
 * Fix 2.81 compilation error on older versions of glib
 * Can now open folders that have a '#' in their names
 * Silence gobject warning when updating a blocklist from URL
### Qt Client
 * Qt 5 support
### Web Client
 * Fix syntax error in index.html's meta name="viewport"
 * Fix file uploading issue in Internet Explorer 11
