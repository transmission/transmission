## [Transmission 2.91](https://trac.transmissionbt.com/query?milestone=2.91&group=component&order=severity) (2016-03-06)
### All Platforms
 * Fix Makefile.am to include Windows patches into source archive
 * Fix miniupnpc script to handle spaces and other special chars in paths
### Mac Client
 * Prevent crash during group rules removal in some cases
 * Fix failure to remove seeding completion notifications from notification center
 * Show main window and scroll to torrent on notification click
 * Fix issue on Yosemite where peers view didn't occupy all the available space when web seed view was hidden
### Qt Client
 * Fix existing running instance detection and torrents delegation when using DBus
### Daemon
 * Fix building on Windows x86
 * Add `--blocklist-update` argument description to transmission-remote man page
 * Use `-rad` as short form of `--remove-and-delete` option in transmission-remote
