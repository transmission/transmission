## [Transmission 2.77](https://trac.transmissionbt.com/query?milestone=2.77&group=component&order=severity) (2013-02-18)
### All Platforms
 * Fix 2.75 regression that broke JSON parsing of real numbers in locales that don't use '.' as a decimal separator
 * Don't invalidate the OS's file cache when closing files
 * Fix overflow error when setting speed limits above ~8589 kB/s
 * Generated magnet links didn't include webseeds
 * Fix minor memory leaks when using webseeds
### GTK+ Client
 * Minor pluralization fixes in the UI
 * Fix folder mis-selection issue in the Preferences dialog
 * Fix GTK+ console warnings on shutdown
### Qt Client
 * Fix non Latin-1 symbol issue when showing file transfer speeds
 * Fix issue when creating new torrents with multiple trackers
 * Fix lost text selection in the properties dialog's 'comment' field
### Daemon
 * Fix documentation errors in the spec and manpages
### Web Client
 * Fix minor DOM leak
### CLI
 * Fix transmission-cli failure when the download directory doesn't exist
