## [Transmission 2.76](https://trac.transmissionbt.com/query?milestone=2.76&group=component&order=severity) (2013-01-08)
### All Platforms
 * Better error logging when user-provided scripts can't be executed
 * The "Time Remaining" property wasn't set for torrents with webseeds but no peers
 * Fix rare error that created a directory name "$HOME"
### GTK+ Client
 * Fix sort-by-age regression introduced in 2.74
 * The "Edit Trackers" window didn't resize properly due to a 2.70 regression
 * Raise the main window when presenting it from an App Indicator
### Qt Client
 * Add magnet link support to transmission-qt.desktop
 * Fix notification area bug that inhibited logouts & desktop hibernation
 * Use the "video" icon when the torrent is an mkv or mp4 file
 * Toggling the "Append '.part' to incomplete files' names" had no effect
 * Fix display of the torrent name in the Torrent Options dialog
 * Fix cursor point bug in the filterbar's entry field
 * Fix crash when adding a magnet link when Transmission was only visible in the system tray
 * Fix free-memory-read error on shutdown
### Daemon
 * Better watchdir support
 * Documentation fixes in transmission-remote's manpage
### Web Client
 * Fix indentation of the torrent list and toolbar buttons on mobile devices
### CLI
 * If the Download directory doesn't exist, try to create it instead of exiting
