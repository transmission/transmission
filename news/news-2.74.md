## [Transmission 2.74](https://trac.transmissionbt.com/query?milestone=2.74&group=component&order=severity) (2012-12-10)
### All Platforms
 * Fix a bug that prevented IPv6 addresses from being saved in dht.dat
 * Better handling of magnet links that contain 'tr.x=' parameters
 * Add filtering of addresses used for uTP peer connections
 * Fix detection of whether or not a peer supports uTP connections
### Mac
 * Auto-grouping won't apply until torrents are demagnetized
 * Tweak the inspector's and add window's file lists to avoid auto-hiding scrollbars overlapping the priority controls
 * Fix potential crash when downloading and seeding complete at the same time
 * Fix bug where stopped torrents might start when waking the computer from sleep
### Web Client
 * Fix a multi-file selection bug
 * Fix bug where the upload and download arrows and rates would not appear for downloading torrents
 * Fix bug when displaying the tracker list
