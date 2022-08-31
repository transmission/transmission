## [Transmission 3.00](https://github.com/transmission/transmission/releases/tag/3.00) (2020-05-03)

### All Platforms
- Allow the RPC server to listen on an IPv6 address ([#161](https://github.com/transmission/transmission/pull/161))
- Change `TR_CURL_SSL_VERIFY` to `TR_CURL_SSL_NO_VERIFY` and enable verification by default ([#334](https://github.com/transmission/transmission/pull/334))
- Go back to using hash as base name for resume and torrent files (those stored in configuration directory) ([#122](https://github.com/transmission/transmission/pull/122))
- Handle "fields" argument in "session-get" RPC request; if "fields" array is present in arguments, only return session fields specified; otherwise return all the fields as before
- Limit the number of incorrect authentication attempts in embedded web server to 100 to prevent brute-force attacks ([#371](https://github.com/transmission/transmission/pull/371))
- Set idle seed limit range to 1..40320 (4 weeks tops) in all clients ([#212](https://github.com/transmission/transmission/pull/212))
- Add Peer ID for Xfplay, PicoTorrent, Free Download Manager, Folx, Baidu Netdisk torrent clients ([#256](https://github.com/transmission/transmission/pull/256), [#285](https://github.com/transmission/transmission/pull/285), [#355](https://github.com/transmission/transmission/pull/355), [#363](https://github.com/transmission/transmission/pull/363), [#386](https://github.com/transmission/transmission/pull/386))
- Announce `INT64_MAX` as size left if the value is unknown (helps with e.g. Amazon S3 trackers) ([#250](https://github.com/transmission/transmission/pull/250))
- Add `TCP_FASTOPEN` support (should result in slight speedup) ([#184](https://github.com/transmission/transmission/pull/184))
- Improve ToS handling on IPv6 connections ([#128](https://github.com/transmission/transmission/pull/128), [#341](https://github.com/transmission/transmission/pull/341), [#360](https://github.com/transmission/transmission/pull/360), [#692](https://github.com/transmission/transmission/pull/692), [#737](https://github.com/transmission/transmission/pull/737))
- Abort handshake if establishing DH shared secret fails (leads to crash) ([#27](https://github.com/transmission/transmission/pull/27))
- Don't switch trackers while announcing (leads to crash) ([#297](https://github.com/transmission/transmission/pull/297))
- Improve completion scripts execution and error handling; add support for .cmd and .bat files on Windows ([#405](https://github.com/transmission/transmission/pull/405))
- Maintain a "session ID" file (in temporary directory) to better detect whether session is local or remote; return the ID as part of "session-get" response (TRAC-5348, [#861](https://github.com/transmission/transmission/pull/861))
- Change torrent location even if no data move is needed ([#35](https://github.com/transmission/transmission/pull/35))
- Support CIDR-notated blocklists ([#230](https://github.com/transmission/transmission/pull/230), [#741](https://github.com/transmission/transmission/pull/741))
- Update the resume file before running scripts ([#825](https://github.com/transmission/transmission/pull/825))
- Make multiscrape limits adaptive ([#837](https://github.com/transmission/transmission/pull/837))
- Add labels support to libtransmission and transmission-remote ([#822](https://github.com/transmission/transmission/pull/822))
- Parse `session-id` header case-insensitively ([#765](https://github.com/transmission/transmission/pull/765))
- Sanitize suspicious path components instead of rejecting them ([#62](https://github.com/transmission/transmission/pull/62), [#294](https://github.com/transmission/transmission/pull/294))
- Load CA certs from system store on Windows / OpenSSL ([#446](https://github.com/transmission/transmission/pull/446))
- Add support for mbedtls (formerly polarssl) and wolfssl (formerly cyassl), LibreSSL ([#115](https://github.com/transmission/transmission/pull/115), [#116](https://github.com/transmission/transmission/pull/116), [#284](https://github.com/transmission/transmission/pull/284), [#486](https://github.com/transmission/transmission/pull/486), [#524](https://github.com/transmission/transmission/pull/524), [#570](https://github.com/transmission/transmission/pull/570))
- Fix building against OpenSSL 1.1.0+ ([#24](https://github.com/transmission/transmission/pull/24))
- Fix quota support for uClibc-ng 1.0.18+ and DragonFly BSD ([#42](https://github.com/transmission/transmission/pull/42), [#58](https://github.com/transmission/transmission/pull/58), [#312](https://github.com/transmission/transmission/pull/312))
- Fix a number of memory leaks (magnet loading, session shutdown, bencoded data parsing) ([#56](https://github.com/transmission/transmission/pull/56))
- Bump miniupnpc version to 2.0.20170509 ([#347](https://github.com/transmission/transmission/pull/347))
- CMake-related improvements (Ninja generator, libappindicator, systemd, Solaris and macOS) ([#72](https://github.com/transmission/transmission/pull/72), [#96](https://github.com/transmission/transmission/pull/96), [#117](https://github.com/transmission/transmission/pull/117), [#118](https://github.com/transmission/transmission/pull/118), [#133](https://github.com/transmission/transmission/pull/133), [#191](https://github.com/transmission/transmission/pull/191))
- Switch to submodules to manage (most of) third-party dependencies
- Fail installation on Windows if UCRT is not installed

### Mac Client
- Bump minimum macOS version to 10.10
- Dark Mode support ([#644](https://github.com/transmission/transmission/pull/644), [#722](https://github.com/transmission/transmission/pull/722), [#757](https://github.com/transmission/transmission/pull/757), [#779](https://github.com/transmission/transmission/pull/779), [#788](https://github.com/transmission/transmission/pull/788))
- Remove Growl support, notification center is always used ([#387](https://github.com/transmission/transmission/pull/387))
- Fix autoupdate on High Sierra and up by bumping the Sparkle version ([#121](https://github.com/transmission/transmission/pull/121), [#600](https://github.com/transmission/transmission/pull/600))
- Transition to ARC ([#336](https://github.com/transmission/transmission/pull/336))
- Use proper UTF-8 encoding (with macOS-specific normalization) when setting download/incomplete directory and completion script paths ([#11](https://github.com/transmission/transmission/pull/11))
- Fix uncaught exception when dragging multiple items between groups ([#51](https://github.com/transmission/transmission/pull/51))
- Add flat variants of status icons for message log ([#134](https://github.com/transmission/transmission/pull/134))
- Optimize image resources size ([#304](https://github.com/transmission/transmission/pull/304), [#429](https://github.com/transmission/transmission/pull/429))
- Update file icon when file name changes ([#37](https://github.com/transmission/transmission/pull/37))
- Update translations

### GTK+ Client
- Add queue up/down hotkeys ([#158](https://github.com/transmission/transmission/pull/158))
- Modernize the .desktop file ([#162](https://github.com/transmission/transmission/pull/162))
- Add AppData file ([#224](https://github.com/transmission/transmission/pull/224))
- Add symbolic icon variant for the Gnome top bar and when the high contrast theme is in use ([#414](https://github.com/transmission/transmission/pull/414), [#449](https://github.com/transmission/transmission/pull/449))
- Update file icon when its name changes ([#37](https://github.com/transmission/transmission/pull/37))
- Switch from intltool to gettext for translations ([#584](https://github.com/transmission/transmission/pull/584), [#647](https://github.com/transmission/transmission/pull/647))
- Update translations, add new translations for Portuguese (Portugal)

### Qt Client
- Bump minimum Qt version to 5.2
- Fix dropping .torrent files into main window on Windows ([#269](https://github.com/transmission/transmission/pull/269))
- Fix prepending of drive letter to various user-selected paths on Windows ([#236](https://github.com/transmission/transmission/pull/236), [#307](https://github.com/transmission/transmission/pull/307), [#404](https://github.com/transmission/transmission/pull/404), [#437](https://github.com/transmission/transmission/pull/437), [#699](https://github.com/transmission/transmission/pull/699), [#723](https://github.com/transmission/transmission/pull/723), [#877](https://github.com/transmission/transmission/pull/877))
- Fix sorting by progress in presence of magnet transfers ([#234](https://github.com/transmission/transmission/pull/234))
- Fix .torrent file trashing upon addition ([#262](https://github.com/transmission/transmission/pull/262))
- Add queue up/down hotkeys ([#158](https://github.com/transmission/transmission/pull/158))
- Reduce torrent properties (file tree) memory usage
- Display tooltips in torrent properties (file tree) in case the names don't fit ([#411](https://github.com/transmission/transmission/pull/411))
- Improve UI look on hi-dpi displays (YMMV)
- Use session ID (if available) to check if session is local or not ([#861](https://github.com/transmission/transmission/pull/861))
- Use default (instead of system) locale to be more flexible ([#130](https://github.com/transmission/transmission/pull/130))
- Modernize the .desktop file ([#162](https://github.com/transmission/transmission/pull/162))
- Update translations, add new translations for Afrikaans, Catalan, Danish, Greek, Norwegian Bokmål, Slovenian

### Daemon
- Use libsystemd instead of libsystemd-daemon (TRAC-5921)
- Harden transmission-daemon.service by disallowing privileges elevation ([#795](https://github.com/transmission/transmission/pull/795))
- Fix exit code to be zero when dumping settings ([#487](https://github.com/transmission/transmission/pull/487))

### Web Client
- Fix tracker error XSS in inspector (CVE-?)
- Fix performance issues due to improper use of `setInterval()` for UI refresh (TRAC-6031)
- Fix recognition of `https://` links in comments field ([#41](https://github.com/transmission/transmission/pull/41), [#180](https://github.com/transmission/transmission/pull/180))
- Fix torrent list style in Google Chrome 59+ ([#384](https://github.com/transmission/transmission/pull/384))
- Show ETA in compact view on non-mobile devices ([#146](https://github.com/transmission/transmission/pull/146))
- Show upload file button on mobile devices ([#320](https://github.com/transmission/transmission/pull/320), [#431](https://github.com/transmission/transmission/pull/431), [#956](https://github.com/transmission/transmission/pull/956))
- Add keyboard hotkeys for web interface ([#351](https://github.com/transmission/transmission/pull/351))
- Disable autocompletion in torrent URL field ([#367](https://github.com/transmission/transmission/pull/367))

### Utils
- Prevent crash in transmission-show displaying torrents with invalid creation date ([#609](https://github.com/transmission/transmission/pull/609))
- Handle IPv6 RPC addresses in transmission-remote ([#247](https://github.com/transmission/transmission/pull/247))
- Add `--unsorted` option to transmission-show ([#767](https://github.com/transmission/transmission/pull/767))
- Widen the torrent-id column in transmission-remote for cleaner formatting ([#840](https://github.com/transmission/transmission/pull/840))
