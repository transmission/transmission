# Transmission 4.1.0-beta.1

This is the first beta for 4.1.0! It's been in development in parallel
with the 4.0.x bugfix series since February 2023's release of 4.0.0 and
has major code changes relative to 4.0.x.


## What's New in 4.1.0-beta.1

### Highlights

* Added optional sequential downloading. ([#4795](https://github.com/transmission/transmission/pull/4795))
* Added the option `preferred-transport` to `settings.json`, so that users can choose their preference between µTP and TCP. ([#5939](https://github.com/transmission/transmission/pull/5939))
* Improved µTP download performance. ([#6508](https://github.com/transmission/transmission/pull/6508))
* Added support for IPv6 and dual-stack UDP trackers. ([#6687](https://github.com/transmission/transmission/pull/6687))
* Fixed `4.0.6` bug where Transmission might spam HTTP tracker announces. ([#7086](https://github.com/transmission/transmission/pull/7086))

### All Platforms

* Added ability to cache IP addresses used in global communications, and use it to fix UDP6 warning log spam. ([#5329](https://github.com/transmission/transmission/pull/5329), [#5510](https://github.com/transmission/transmission/pull/5510))
* Added support for sending an `ipv4` parameter during the [Extension Protocol handshake](https://www.bittorrent.org/beps/bep_0010.html#handshake-message). ([#5643](https://github.com/transmission/transmission/pull/5643))
* Setting `"cache-size-mb": 0` in `settings.json` now disables the disk write cache. ([#5668](https://github.com/transmission/transmission/pull/5668))
* Improved libtransmission code to use less CPU and RAM. ([#5801](https://github.com/transmission/transmission/pull/5801))
* Transmission now checks if local files exists after setting torrent location. ([#5978](https://github.com/transmission/transmission/pull/5978))
* Improved handling of plaintext and MSE handshakes. ([#6025](https://github.com/transmission/transmission/pull/6025))
* If a torrent contains empty (zero byte) files, create them when starting the torrent. ([#6232](https://github.com/transmission/transmission/pull/6232))
* Improved DHT performance. ([#6569](https://github.com/transmission/transmission/pull/6569), [#6695](https://github.com/transmission/transmission/pull/6695))
* Added advanced `sleep-per-seconds-during-verify` setting to `settings.json`. ([#6572](https://github.com/transmission/transmission/pull/6572))
* Improved µTP download performance. ([#6586](https://github.com/transmission/transmission/pull/6586))
* Added support for IPv6 Local Peer Discovery. ([#6700](https://github.com/transmission/transmission/pull/6700))
* Allow port forwarding state to automatically recover from error. ([#6718](https://github.com/transmission/transmission/pull/6718))
* Save upload/download queue order between sessions. ([#6753](https://github.com/transmission/transmission/pull/6753))
* Make client `reqq` configurable. ([#7030](https://github.com/transmission/transmission/pull/7030))
* Daemon log timestamps are now in local ISO8601 format. ([#7057](https://github.com/transmission/transmission/pull/7057))
* Fixed crash in `tr_peerMgrPeerStats()`. ([#5279](https://github.com/transmission/transmission/pull/5279))
* Fixed bug in sending torrent metadata to peers. ([#5460](https://github.com/transmission/transmission/pull/5460))
* Avoid unnecessary heap memory allocations. ([#5519](https://github.com/transmission/transmission/pull/5519), [#5520](https://github.com/transmission/transmission/pull/5520), [#5522](https://github.com/transmission/transmission/pull/5522), [#5527](https://github.com/transmission/transmission/pull/5527), [#5540](https://github.com/transmission/transmission/pull/5540), [#5649](https://github.com/transmission/transmission/pull/5649), [#5666](https://github.com/transmission/transmission/pull/5666), [#5672](https://github.com/transmission/transmission/pull/5672), [#5676](https://github.com/transmission/transmission/pull/5676), [#5720](https://github.com/transmission/transmission/pull/5720), [#5722](https://github.com/transmission/transmission/pull/5722), [#5725](https://github.com/transmission/transmission/pull/5725), [#5726](https://github.com/transmission/transmission/pull/5726), [#5768](https://github.com/transmission/transmission/pull/5768), [#5788](https://github.com/transmission/transmission/pull/5788), [#5830](https://github.com/transmission/transmission/pull/5830))
* Fixed filename collision edge case when renaming files. ([#5563](https://github.com/transmission/transmission/pull/5563))
* Fixed locale errors that broke number rounding when displaying statistics, e.g. upload / download ratios. ([#5587](https://github.com/transmission/transmission/pull/5587))
* In RPC responses, change the default sort order of torrents to match Transmission 3.00. ([#5604](https://github.com/transmission/transmission/pull/5604))
* Improved handling of multiple connections from the same IP address. ([#5619](https://github.com/transmission/transmission/pull/5619))
* Always use a fixed-length key query in tracker announces. This isn't required by the [spec](https://www.bittorrent.org/beps/bep_0007.html), but some trackers rely on that fixed length because it's common practice by other BitTorrent clients. ([#5652](https://github.com/transmission/transmission/pull/5652))
* Fixed minor performance bug that caused disk writes to be made in smaller batches than intended. ([#5671](https://github.com/transmission/transmission/pull/5671))
* Fixed potential Windows crash when [getstdhandle()](https://learn.microsoft.com/en-us/windows/console/getstdhandle) returns `NULL`. ([#5675](https://github.com/transmission/transmission/pull/5675))
* Modified LTEP to advertise PEX support more proactively, and added an sanity check for magnet metadata exchange. ([#5783](https://github.com/transmission/transmission/pull/5783))
* Fixed `4.0.0` bug where the port numbers in LPD announces are sometimes malformed. ([#5825](https://github.com/transmission/transmission/pull/5825))
* Fixed a bug that prevented editing the query part of a tracker URL. ([#5871](https://github.com/transmission/transmission/pull/5871))
* Fixed a bug where Transmission may not announce LPD on its listening interface. ([#5875](https://github.com/transmission/transmission/pull/5875))
* Fixed a bug that prevented editing trackers on magnet links. ([#5957](https://github.com/transmission/transmission/pull/5957))
* Fixed HTTP tracker announces and scrapes sometimes failing after adding a torrent file by HTTPS URL. ([#5969](https://github.com/transmission/transmission/pull/5969))
* Fixed blocklist error seen on some Synology devices due to a bug in `tr_sys_path_copy()`. ([#5974](https://github.com/transmission/transmission/pull/5974))
* Run peerMgrPeerStats in session thread. ([#5992](https://github.com/transmission/transmission/pull/5992))
* In some locales, some JSON stirngs were incorrectly escaped. ([#6005](https://github.com/transmission/transmission/pull/6005))
* Fixed `1.60` bug where low priority torrents behaved as if they had a normal priority. ([#6079](https://github.com/transmission/transmission/pull/6079))
* Fixed `4.0.4` regression that could cause slower downloads when upload speed limits were enabled. ([#6082](https://github.com/transmission/transmission/pull/6082))
* Fixed `4.0.0` bug where the `IP address` field in UDP announces were not encoded in network byte order. [[BEP-15](https://www.bittorrent.org/beps/bep_0015.html)]. ([#6126](https://github.com/transmission/transmission/pull/6126))
* Improved parsing HTTP tracker announce response. ([#6223](https://github.com/transmission/transmission/pull/6223))
* Fixed `4.0.0` bugs where some RPC methods don't put torrents in `recently-active` anymore. ([#6355](https://github.com/transmission/transmission/pull/6355), [#6405](https://github.com/transmission/transmission/pull/6405))
* Fixed error when using mbedtls crypto backend:  "CTR_DRBG - The requested random buffer length is too big". ([#6379](https://github.com/transmission/transmission/pull/6379))
* Fixed `4.0.0` bug that caused some user scripts to have an invalid `TR_TORRENT_TRACKERS` environment variable. ([#6434](https://github.com/transmission/transmission/pull/6434))
* Added optional sequential downloading. ([#6450](https://github.com/transmission/transmission/pull/6450), [#6746](https://github.com/transmission/transmission/pull/6746))
* Fixed `4.0.0` bug where `alt-speed-enabled` had no effect in `settings.json`. ([#6483](https://github.com/transmission/transmission/pull/6483))
* Fixed `4.0.0` bug where the GTK client's "Use authentication" option was not saved between's sessions. ([#6514](https://github.com/transmission/transmission/pull/6514))
* Fixed `4.0.0` bug where `secondsDownloading` and `secondsSeeding` will be reset when stopping the torrent. ([#6844](https://github.com/transmission/transmission/pull/6844))
* Fixed `4.0.0` bug where the filename for single-file torrents aren't sanitized. ([#6846](https://github.com/transmission/transmission/pull/6846))
* Partial file suffixes will now be updated after torrent verification. ([#6871](https://github.com/transmission/transmission/pull/6871))
* Limit the number of bad pieces to accept from a webseed before banning it. ([#6875](https://github.com/transmission/transmission/pull/6875))
* Fixed a `4.0.0` bug where `2.20`-`3.00` torrent piece timestamps saved in the resume file aren't loaded correctly. ([#6896](https://github.com/transmission/transmission/pull/6896))
* Fixed a bug that could discard BT messages that immediately followed a handshake. ([#6913](https://github.com/transmission/transmission/pull/6913))
* Various bug fixes and improvements related to PEX flags. ([#6917](https://github.com/transmission/transmission/pull/6917))
* Fixed a bug where the turtle icon is active but not effective on starting Transmission. ([#6937](https://github.com/transmission/transmission/pull/6937))
* Fixed a bug where Transmission does not properly reconnect on handshake error. ([#6950](https://github.com/transmission/transmission/pull/6950))
* Fixed edge cases where `date done` and `recently-active` does not get updated after torrent state change. ([#6992](https://github.com/transmission/transmission/pull/6992))
* Fixed a `4.0.0` bug where the tracker error is not cleared when the tracker is removed from the torrent. ([#7141](https://github.com/transmission/transmission/pull/7141))
* Fixed a bug where torrent progress is not properly updated after verifying. ([#7143](https://github.com/transmission/transmission/pull/7143))
* Fixed `1.74` bug where resume files are not saved when shutting down Transmission. ([#7216](https://github.com/transmission/transmission/pull/7216))
* Fixed `4.0.0` bug where the download rate of webseeds are double-counted. ([#7235](https://github.com/transmission/transmission/pull/7235))
* Improved libtransmission code to use less CPU. ([#4876](https://github.com/transmission/transmission/pull/4876), [#5645](https://github.com/transmission/transmission/pull/5645), [#5715](https://github.com/transmission/transmission/pull/5715), [#5734](https://github.com/transmission/transmission/pull/5734), [#5740](https://github.com/transmission/transmission/pull/5740), [#5792](https://github.com/transmission/transmission/pull/5792), [#6103](https://github.com/transmission/transmission/pull/6103), [#6111](https://github.com/transmission/transmission/pull/6111), [#6325](https://github.com/transmission/transmission/pull/6325), [#6549](https://github.com/transmission/transmission/pull/6549), [#6589](https://github.com/transmission/transmission/pull/6589), [#6712](https://github.com/transmission/transmission/pull/6712), [#7027](https://github.com/transmission/transmission/pull/7027))
* Slightly reduced latency when sending protocol messages to peers. ([#5394](https://github.com/transmission/transmission/pull/5394))
* Fixed a bug where disk IO rate is much higher than transfer rate. ([#7089](https://github.com/transmission/transmission/pull/7089))
* Refactor: prefer direct-brace-initialization. ([#5803](https://github.com/transmission/transmission/pull/5803))
* Refactor: add tr_variant_serde. ([#5903](https://github.com/transmission/transmission/pull/5903))
* Dropped jsonsl in favour of RapidJSON as our json lexer. ([#6138](https://github.com/transmission/transmission/pull/6138))
* Easier recovery from temporarily missing data files, no longer needing to remove and re-add torrent. ([#6277](https://github.com/transmission/transmission/pull/6277))
* Use a consistent unit formatting code between clients. ([#5108](https://github.com/transmission/transmission/pull/5108))
* Chore: bump minimum openssl version to 1.1.0. ([#6047](https://github.com/transmission/transmission/pull/6047))
* Fixed libtransmission build on very old cmake versions. ([#6418](https://github.com/transmission/transmission/pull/6418))
* Build: fix building on macOS 10.14.6, 10.15.7 and 11.7. ([#6590](https://github.com/transmission/transmission/pull/6590))
* Added torrent priority to completion script environment variables. ([#6629](https://github.com/transmission/transmission/pull/6629))
* Dropped support for miniupnpc version below `1.7`. ([#6665](https://github.com/transmission/transmission/pull/6665))
* Bumping libdeflate/small/utfcpp to newer versions. ([#6709](https://github.com/transmission/transmission/pull/6709))
* Bumped fast-float to 6.1.1 and miniupnpc to 2.2.7 and libdeflate to 1.2.0. ([#6721](https://github.com/transmission/transmission/pull/6721))
* Default initialize sleep callback duration in tr_verify_worker. ([#6789](https://github.com/transmission/transmission/pull/6789))
* Bumped miniupnpc to 2.2.8. ([#6907](https://github.com/transmission/transmission/pull/6907))
* Compatibility with libfmt v11. ([#6979](https://github.com/transmission/transmission/pull/6979))
* Removed TR_ASSERT(now >= latest). ([#7018](https://github.com/transmission/transmission/pull/7018))
* Updated documentation. ([#7044](https://github.com/transmission/transmission/pull/7044))

### macOS Client

* Added "Show Toolbar" toggle. ([#4419](https://github.com/transmission/transmission/pull/4419))
* Feat: add stats for known peers, not just connected ones. ([#4900](https://github.com/transmission/transmission/pull/4900))
* MacOS convert TorrentTableView to view based. ([#5147](https://github.com/transmission/transmission/pull/5147))
* Feat: support redirects to magnet. ([#6012](https://github.com/transmission/transmission/pull/6012))
* Render file tree in QuickLook plugin for .torrent files. ([#6091](https://github.com/transmission/transmission/pull/6091))
* Added an option to set Transmission as the default app for torrent files. ([#6099](https://github.com/transmission/transmission/pull/6099))
* Support Dark Mode and update default font in QuickLook plugin. ([#6101](https://github.com/transmission/transmission/pull/6101))
* Support pasting multiple magnets on the same line. ([#6465](https://github.com/transmission/transmission/pull/6465))
* Support multiple URL objects from pasteboard. ([#6467](https://github.com/transmission/transmission/pull/6467))
* Feat: clear the badge when quitting app. ([#7088](https://github.com/transmission/transmission/pull/7088))
* Fix: apply i18n to percentage values. ([#5568](https://github.com/transmission/transmission/pull/5568))
* Fixed "Unrecognized colorspace number -1" error message. ([#6049](https://github.com/transmission/transmission/pull/6049))
* Fix: URL cleanup in BlocklistDownloader on macOS. ([#6096](https://github.com/transmission/transmission/pull/6096))
* Fixed early truncation of long group names in groups list. ([#6104](https://github.com/transmission/transmission/pull/6104))
* Use screen.visibleFrame instead of screen.frame. ([#6321](https://github.com/transmission/transmission/pull/6321))
* Fix: support dark mode colors with pieces bar on macOS. ([#6959](https://github.com/transmission/transmission/pull/6959))
* Fixed dock bug that prevented resizing. ([#7188](https://github.com/transmission/transmission/pull/7188))
* Added alternating row color in QuickLook plugin. ([#5216](https://github.com/transmission/transmission/pull/5216))
* Refactor: remove unnecessary NSNotificationCenter observer removals. ([#5118](https://github.com/transmission/transmission/pull/5118))
* Refactor: move macOS default app logic to dedicated class. ([#6120](https://github.com/transmission/transmission/pull/6120))
* Added sort-by-ETA option. ([#4169](https://github.com/transmission/transmission/pull/4169))
* Support localized punctuation for "Port:". ([#4452](https://github.com/transmission/transmission/pull/4452))
* Replace mac app default BindPort with a random port. ([#5102](https://github.com/transmission/transmission/pull/5102))
* Updated code that had been using deprecated API. ([#5633](https://github.com/transmission/transmission/pull/5633))
* Support macOS Sonoma when building from sources. ([#6016](https://github.com/transmission/transmission/pull/6016))
* Chore: replace deprecated NSNamePboardType with NSPasteboardTypeName. ([#6107](https://github.com/transmission/transmission/pull/6107))
* Fixed building on macOS Mojave. ([#6180](https://github.com/transmission/transmission/pull/6180))
* Improved macOS UI code to use less CPU. ([#6452](https://github.com/transmission/transmission/pull/6452))
* Fixed app unable to start when having many torrents and TimeMachine enabled. ([#6523](https://github.com/transmission/transmission/pull/6523))
* Support finding Transmission in Spotlight with keywords "torrent" and "magnet". ([#6578](https://github.com/transmission/transmission/pull/6578))
* Removed warning "don't cut off end". ([#6890](https://github.com/transmission/transmission/pull/6890))
* Opt-in to secure coding explicitly. ([#7020](https://github.com/transmission/transmission/pull/7020))

### Qt Client

* Added ETA to compact view. ([#3926](https://github.com/transmission/transmission/pull/3926))
* Added the web client's Labels feature. ([#6428](https://github.com/transmission/transmission/pull/6428))
* Fixed torrent name rendering when showing magnet links in compact view. ([#5491](https://github.com/transmission/transmission/pull/5491))
* Fixed bug that broke the "Move torrent file to trash" setting. ([#5505](https://github.com/transmission/transmission/pull/5505))
* Fixed Qt 6.4 deprecation warning. ([#5552](https://github.com/transmission/transmission/pull/5552))
* Fixed poor resolution of Qt application icon. ([#5570](https://github.com/transmission/transmission/pull/5570))
* Fix: only append '.added' suffix to watchdir files. ([#5705](https://github.com/transmission/transmission/pull/5705))
* Fixed compatibility issue with 4.x clients talking to Transmission 3.x servers. ([#6438](https://github.com/transmission/transmission/pull/6438))
* Fixed `4.0.0` bug where piece size description text and slider state in torrent creation dialog are not always up-to-date. ([#6516](https://github.com/transmission/transmission/pull/6516))
* Use semi-transparent color for inactive torrents. ([#6544](https://github.com/transmission/transmission/pull/6544))
* Correct "Queue for download" last activity. ([#6872](https://github.com/transmission/transmission/pull/6872))
* Avoid unnecessary heap memory allocations. ([#6542](https://github.com/transmission/transmission/pull/6542))
* Fixed spinbox translation ambiguity. ([#5124](https://github.com/transmission/transmission/pull/5124))
* Improved Qt client's accessibility. ([#6518](https://github.com/transmission/transmission/pull/6518), [#6520](https://github.com/transmission/transmission/pull/6520))
* Changed Qt client CLI options parsing to accept Qt options as a separate group. ([#7076](https://github.com/transmission/transmission/pull/7076))
* Modified the "New Torrent" dialog's piece size range to [16 KiB..256 MiB]. ([#6211](https://github.com/transmission/transmission/pull/6211))

### GTK Client

* The Qt and GTK Client now does separate port checks for IPv4 and IPv6. ([#6525](https://github.com/transmission/transmission/pull/6525))
* Use native file chooser dialogs (GTK client). ([#6545](https://github.com/transmission/transmission/pull/6545))
* Fixed missing 'Remove torrent' tooltip. ([#5777](https://github.com/transmission/transmission/pull/5777))
* If there was some disk error with torrent removal, fail with a user readable error message. ([#6055](https://github.com/transmission/transmission/pull/6055))
* Setting default behaviour for GTK dialogs to add torrent from url and add tracker. ([#7102](https://github.com/transmission/transmission/pull/7102))
* Fixed crash when opening torrent file from "Recently used" section in GTK 4. ([#6131](https://github.com/transmission/transmission/pull/6131))
* Fix: Avoid ambiguous overload when calling Gdk::Cursor::create. ([#7070](https://github.com/transmission/transmission/pull/7070))
* Fixed `4.0.0` regression causing GTK client to hang in some cases.
Broken-by: #4430. ([#7097](https://github.com/transmission/transmission/pull/7097))
* GTK client progress bar colours updated to match Web and macOS clients. ([#5906](https://github.com/transmission/transmission/pull/5906))
* Fix: QT build missing an icon. ([#6683](https://github.com/transmission/transmission/pull/6683))
* Improved GTK client's accessibility. ([#7119](https://github.com/transmission/transmission/pull/7119))
* Added `developer_name` entry to the Flathub build. ([#6596](https://github.com/transmission/transmission/pull/6596))
* Fixed file list text size adjustment based on global settings. ([#7096](https://github.com/transmission/transmission/pull/7096))

### Web Client

* Added support for adding torrents by drag-and-drop. ([#5082](https://github.com/transmission/transmission/pull/5082))
* Added high contrast theme. ([#5470](https://github.com/transmission/transmission/pull/5470))
* Replaced background colors with system color keywords to enable using browser's colors. CSS style adjustments esp. for label and buttons. ([#5897](https://github.com/transmission/transmission/pull/5897))
* Added percent digits into the progress bar. ([#5937](https://github.com/transmission/transmission/pull/5937))
* Improved WebUI responsiveness and made quality of life improvements. ([#5947](https://github.com/transmission/transmission/pull/5947))
* The WebUI now does separate port checks for IPv4 and IPv6. ([#5953](https://github.com/transmission/transmission/pull/5953), [#6607](https://github.com/transmission/transmission/pull/6607))
* Added forced variant of the "Verify Local Data" context menu item to WebUI. ([#5981](https://github.com/transmission/transmission/pull/5981))
* Feat: Only show .torrent files in the web UI. ([#6320](https://github.com/transmission/transmission/pull/6320))
* The inspector can now be hidden by clicking. ([#6863](https://github.com/transmission/transmission/pull/6863))
* Don't show `null` as a tier name in the inspector's tier list. ([#5462](https://github.com/transmission/transmission/pull/5462))
* Fixed truncated play / pause icons. ([#5771](https://github.com/transmission/transmission/pull/5771))
* Fixed overflow when rendering peer lists and made speed indicators honor `prefers-color-scheme` media queries. ([#5814](https://github.com/transmission/transmission/pull/5814))
* Made the main menu accessible even on smaller displays. ([#5827](https://github.com/transmission/transmission/pull/5827))
* WebUI: Fix graying out inspector. ([#5893](https://github.com/transmission/transmission/pull/5893))
* Fixed updating magnet link after selecting same torrent again. ([#6028](https://github.com/transmission/transmission/pull/6028))
* Fix: add seed progress percentage to compact rows. ([#6034](https://github.com/transmission/transmission/pull/6034))
* Fixed `4.0.0` bug where the WebUI "Set Location" dialogue does not auto fill the selected torrent's current download location. ([#6334](https://github.com/transmission/transmission/pull/6334))
* Fixed `4.0.5` bug where svg and png icons in the WebUI might not be displayed. ([#6409](https://github.com/transmission/transmission/pull/6409), [#6430](https://github.com/transmission/transmission/pull/6430))
* Fixed a `4.0.0` bug where the infinite ratio symbol was displayed incorrectly in the WebUI. ([#6491](https://github.com/transmission/transmission/pull/6491))
* Fix(web): pressing the enter key now submits dialogs. ([#7036](https://github.com/transmission/transmission/pull/7036))
* Removed excessive `session-set` RPC calls related to WebUI preference dialogue. ([#5994](https://github.com/transmission/transmission/pull/5994))
* Removed modifiers for keyboard shortcuts. ([#5331](https://github.com/transmission/transmission/pull/5331))
* Improved some UI styling and spacing. ([#5466](https://github.com/transmission/transmission/pull/5466))
* Updated WebUI progress bar and highlight colours. ([#5762](https://github.com/transmission/transmission/pull/5762))
* WebUI: Improved filterbar for narrowed viewports. ([#5828](https://github.com/transmission/transmission/pull/5828))
* Unified CSS shadow properties. ([#5840](https://github.com/transmission/transmission/pull/5840))
* Feat: webui use monochrome icons for play/pause buttons. ([#5868](https://github.com/transmission/transmission/pull/5868))
* Improved overflow menu for web client. ([#5895](https://github.com/transmission/transmission/pull/5895))
* Added display and time in torrent detail. ([#5918](https://github.com/transmission/transmission/pull/5918))
* Added touchscreen support in the context menu. ([#5928](https://github.com/transmission/transmission/pull/5928))
* Fixed truncated hash in inspector page, added name section to inspector page. ([#7014](https://github.com/transmission/transmission/pull/7014))
* Updated gray color for grayed out objects. ([#7248](https://github.com/transmission/transmission/pull/7248))
* Updated displaying number in new gigabyte per second unit. ([#7279](https://github.com/transmission/transmission/pull/7279))
* Increased base font sizes, and progress bar size in compact view. ([#5340](https://github.com/transmission/transmission/pull/5340))
* Use [`esbuild`](https://esbuild.github.io/) to build the web client. ([#6280](https://github.com/transmission/transmission/pull/6280))

### Daemon

* Fixed minor memory leak. ([#5695](https://github.com/transmission/transmission/pull/5695))
* Fixed a couple of logging issues. ([#6463](https://github.com/transmission/transmission/pull/6463))
* Updated documentation. ([#6499](https://github.com/transmission/transmission/pull/6499))
* More accurate timestamps for daemon logs. ([#7009](https://github.com/transmission/transmission/pull/7009))
* Avoid unnecessary heap memory allocations. ([#5724](https://github.com/transmission/transmission/pull/5724))
* Updated transmission-daemon.1 to sync with --help. ([#6059](https://github.com/transmission/transmission/pull/6059))
* Added start_paused to settings and daemon. ([#6728](https://github.com/transmission/transmission/pull/6728))
* Added documentation key to systemd service file. ([#6781](https://github.com/transmission/transmission/pull/6781))

### transmission-cli

* Fixed "no such file or directory" warning when adding a magnet link. ([#5426](https://github.com/transmission/transmission/pull/5426))
* Fixed bug that caused the wrong decimal separator to be used in some locales. ([#5444](https://github.com/transmission/transmission/pull/5444))
* Fix: restore tr_optind in all getConfigDir branches. ([#6920](https://github.com/transmission/transmission/pull/6920))
* Refactor: add libtransmission::Values. ([#6215](https://github.com/transmission/transmission/pull/6215))

### transmission-remote

* Remote: Implement idle seeding limits. ([#2947](https://github.com/transmission/transmission/pull/2947))
* Fixed display bug that failed to show some torrent labels. ([#5572](https://github.com/transmission/transmission/pull/5572))
* Added optional sequential downloading. ([#6454](https://github.com/transmission/transmission/pull/6454))
* Fixed crash in printTorrentList. ([#6819](https://github.com/transmission/transmission/pull/6819))
* Added 'months' and 'years' to ETA display for extremely slow torrents. ([#5584](https://github.com/transmission/transmission/pull/5584))
* Transmission-remote: for '-l', implement default sorting by addedDate. ([#5608](https://github.com/transmission/transmission/pull/5608))

### Everything Else

* Sanitize torrent filenames depending on current OS. ([#3823](https://github.com/transmission/transmission/pull/3823))
* Fixed building Xcode project. ([#5521](https://github.com/transmission/transmission/pull/5521))
* Handle IPv6 NAT during LTEP handshake. ([#5565](https://github.com/transmission/transmission/pull/5565))
* Expose files' begin and end pieces via RPC. ([#5578](https://github.com/transmission/transmission/pull/5578))
* Updated the torrent creator's default piece size to handle very large torrents better. ([#5615](https://github.com/transmission/transmission/pull/5615))
* Updated documentation. ([#5688](https://github.com/transmission/transmission/pull/5688), [#6083](https://github.com/transmission/transmission/pull/6083), [#6196](https://github.com/transmission/transmission/pull/6196), [#6199](https://github.com/transmission/transmission/pull/6199), [#6367](https://github.com/transmission/transmission/pull/6367), [#6677](https://github.com/transmission/transmission/pull/6677), [#6800](https://github.com/transmission/transmission/pull/6800), [#6828](https://github.com/transmission/transmission/pull/6828), [#7120](https://github.com/transmission/transmission/pull/7120))
* Improved libtransmission code to use less CPU. ([#5651](https://github.com/transmission/transmission/pull/5651))
* Sync translations. ([#6453](https://github.com/transmission/transmission/pull/6453))
* Ran all PNG files through lossless compressors to make them smaller. ([#5586](https://github.com/transmission/transmission/pull/5586))
* Fixed potential build issue when compiling on macOS with gcc. ([#5632](https://github.com/transmission/transmission/pull/5632))
* Fix: conform to libcurl requirements to avoid memory leak. ([#5702](https://github.com/transmission/transmission/pull/5702))
* Chore: update generated transmission-web files. ([#5790](https://github.com/transmission/transmission/pull/5790), [#5831](https://github.com/transmission/transmission/pull/5831), [#6037](https://github.com/transmission/transmission/pull/6037), [#6156](https://github.com/transmission/transmission/pull/6156), [#6427](https://github.com/transmission/transmission/pull/6427))
* Improved support for building with the NDK on Android. ([#6024](https://github.com/transmission/transmission/pull/6024))
* Bumped [fast_float](https://github.com/fastfloat/fast_float/) snapshot to [v5.3.0](https://github.com/fastfloat/fast_float/releases/tag/v5.3.0). ([#6255](https://github.com/transmission/transmission/pull/6255))
* Chore: bump fmt to 10.1.1. ([#6358](https://github.com/transmission/transmission/pull/6358))
* GTK: Add 4.0.4 and 4.0.5 releases to metainfo. ([#6378](https://github.com/transmission/transmission/pull/6378))
* Hardened systemd service settings. ([#6391](https://github.com/transmission/transmission/pull/6391))
* Updated miniunpnp snapshot to 2.3.4 (miniupnpc 2.2.6). ([#6459](https://github.com/transmission/transmission/pull/6459))
* Chore: fix warnings when compiling macOS client with either Xcode or CMake. ([#6676](https://github.com/transmission/transmission/pull/6676))
* Added sanitizer-tests-macos. ([#6703](https://github.com/transmission/transmission/pull/6703))
* Added documentation for building `transmission-qt` on macOS. ([#6814](https://github.com/transmission/transmission/pull/6814))
* Build with -latomic on platforms that need it. ([#6774](https://github.com/transmission/transmission/pull/6774))
* Fixed building with mbedtls 3.X. ([#6822](https://github.com/transmission/transmission/pull/6822))
* Build: enable clang-tidy by default. ([#6989](https://github.com/transmission/transmission/pull/6989))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @1100101 ([Frank Aurich](https://github.com/1100101)):
  * Code review. ([#6177](https://github.com/transmission/transmission/pull/6177))
* @andreygursky:
  * Code review. ([#6232](https://github.com/transmission/transmission/pull/6232), [#6277](https://github.com/transmission/transmission/pull/6277), [#6332](https://github.com/transmission/transmission/pull/6332), [#6572](https://github.com/transmission/transmission/pull/6572))
* @basilefff ([Василий Чай](https://github.com/basilefff)):
  * Fixed filename collision edge case when renaming files. ([#5563](https://github.com/transmission/transmission/pull/5563))
* @chantzish:
  * Fixed a bug that prevented editing trackers on magnet links. ([#5957](https://github.com/transmission/transmission/pull/5957))
* @fredo-47:
  * Changed some log-levels in global-ip-cache.cc from info to debug. ([#5870](https://github.com/transmission/transmission/pull/5870))
* @frozenpandaman ([eli](https://github.com/frozenpandaman)):
  * Code review. ([#6753](https://github.com/transmission/transmission/pull/6753))
* @G-Ray ([Geoffrey Bonneville](https://github.com/G-Ray)):
  * Fixed potential Windows crash when [getstdhandle()](https://learn.microsoft.com/en-us/windows/console/getstdhandle) returns `NULL`. ([#5675](https://github.com/transmission/transmission/pull/5675))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Typos in libtransmission source code comments. ([#5473](https://github.com/transmission/transmission/pull/5473))
* @hgy59:
  * Fixed blocklist error seen on some Synology devices due to a bug in `tr_sys_path_copy()`. ([#5974](https://github.com/transmission/transmission/pull/5974))
* @killemov:
  * Code review. ([#6753](https://github.com/transmission/transmission/pull/6753))
* @kmikita:
  * Refactor: save stats.json periodically and when closing session #5476. ([#5490](https://github.com/transmission/transmission/pull/5490))
* @KyleSanderson ([Kyle Sanderson](https://github.com/KyleSanderson)):
  * Code review. ([#7030](https://github.com/transmission/transmission/pull/7030))
  * Fixed uninitialized session_id_t values. ([#5396](https://github.com/transmission/transmission/pull/5396))
* @LaserEyess:
  * Fixup: dedup tr_rpc_address with tr_address. ([#5523](https://github.com/transmission/transmission/pull/5523))
  * Fix: fix broken unix socket support. ([#5665](https://github.com/transmission/transmission/pull/5665))
* @midzer:
  * Improved libtransmission code to use less CPU. ([#4876](https://github.com/transmission/transmission/pull/4876))
* @Mrnikifabio ([Mrnikifabio](https://github.com/Mrnikifabio)):
  * Added advanced `sleep-per-seconds-during-verify` setting to `settings.json`. ([#6572](https://github.com/transmission/transmission/pull/6572))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#6138](https://github.com/transmission/transmission/pull/6138), [#6179](https://github.com/transmission/transmission/pull/6179), [#6418](https://github.com/transmission/transmission/pull/6418), [#6665](https://github.com/transmission/transmission/pull/6665), [#6672](https://github.com/transmission/transmission/pull/6672), [#6735](https://github.com/transmission/transmission/pull/6735), [#6949](https://github.com/transmission/transmission/pull/6949), [#7187](https://github.com/transmission/transmission/pull/7187), [#7233](https://github.com/transmission/transmission/pull/7233), [#7266](https://github.com/transmission/transmission/pull/7266))
  * Fix: crash after nullptr dereference in rpcimpl. ([#6177](https://github.com/transmission/transmission/pull/6177))
  * Use std::declval instead of nullptr cast trick. ([#6785](https://github.com/transmission/transmission/pull/6785))
  * Added virtual destructor to the polymorphic Settings class. ([#6786](https://github.com/transmission/transmission/pull/6786))
  * Make std::hash specialization for tr_socket_address a struct. ([#6788](https://github.com/transmission/transmission/pull/6788))
  * Default initialize sleep callback duration in tr_verify_worker. ([#6789](https://github.com/transmission/transmission/pull/6789))
  * Updated outdated Doxygen params refs for tr_recentHistory. ([#6791](https://github.com/transmission/transmission/pull/6791))
* @nmaggioni ([Niccolò Maggioni](https://github.com/nmaggioni)):
  * Added torrent priority to completion script environment variables. ([#6629](https://github.com/transmission/transmission/pull/6629))
* @pldubouilh ([Pierre Dubouilh](https://github.com/pldubouilh)):
  * Added optional sequential downloading. ([#4795](https://github.com/transmission/transmission/pull/4795))
* @qu1ck:
  * Added optional sequential downloading. ([#6450](https://github.com/transmission/transmission/pull/6450))
* @qzydustin ([Zhenyu Qi](https://github.com/qzydustin)):
  * Fixed a bug that prevented editing the query part of a tracker URL. ([#5871](https://github.com/transmission/transmission/pull/5871))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Code review. ([#6891](https://github.com/transmission/transmission/pull/6891), [#6901](https://github.com/transmission/transmission/pull/6901), [#6913](https://github.com/transmission/transmission/pull/6913), [#6917](https://github.com/transmission/transmission/pull/6917), [#6918](https://github.com/transmission/transmission/pull/6918), [#6922](https://github.com/transmission/transmission/pull/6922), [#6929](https://github.com/transmission/transmission/pull/6929), [#6949](https://github.com/transmission/transmission/pull/6949), [#6950](https://github.com/transmission/transmission/pull/6950), [#6969](https://github.com/transmission/transmission/pull/6969), [#6975](https://github.com/transmission/transmission/pull/6975), [#6976](https://github.com/transmission/transmission/pull/6976), [#6992](https://github.com/transmission/transmission/pull/6992), [#7027](https://github.com/transmission/transmission/pull/7027), [#7030](https://github.com/transmission/transmission/pull/7030), [#7089](https://github.com/transmission/transmission/pull/7089), [#7143](https://github.com/transmission/transmission/pull/7143), [#7204](https://github.com/transmission/transmission/pull/7204))
  * Clarify MSE crypto algo usage. ([#6957](https://github.com/transmission/transmission/pull/6957))
  * Chore: clarify DhtPort specific usage. ([#6963](https://github.com/transmission/transmission/pull/6963))
* @titer ([Eric Petit](https://github.com/titer)):
  * Code review. ([#5329](https://github.com/transmission/transmission/pull/5329))

### Contributions to macOS Client:

* @Artoria2e5 ([Mingye Wang](https://github.com/Artoria2e5)):
  * Use screen.visibleFrame instead of screen.frame. ([#6321](https://github.com/transmission/transmission/pull/6321))
* @bitigchi ([Emir SARI](https://github.com/bitigchi)):
  * Fix: apply i18n to percentage values. ([#5568](https://github.com/transmission/transmission/pull/5568))
* @cleberpereiradasilva ([Cleber Pereira da Silva](https://github.com/cleberpereiradasilva)):
  * Code review. ([#6120](https://github.com/transmission/transmission/pull/6120))
* @DevilDimon ([Dmitry Serov](https://github.com/DevilDimon)):
  * Code review. ([#5147](https://github.com/transmission/transmission/pull/5147), [#5221](https://github.com/transmission/transmission/pull/5221), [#6120](https://github.com/transmission/transmission/pull/6120), [#6613](https://github.com/transmission/transmission/pull/6613))
  * Refactor: use idiomatic enum names & types in objc. ([#5090](https://github.com/transmission/transmission/pull/5090))
  * Refactor: remove unnecessary NSNotificationCenter observer removals. ([#5118](https://github.com/transmission/transmission/pull/5118))
* @fxcoudert ([FX Coudert](https://github.com/fxcoudert)):
  * Code review. ([#5282](https://github.com/transmission/transmission/pull/5282), [#6184](https://github.com/transmission/transmission/pull/6184), [#6214](https://github.com/transmission/transmission/pull/6214))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Typos in macosx. ([#5475](https://github.com/transmission/transmission/pull/5475))
* @i0ntempest ([Zhenfu Shi](https://github.com/i0ntempest)):
  * Fix: wrong case in AppKit.h. ([#5456](https://github.com/transmission/transmission/pull/5456))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#4419](https://github.com/transmission/transmission/pull/4419), [#4900](https://github.com/transmission/transmission/pull/4900), [#4919](https://github.com/transmission/transmission/pull/4919), [#5090](https://github.com/transmission/transmission/pull/5090), [#5102](https://github.com/transmission/transmission/pull/5102), [#5147](https://github.com/transmission/transmission/pull/5147), [#5263](https://github.com/transmission/transmission/pull/5263), [#5282](https://github.com/transmission/transmission/pull/5282), [#5456](https://github.com/transmission/transmission/pull/5456), [#5633](https://github.com/transmission/transmission/pull/5633), [#5844](https://github.com/transmission/transmission/pull/5844), [#5846](https://github.com/transmission/transmission/pull/5846), [#6012](https://github.com/transmission/transmission/pull/6012), [#6096](https://github.com/transmission/transmission/pull/6096), [#6104](https://github.com/transmission/transmission/pull/6104), [#6184](https://github.com/transmission/transmission/pull/6184), [#6214](https://github.com/transmission/transmission/pull/6214), [#6242](https://github.com/transmission/transmission/pull/6242), [#6290](https://github.com/transmission/transmission/pull/6290), [#6292](https://github.com/transmission/transmission/pull/6292), [#6299](https://github.com/transmission/transmission/pull/6299), [#6452](https://github.com/transmission/transmission/pull/6452), [#6465](https://github.com/transmission/transmission/pull/6465), [#6489](https://github.com/transmission/transmission/pull/6489), [#6523](https://github.com/transmission/transmission/pull/6523), [#6578](https://github.com/transmission/transmission/pull/6578), [#6591](https://github.com/transmission/transmission/pull/6591), [#6600](https://github.com/transmission/transmission/pull/6600), [#6601](https://github.com/transmission/transmission/pull/6601), [#6610](https://github.com/transmission/transmission/pull/6610), [#6611](https://github.com/transmission/transmission/pull/6611), [#6612](https://github.com/transmission/transmission/pull/6612), [#6626](https://github.com/transmission/transmission/pull/6626), [#6698](https://github.com/transmission/transmission/pull/6698), [#6911](https://github.com/transmission/transmission/pull/6911), [#7088](https://github.com/transmission/transmission/pull/7088), [#7188](https://github.com/transmission/transmission/pull/7188), [#7226](https://github.com/transmission/transmission/pull/7226))
  * Render file tree in QuickLook plugin for .torrent files. ([#6091](https://github.com/transmission/transmission/pull/6091))
  * Added an option to set Transmission as the default app for torrent files. ([#6099](https://github.com/transmission/transmission/pull/6099))
  * Support Dark Mode and update default font in QuickLook plugin. ([#6101](https://github.com/transmission/transmission/pull/6101))
  * Chore: replace deprecated NSNamePboardType with NSPasteboardTypeName. ([#6107](https://github.com/transmission/transmission/pull/6107))
  * Refactor: move macOS default app logic to dedicated class. ([#6120](https://github.com/transmission/transmission/pull/6120))
  * Feat: directly open macOS notifications preferences for app. ([#6121](https://github.com/transmission/transmission/pull/6121))
  * Fix: support dark mode colors with pieces bar on macOS. ([#6959](https://github.com/transmission/transmission/pull/6959))
* @sweetppro ([SweetPPro](https://github.com/sweetppro)):
  * Code review. ([#5102](https://github.com/transmission/transmission/pull/5102), [#5221](https://github.com/transmission/transmission/pull/5221), [#5282](https://github.com/transmission/transmission/pull/5282), [#5844](https://github.com/transmission/transmission/pull/5844), [#5846](https://github.com/transmission/transmission/pull/5846), [#5991](https://github.com/transmission/transmission/pull/5991), [#6012](https://github.com/transmission/transmission/pull/6012), [#6016](https://github.com/transmission/transmission/pull/6016), [#6053](https://github.com/transmission/transmission/pull/6053), [#6207](https://github.com/transmission/transmission/pull/6207), [#6299](https://github.com/transmission/transmission/pull/6299))
  * MacOS convert TorrentTableView to view based. ([#5147](https://github.com/transmission/transmission/pull/5147))
  * Regression fix: missing priority icon in torrent cell. ([#5856](https://github.com/transmission/transmission/pull/5856))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#4900](https://github.com/transmission/transmission/pull/4900), [#4919](https://github.com/transmission/transmission/pull/4919), [#6091](https://github.com/transmission/transmission/pull/6091))
* @wutzi15 ([Benedikt Bergenthal](https://github.com/wutzi15)):
  * Fix: URL cleanup in BlocklistDownloader on macOS. ([#6096](https://github.com/transmission/transmission/pull/6096))
* @zorgiepoo ([Zorg](https://github.com/zorgiepoo)):
  * Code review. ([#5263](https://github.com/transmission/transmission/pull/5263))

### Contributions to Qt Client:

* @dmantipov ([Dmitry Antipov](https://github.com/dmantipov)):
  * Fixed Qt 6.4 deprecation warning. ([#5552](https://github.com/transmission/transmission/pull/5552))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Fixed poor resolution of Qt application icon. ([#5570](https://github.com/transmission/transmission/pull/5570))
* @ile6695 ([Ilkka Kallioniemi](https://github.com/ile6695)):
  * Modified the "New Torrent" dialog's piece size range to [16 KiB..256 MiB]. ([#6211](https://github.com/transmission/transmission/pull/6211))
* @juliapaci ([Julia](https://github.com/juliapaci)):
  * Correct "Queue for download" last activity. ([#6872](https://github.com/transmission/transmission/pull/6872))
* @killemov:
  * Code review. ([#6872](https://github.com/transmission/transmission/pull/6872))
* @Kljunas2 ([Miha Korenjak](https://github.com/Kljunas2)):
  * Fixed spinbox translation ambiguity. ([#5124](https://github.com/transmission/transmission/pull/5124))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7192](https://github.com/transmission/transmission/pull/7192))
* @pudymody ([Federico Scodelaro](https://github.com/pudymody)):
  * Added ETA to compact view. ([#3926](https://github.com/transmission/transmission/pull/3926))
* @Schlossgeist ([Nick](https://github.com/Schlossgeist)):
  * Added the web client's Labels feature. ([#6428](https://github.com/transmission/transmission/pull/6428))
  * Fixed compatibility issue with 4.x clients talking to Transmission 3.x servers. ([#6438](https://github.com/transmission/transmission/pull/6438))
  * Qt: add dynamic RPC keys. ([#6599](https://github.com/transmission/transmission/pull/6599))
* @sfan5:
  * Code review. ([#5505](https://github.com/transmission/transmission/pull/5505))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#5704](https://github.com/transmission/transmission/pull/5704), [#6307](https://github.com/transmission/transmission/pull/6307), [#6542](https://github.com/transmission/transmission/pull/6542))
  * Fix: use correct quark for `Prefs::SHOW_TRACKER_SCRAPES`. ([#7116](https://github.com/transmission/transmission/pull/7116))
  * Chore: remove unused `TR_KEY_show_extra_peer_details` app default. ([#7117](https://github.com/transmission/transmission/pull/7117))

### Contributions to GTK Client:

* @aleasto ([Alessandro Astone](https://github.com/aleasto)):
  * Fix: Avoid ambiguous overload when calling Gdk::Cursor::create. ([#7070](https://github.com/transmission/transmission/pull/7070))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Fixed missing 'Remove torrent' tooltip. ([#5777](https://github.com/transmission/transmission/pull/5777))
  * GTK client progress bar colours updated to match Web and macOS clients. ([#5906](https://github.com/transmission/transmission/pull/5906))
* @kra-mo ([kramo](https://github.com/kra-mo)):
  * Chore: update GTK client screenshot. ([#5660](https://github.com/transmission/transmission/pull/5660))
* @lvella ([Lucas Clemente Vella](https://github.com/lvella)):
  * If there was some disk error with torrent removal, fail with a user readable error message. ([#6055](https://github.com/transmission/transmission/pull/6055))
* @mrbass21 ([Jason Beck](https://github.com/mrbass21)):
  * Fix: QT build missing an icon. ([#6683](https://github.com/transmission/transmission/pull/6683))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#6258](https://github.com/transmission/transmission/pull/6258))
  * Fixed crash when opening torrent file from "Recently used" section in GTK 4. ([#6131](https://github.com/transmission/transmission/pull/6131))
* @rafe-s ([Rafe S.](https://github.com/rafe-s)):
  * Fixed missing #include in DetailsDialog.cc. ([#5737](https://github.com/transmission/transmission/pull/5737))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#6404](https://github.com/transmission/transmission/pull/6404), [#6527](https://github.com/transmission/transmission/pull/6527), [#6608](https://github.com/transmission/transmission/pull/6608), [#6683](https://github.com/transmission/transmission/pull/6683))
  * The Qt and GTK Client now does separate port checks for IPv4 and IPv6. ([#6525](https://github.com/transmission/transmission/pull/6525))
  * Chore: delete redundant `MainWindow.ui.full`. ([#7113](https://github.com/transmission/transmission/pull/7113))
* @wjt ([Will Thompson](https://github.com/wjt)):
  * GTK: Improve appstream metainfo. ([#5407](https://github.com/transmission/transmission/pull/5407))
  * Added `developer_name` entry to the Flathub build. ([#6596](https://github.com/transmission/transmission/pull/6596))
  * Added launchable tag to metainfo files. ([#6720](https://github.com/transmission/transmission/pull/6720))

### Contributions to Web Client:

* @ask1234560 ([Ananthu](https://github.com/ask1234560)):
  * Removed modifiers for keyboard shortcuts. ([#5331](https://github.com/transmission/transmission/pull/5331))
* @bheesham ([Bheesham Persaud](https://github.com/bheesham)):
  * Fix(web): pressing the enter key now submits dialogs. ([#7036](https://github.com/transmission/transmission/pull/7036))
* @dareiff ([Derek Reiff](https://github.com/dareiff)):
  * Code review. ([#5340](https://github.com/transmission/transmission/pull/5340), [#5828](https://github.com/transmission/transmission/pull/5828), [#5868](https://github.com/transmission/transmission/pull/5868), [#5897](https://github.com/transmission/transmission/pull/5897), [#5918](https://github.com/transmission/transmission/pull/5918), [#5921](https://github.com/transmission/transmission/pull/5921), [#5928](https://github.com/transmission/transmission/pull/5928), [#5947](https://github.com/transmission/transmission/pull/5947), [#6008](https://github.com/transmission/transmission/pull/6008))
  * Added support for adding torrents by drag-and-drop. ([#5082](https://github.com/transmission/transmission/pull/5082))
  * Fixed overflow when rendering peer lists and made speed indicators honor `prefers-color-scheme` media queries. ([#5814](https://github.com/transmission/transmission/pull/5814))
  * Fix: add seed progress percentage to compact rows. ([#6034](https://github.com/transmission/transmission/pull/6034))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Increased base font sizes, and progress bar size in compact view. ([#5340](https://github.com/transmission/transmission/pull/5340))
  * Updated WebUI progress bar and highlight colours. ([#5762](https://github.com/transmission/transmission/pull/5762))
  * Fixed truncated play / pause icons. ([#5771](https://github.com/transmission/transmission/pull/5771))
  * Feat: webui use monochrome icons for play/pause buttons. ([#5868](https://github.com/transmission/transmission/pull/5868))
* @hluup ([Hendrik Luup](https://github.com/hluup)):
  * Refactor(web): use crypto.randomUUID for UUID if available. ([#6649](https://github.com/transmission/transmission/pull/6649))
  * Refactor(web): Remove unused functions from utils.js. ([#6656](https://github.com/transmission/transmission/pull/6656))
  * Fix(web): close connection failed alert on successful request. ([#6657](https://github.com/transmission/transmission/pull/6657))
  * Refactor(web): extract mime icon creation to the helper. ([#6664](https://github.com/transmission/transmission/pull/6664))
  * Fix(web): do not close all popups on successful rpc requests. ([#6675](https://github.com/transmission/transmission/pull/6675))
* @ire4ever1190 ([Jake Leahy](https://github.com/ire4ever1190)):
  * Feat: Only show .torrent files in the web UI. ([#6320](https://github.com/transmission/transmission/pull/6320))
* @killemov:
  * Code review. ([#5082](https://github.com/transmission/transmission/pull/5082), [#7014](https://github.com/transmission/transmission/pull/7014), [#7037](https://github.com/transmission/transmission/pull/7037), [#7285](https://github.com/transmission/transmission/pull/7285))
* @klevain:
  * Added high contrast theme. ([#5470](https://github.com/transmission/transmission/pull/5470))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#6224](https://github.com/transmission/transmission/pull/6224), [#6334](https://github.com/transmission/transmission/pull/6334), [#6739](https://github.com/transmission/transmission/pull/6739), [#7214](https://github.com/transmission/transmission/pull/7214))
* @niol:
  * The inspector can now be hidden by clicking. ([#6863](https://github.com/transmission/transmission/pull/6863))
* @regdos ([Tomasz Regdos](https://github.com/regdos)):
  * Code review. ([#7037](https://github.com/transmission/transmission/pull/7037))
* @rsekman ([Robin Seth Ekman](https://github.com/rsekman)):
  * Build: update web/CMakeLists.txt to reflect renamed files (Fixes #7240). ([#7241](https://github.com/transmission/transmission/pull/7241))
* @Rukario:
  * Code review. ([#5340](https://github.com/transmission/transmission/pull/5340), [#7001](https://github.com/transmission/transmission/pull/7001), [#7036](https://github.com/transmission/transmission/pull/7036), [#7037](https://github.com/transmission/transmission/pull/7037))
  * Made the main menu accessible even on smaller displays. ([#5827](https://github.com/transmission/transmission/pull/5827))
  * WebUI: Improved filterbar for narrowed viewports. ([#5828](https://github.com/transmission/transmission/pull/5828))
  * Unified CSS shadow properties. ([#5840](https://github.com/transmission/transmission/pull/5840))
  * Removed a rounding method to enable using decimal for granular progress bar growth. ([#5857](https://github.com/transmission/transmission/pull/5857))
  * WebUI: Fix graying out inspector. ([#5893](https://github.com/transmission/transmission/pull/5893))
  * Improved overflow menu for web client. ([#5895](https://github.com/transmission/transmission/pull/5895))
  * Replaced background colors with system color keywords to enable using browser's colors. CSS style adjustments esp. for label and buttons. ([#5897](https://github.com/transmission/transmission/pull/5897))
  * Added a new keyboard shortcut for web client listening to K to edit torrent labels. ([#5911](https://github.com/transmission/transmission/pull/5911))
  * Added display and time in torrent detail. ([#5918](https://github.com/transmission/transmission/pull/5918))
  * A quick fix to buttons not graying out in dark mode. ([#5921](https://github.com/transmission/transmission/pull/5921))
  * Added touchscreen support in the context menu. ([#5928](https://github.com/transmission/transmission/pull/5928))
  * Added percent digits into the progress bar. ([#5937](https://github.com/transmission/transmission/pull/5937))
  * Improved WebUI responsiveness and made quality of life improvements. ([#5947](https://github.com/transmission/transmission/pull/5947))
  * Fixed updating magnet link after selecting same torrent again. ([#6028](https://github.com/transmission/transmission/pull/6028))
  * Fixed truncated hash in inspector page, added name section to inspector page. ([#7014](https://github.com/transmission/transmission/pull/7014))
  * Fix: correct percent calculation for progressbar. ([#7245](https://github.com/transmission/transmission/pull/7245))
  * Updated gray color for grayed out objects. ([#7248](https://github.com/transmission/transmission/pull/7248))
  * Updated displaying number in new gigabyte per second unit. ([#7279](https://github.com/transmission/transmission/pull/7279))
  * Refactor: softcode/consolidate strings in `torrent-row.js`, normal appendChild order for compact mode. ([#7285](https://github.com/transmission/transmission/pull/7285))
* @S-Aarab ([Safouane Aarab](https://github.com/S-Aarab)):
  * Improved some UI styling and spacing. ([#5466](https://github.com/transmission/transmission/pull/5466))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#5857](https://github.com/transmission/transmission/pull/5857), [#5918](https://github.com/transmission/transmission/pull/5918), [#5937](https://github.com/transmission/transmission/pull/5937), [#6028](https://github.com/transmission/transmission/pull/6028), [#6320](https://github.com/transmission/transmission/pull/6320), [#6649](https://github.com/transmission/transmission/pull/6649), [#6664](https://github.com/transmission/transmission/pull/6664), [#7014](https://github.com/transmission/transmission/pull/7014), [#7241](https://github.com/transmission/transmission/pull/7241), [#7279](https://github.com/transmission/transmission/pull/7279))
  * The WebUI now does separate port checks for IPv4 and IPv6. ([#5953](https://github.com/transmission/transmission/pull/5953), [#6607](https://github.com/transmission/transmission/pull/6607))
  * Added forced variant of the "Verify Local Data" context menu item to WebUI. ([#5981](https://github.com/transmission/transmission/pull/5981), [#6088](https://github.com/transmission/transmission/pull/6088))
  * Removed excessive `session-set` RPC calls related to WebUI preference dialogue. ([#5994](https://github.com/transmission/transmission/pull/5994))
  * Feat: WebUI torrent tracker list style improvements. ([#6008](https://github.com/transmission/transmission/pull/6008))
  * Feat: WebUI display full peer address in tooltip. ([#6081](https://github.com/transmission/transmission/pull/6081))
  * Feat: display full client name in tooltip [WebUI]. ([#6224](https://github.com/transmission/transmission/pull/6224))
  * Fixed `4.0.0` bug where the WebUI "Set Location" dialogue does not auto fill the selected torrent's current download location. ([#6334](https://github.com/transmission/transmission/pull/6334))
  * Fixed `4.0.5` bug where svg and png icons in the WebUI might not be displayed. ([#6409](https://github.com/transmission/transmission/pull/6409), [#6430](https://github.com/transmission/transmission/pull/6430))
  * Webui: make torrent row colours more readable. ([#6462](https://github.com/transmission/transmission/pull/6462))
  * Chore: webui scss cleanup. ([#6471](https://github.com/transmission/transmission/pull/6471))
  * Fixup! chore: webui scss cleanup (#6471). ([#6490](https://github.com/transmission/transmission/pull/6490))
  * Fixed a `4.0.0` bug where the infinite ratio symbol was displayed incorrectly in the WebUI. ([#6491](https://github.com/transmission/transmission/pull/6491))
  * Revert "feat: Only show .torrent files in the web UI (#6320)". ([#6538](https://github.com/transmission/transmission/pull/6538))
  * Webui: fixed width for speed info. ([#6739](https://github.com/transmission/transmission/pull/6739))
  * Refactor: parse cookie pref values as their default values types. ([#7001](https://github.com/transmission/transmission/pull/7001))
  * Chore: misc WebUI fixes and cleanup. ([#7037](https://github.com/transmission/transmission/pull/7037))
  * Ci: automatically regenerate `package.json.buildonly`. ([#7198](https://github.com/transmission/transmission/pull/7198))
  * Chore: convert `transmission-web` to ES module. ([#7209](https://github.com/transmission/transmission/pull/7209))
  * Fix: support nodejs below v20.11 for `generate_buildonly.js`. ([#7214](https://github.com/transmission/transmission/pull/7214))
* @tiagoboldt ([Tiago Boldt Sousa](https://github.com/tiagoboldt)):
  * Code review. ([#5082](https://github.com/transmission/transmission/pull/5082))

### Contributions to Daemon:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#6728](https://github.com/transmission/transmission/pull/6728))
* @niol:
  * Added documentation key to systemd service file. ([#6781](https://github.com/transmission/transmission/pull/6781))
* @rafe-s ([Rafe S.](https://github.com/rafe-s)):
  * Updated transmission-daemon.1 to sync with --help. ([#6059](https://github.com/transmission/transmission/pull/6059))
* @Schlossgeist ([Nick](https://github.com/Schlossgeist)):
  * Code review. ([#6619](https://github.com/transmission/transmission/pull/6619))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#6198](https://github.com/transmission/transmission/pull/6198), [#6619](https://github.com/transmission/transmission/pull/6619), [#6728](https://github.com/transmission/transmission/pull/6728))
  * Refactor: replace some C idioms with C++ ones. ([#5656](https://github.com/transmission/transmission/pull/5656))
  * Fixed minor memory leak. ([#5695](https://github.com/transmission/transmission/pull/5695))
  * Chore: iwyu. ([#6201](https://github.com/transmission/transmission/pull/6201))
  * Updated documentation. ([#6499](https://github.com/transmission/transmission/pull/6499))
  * Fixup! fix: include daemon-specific options in app defaults (#6499). ([#6505](https://github.com/transmission/transmission/pull/6505))
  * More accurate timestamps for daemon logs. ([#7009](https://github.com/transmission/transmission/pull/7009))
  * Fix: check `tr_num_parse` result in daemon. ([#7181](https://github.com/transmission/transmission/pull/7181))
  * Fix: add missing `settings.json` docs and app defaults. ([#7218](https://github.com/transmission/transmission/pull/7218))

### Contributions to transmission-cli:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#6488](https://github.com/transmission/transmission/pull/6488))
  * Fix: app defaults should override libtransmission defaults. ([#6495](https://github.com/transmission/transmission/pull/6495))
* @tobbez ([Torbjörn Lönnemark](https://github.com/tobbez)):
  * Fix: restore tr_optind in all getConfigDir branches. ([#6920](https://github.com/transmission/transmission/pull/6920))

### Contributions to transmission-remote:

* @killemov:
  * Code review. ([#5584](https://github.com/transmission/transmission/pull/5584))
* @lajp ([Luukas Pörtfors](https://github.com/lajp)):
  * Remote: Implement idle seeding limits. ([#2947](https://github.com/transmission/transmission/pull/2947))
* @lvd2:
  * Added 'months' and 'years' to ETA display for extremely slow torrents. ([#5584](https://github.com/transmission/transmission/pull/5584))
  * Transmission-remote: for '-l', implement default sorting by addedDate. ([#5608](https://github.com/transmission/transmission/pull/5608))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#6798](https://github.com/transmission/transmission/pull/6798))
* @rsekman ([Robin Seth Ekman](https://github.com/rsekman)):
  * Fixed display bug that failed to show some torrent labels. ([#5572](https://github.com/transmission/transmission/pull/5572))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#5608](https://github.com/transmission/transmission/pull/5608), [#6819](https://github.com/transmission/transmission/pull/6819))
  * Ui: unify percent done format in `transmission-remote`. ([#6447](https://github.com/transmission/transmission/pull/6447))
  * Added optional sequential downloading. ([#6454](https://github.com/transmission/transmission/pull/6454))
  * Refactor: new `tr_variant` API in `transmission-remote` and other cleanup. ([#6798](https://github.com/transmission/transmission/pull/6798))
  * Fix: torrent details speed info unit. ([#6845](https://github.com/transmission/transmission/pull/6845))
* @vchimishuk ([Viacheslav Chimishuk](https://github.com/vchimishuk)):
  * Code review. ([#2947](https://github.com/transmission/transmission/pull/2947))

### Contributions to Everything Else:

* @barracuda156 ([Sergey Fedorov](https://github.com/barracuda156)):
  * Code review. ([#5632](https://github.com/transmission/transmission/pull/5632))
* @cmo-pomerium:
  * Added open source reverse proxy guide on securing transmission. ([#5867](https://github.com/transmission/transmission/pull/5867))
* @dechamps ([Etienne Dechamps](https://github.com/dechamps)):
  * Updated documentation. ([#7110](https://github.com/transmission/transmission/pull/7110), [#7120](https://github.com/transmission/transmission/pull/7120))
* @fetzu ([Julien](https://github.com/fetzu)):
  * Chore: removed copyright timespans in headers. ([#4850](https://github.com/transmission/transmission/pull/4850))
  * Chore: automated copyright update with GitHub Actions. ([#6195](https://github.com/transmission/transmission/pull/6195))
  * Updated documentation. ([#6196](https://github.com/transmission/transmission/pull/6196), [#6199](https://github.com/transmission/transmission/pull/6199))
* @fxcoudert ([FX Coudert](https://github.com/fxcoudert)):
  * Updated workflow to Qt 6 on macOS. ([#6206](https://github.com/transmission/transmission/pull/6206))
* @G-Ray ([Geoffrey Bonneville](https://github.com/G-Ray)):
  * Improved support for building with the NDK on Android. ([#6024](https://github.com/transmission/transmission/pull/6024))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Code review. ([#6102](https://github.com/transmission/transmission/pull/6102), [#6391](https://github.com/transmission/transmission/pull/6391))
  * Typos in libtransmission tests source code comments. ([#5468](https://github.com/transmission/transmission/pull/5468))
  * Fixed image regression in docs. ([#5624](https://github.com/transmission/transmission/pull/5624))
  * Docs: remove dead link from Scripts.md. ([#6283](https://github.com/transmission/transmission/pull/6283))
  * Updated documentation. ([#6696](https://github.com/transmission/transmission/pull/6696))
* @ile6695 ([Ilkka Kallioniemi](https://github.com/ile6695)):
  * Code review. ([#7252](https://github.com/transmission/transmission/pull/7252))
  * Docs: Add Debian 12 instructions. ([#5866](https://github.com/transmission/transmission/pull/5866))
* @jakiki6 ([Laura Kirsch](https://github.com/jakiki6)):
  * Libtransmission: fix copyright header generation. ([#6874](https://github.com/transmission/transmission/pull/6874))
* @jemadux ([Klearchos-Angelos Gkountras](https://github.com/jemadux)):
  * Updating debian release. ([#6030](https://github.com/transmission/transmission/pull/6030))
* @jggimi ([Josh Grosse](https://github.com/jggimi)):
  * Updated documentation. ([#7282](https://github.com/transmission/transmission/pull/7282))
* @killemov:
  * Code review. ([#6828](https://github.com/transmission/transmission/pull/6828), [#7252](https://github.com/transmission/transmission/pull/7252))
* @LaserEyess:
  * Code review. ([#3823](https://github.com/transmission/transmission/pull/3823))
  * Only format JS if files in web/ change. ([#5525](https://github.com/transmission/transmission/pull/5525))
  * Hardened systemd service settings. ([#6391](https://github.com/transmission/transmission/pull/6391))
  * Added user presets to .gitignore. ([#6407](https://github.com/transmission/transmission/pull/6407))
* @lighterowl ([Daniel Kamil Kozar](https://github.com/lighterowl)):
  * Expose files' begin and end pieces via RPC. ([#5578](https://github.com/transmission/transmission/pull/5578))
* @luk1337:
  * Fix: Use non-lib64 systemd system unit dir path. ([#7115](https://github.com/transmission/transmission/pull/7115))
* @luzpaz:
  * Docs: fix comment typos. ([#5980](https://github.com/transmission/transmission/pull/5980))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#3823](https://github.com/transmission/transmission/pull/3823), [#4931](https://github.com/transmission/transmission/pull/4931), [#5521](https://github.com/transmission/transmission/pull/5521), [#5632](https://github.com/transmission/transmission/pull/5632), [#6187](https://github.com/transmission/transmission/pull/6187), [#6195](https://github.com/transmission/transmission/pull/6195), [#6407](https://github.com/transmission/transmission/pull/6407), [#6453](https://github.com/transmission/transmission/pull/6453), [#6459](https://github.com/transmission/transmission/pull/6459), [#6923](https://github.com/transmission/transmission/pull/6923), [#7169](https://github.com/transmission/transmission/pull/7169), [#7252](https://github.com/transmission/transmission/pull/7252), [#7254](https://github.com/transmission/transmission/pull/7254), [#7268](https://github.com/transmission/transmission/pull/7268), [#7273](https://github.com/transmission/transmission/pull/7273))
  * Fix: CI: Do not use nproc on macOS hosts. ([#5833](https://github.com/transmission/transmission/pull/5833))
  * Chore: fix warnings in CodeQL workflow. ([#6106](https://github.com/transmission/transmission/pull/6106))
  * Chore: Add compile_commands.json to .gitignore. ([#6219](https://github.com/transmission/transmission/pull/6219))
  * Chore: fix warnings when compiling macOS client with either Xcode or CMake. ([#6676](https://github.com/transmission/transmission/pull/6676))
  * Fixed incorrect value for SortIncludes in .clang-format. ([#6784](https://github.com/transmission/transmission/pull/6784))
  * Fixed code style script path in CONTRIBUTING.md. ([#6787](https://github.com/transmission/transmission/pull/6787))
  * Chore: update older macos build verification workflow to macos-12. ([#6883](https://github.com/transmission/transmission/pull/6883))
* @NickWick13 ([Nick](https://github.com/NickWick13)):
  * Docs: update translation site names. ([#5481](https://github.com/transmission/transmission/pull/5481))
* @niol:
  * Build with -latomic on platforms that need it. ([#6774](https://github.com/transmission/transmission/pull/6774))
  * Updated documentation. ([#6800](https://github.com/transmission/transmission/pull/6800))
* @orangepizza:
  * Fixed building with mbedtls 3.X. ([#6822](https://github.com/transmission/transmission/pull/6822))
* @PHLAK ([Chris Kankiewicz](https://github.com/PHLAK)):
  * Docs: update documentation of logging levels (#5700). ([#5700](https://github.com/transmission/transmission/pull/5700))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Updated documentation. ([#6946](https://github.com/transmission/transmission/pull/6946))
* @rodiontsev ([Dmitry Rodiontsev](https://github.com/rodiontsev)):
  * Updated documentation. ([#7040](https://github.com/transmission/transmission/pull/7040))
* @rsekman ([Robin Seth Ekman](https://github.com/rsekman)):
  * Build: enable clang-tidy by default. ([#6989](https://github.com/transmission/transmission/pull/6989))
  * Fix: silence bugprone-unchecked-optional-access warnings. ([#6990](https://github.com/transmission/transmission/pull/6990))
* @sfan5:
  * Sanitize torrent filenames depending on current OS. ([#3823](https://github.com/transmission/transmission/pull/3823))
* @si14 ([Dan Groshev](https://github.com/si14)):
  * Updated documentation. ([#6083](https://github.com/transmission/transmission/pull/6083))
* @stefanos82 ([stefanos](https://github.com/stefanos82)):
  * Updated documentation. ([#5688](https://github.com/transmission/transmission/pull/5688))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#3823](https://github.com/transmission/transmission/pull/3823), [#5688](https://github.com/transmission/transmission/pull/5688), [#6186](https://github.com/transmission/transmission/pull/6186), [#6187](https://github.com/transmission/transmission/pull/6187), [#6340](https://github.com/transmission/transmission/pull/6340), [#6459](https://github.com/transmission/transmission/pull/6459), [#6631](https://github.com/transmission/transmission/pull/6631), [#6703](https://github.com/transmission/transmission/pull/6703), [#6726](https://github.com/transmission/transmission/pull/6726), [#6743](https://github.com/transmission/transmission/pull/6743), [#6946](https://github.com/transmission/transmission/pull/6946), [#6989](https://github.com/transmission/transmission/pull/6989), [#6990](https://github.com/transmission/transmission/pull/6990), [#6997](https://github.com/transmission/transmission/pull/6997), [#7040](https://github.com/transmission/transmission/pull/7040), [#7275](https://github.com/transmission/transmission/pull/7275), [#7282](https://github.com/transmission/transmission/pull/7282))
  * Handle IPv6 NAT during LTEP handshake. ([#5565](https://github.com/transmission/transmission/pull/5565))
  * Updated the torrent creator's default piece size to handle very large torrents better. ([#5615](https://github.com/transmission/transmission/pull/5615))
  * Improved libtransmission code to use less CPU. ([#5651](https://github.com/transmission/transmission/pull/5651))
  * Fix: conform to libcurl requirements to avoid memory leak. ([#5702](https://github.com/transmission/transmission/pull/5702))
  * Fix: revert "perf: improve IPv4 `tr_address` comparison". ([#5709](https://github.com/transmission/transmission/pull/5709))
  * Updated documentation. ([#5784](https://github.com/transmission/transmission/pull/5784), [#5787](https://github.com/transmission/transmission/pull/5787), [#5793](https://github.com/transmission/transmission/pull/5793), [#5819](https://github.com/transmission/transmission/pull/5819), [#5863](https://github.com/transmission/transmission/pull/5863), [#6102](https://github.com/transmission/transmission/pull/6102), [#6302](https://github.com/transmission/transmission/pull/6302), [#6448](https://github.com/transmission/transmission/pull/6448), [#6677](https://github.com/transmission/transmission/pull/6677), [#6919](https://github.com/transmission/transmission/pull/6919))
  * Fix: fix ci web cmake option. ([#5835](https://github.com/transmission/transmission/pull/5835))
  * Added myself to AUTHORS. ([#5859](https://github.com/transmission/transmission/pull/5859))
  * Feat: update `TR_VCS_REVISION` when git HEAD changes. ([#6100](https://github.com/transmission/transmission/pull/6100))
  * Fix: don't create all 0-byte files in `MakemetaTest::makeRandomFiles()`. ([#6394](https://github.com/transmission/transmission/pull/6394))
  * Fixup! fix: implement proper download limit for uTP (#6416). ([#6481](https://github.com/transmission/transmission/pull/6481))
  * Ci: bump CI actions. ([#6666](https://github.com/transmission/transmission/pull/6666))
  * Ci: trigger CI when changing CI definitions. ([#6688](https://github.com/transmission/transmission/pull/6688))
  * Fix: mismatched `class` forward declaration for `struct tr_peer`. ([#6725](https://github.com/transmission/transmission/pull/6725))
  * Ci: bump `clang-tidy` from 14 to 18. ([#6923](https://github.com/transmission/transmission/pull/6923))
  * Chore: misc formatting updates and fixes. ([#7049](https://github.com/transmission/transmission/pull/7049))
  * Build: fail if clang-tidy is not found when `-DRUN_CLANG_TIDY=ON`. ([#7210](https://github.com/transmission/transmission/pull/7210))
  * Build: accept false constant other than `OFF` for `REBUILD_WEB`. ([#7219](https://github.com/transmission/transmission/pull/7219))
  * Ci: test `REBUILD_WEB=ON`. ([#7242](https://github.com/transmission/transmission/pull/7242))
  * Fix: CI errors related to `REBUILD_WEB`. ([#7252](https://github.com/transmission/transmission/pull/7252))
  * Test: use new `tr_variant` API. ([#7268](https://github.com/transmission/transmission/pull/7268))
  * Ci: remove macos pkgconf workaround. ([#7273](https://github.com/transmission/transmission/pull/7273))
* @TheKhanj ([Pooyan Khanjankhani](https://github.com/TheKhanj)):
  * Updated documentation. ([#6790](https://github.com/transmission/transmission/pull/6790))
* @wjt ([Will Thompson](https://github.com/wjt)):
  * GTK: Add 4.0.4 and 4.0.5 releases to metainfo. ([#6378](https://github.com/transmission/transmission/pull/6378))
* @xm21x ([Christina](https://github.com/xm21x)):
  * Updated documentation. ([#6367](https://github.com/transmission/transmission/pull/6367))

