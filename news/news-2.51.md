## [Transmission 2.51](https://trac.transmissionbt.com/query?milestone=2.51&group=component&order=severity) (2012-04-08)
### All Platforms
 * Update the bundled libnatpmp and miniupnp port forwarding libraries
 * Add environment variable options to have libcurl verify SSL certs
 * Support user-specified CXX environment variables during compile time
### Mac
 * Raise the allowed limits for many configuration options
 * Fix regression that ignored user-specified TRANSMISSION_HOME environment
### GTK+
 * Fix crash when adding torrents on systems without G_USER_DIRECTORY_DOWNLOAD
 * Honor the notification sound setting
 * Add a tooltip to files in the torrents' file list
 * Fix broken handling of the Cancel button in the "Open URL" dialog
 * Improve support for Gnome Shell and Unity
 * Catch SIGTERM instead of SIGKILL
### Qt
 * Progress bar colors are now similar to the Mac and Web clients'
 * Improve the "Open Folder" behavior
### Web Client
 * Fix global seed ratio progress bars
 * Fix sometimes-incorrect ratio being displayed in the inspector
 * If multiple torrents are selected, show the aggregate info in the inspector
 * Upgrade to jQuery 1.7.2
### Daemon
 * Show magnet link information in transmission-remote -i
