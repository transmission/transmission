## [Transmission 2.81](https://trac.transmissionbt.com/query?milestone=2.81&group=component&order=severity) (2013-07-17)
### All Platforms
 * Fix 2.80 bug that showed the incorrect status for some peers
 * Better handling of announce errors returned by some trackers
 * Fix compilation error on Solaris
### Mac Client
 * Fix 2.80 crash when removing a torrent when its seed ratio or idle limit is reached
 * Fix crash when pausing some torrents
 * Fix 2.80 icon display on Mavericks
### GTK+ Client
 * Fix minor memory leaks
 * Remove OnlyShowIn= from the .desktop file
### Qt Client
 * Remove OnlyShowIn= from the .desktop file
### Daemon
 * Change the systemd script to start Transmission after the network's initialized
### Web Client
 * Slightly better compression of png files
