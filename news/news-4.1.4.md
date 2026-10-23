# Transmission 4.1.4

This is Transmission 4.1.4, a bugfix release. It fixes a security vulnerability in the macOS client and has other small bugfixes. Users are encouraged to upgrade to this version.

## What's New in 4.1.4

### All Platforms

* Fixed a potential RCE vulnerability in the macOS client. It requires local write access to the user's preferences, so it is not remotely exploitable. ([GHSA-v7mp-fpv3-jf5r](https://github.com/transmission/transmission/security/advisories/GHSA-v7mp-fpv3-jf5r))
* Fixed a Transmission 4.1.0 regression that could cause torrents with zero-byte files to be handled incorrectly, sometimes losing downloaded progress ([#9165](https://github.com/transmission/transmission/pull/9165))
* Hardened the `torrent-add` RPC method against malformed requests. ([#9158](https://github.com/transmission/transmission/pull/9158))
* Unified launch and delegation between the Qt and GTK clients: If either is already running and then you or a browser's mime-type handler tries to start the other to add a new torrent, that new torrent will get handed off to the pre-existing session. ([#9032](https://github.com/transmission/transmission/pull/9032))
* Fixed a build failure that happened when building Transmission with `-Wnull-dereference -Werror` in gcc 12.15. ([#9057](https://github.com/transmission/transmission/pull/9057))
* Fixed a rare bug could leave Transmission hung but running on shutdown. ([#9058](https://github.com/transmission/transmission/pull/9058))
* Fixed a rare bug that could prevent downloading webseeds from finishing. ([#9146](https://github.com/transmission/transmission/pull/9146))
* Fixed a bug where setting `TR_CURL_PROXY_SSL_NO_VERIFY` broke origin server SSL verification. ([#9153](https://github.com/transmission/transmission/pull/9153))
* Fixed per-torrent session speed-limit overrides so they apply to both downloads and uploads. ([#9154](https://github.com/transmission/transmission/pull/9154))

### macOS Client

* Fixed a bug that prevented some magnet links from being added correctly. ([#9062](https://github.com/transmission/transmission/pull/9062))
* Migrate Sparkle update signing from DSA to EdDSA. ([#9065](https://github.com/transmission/transmission/pull/9065))

### Qt Client

* On Windows builds, change to zlib `1.3.2` from `1.3.1` to pick up [7ASecurity audit](https://7asecurity.com/blog/2026/02/zlib-7asecurity-audit/) fixes. ([#9025](https://github.com/transmission/transmission/pull/9025))
* On Windows 10+ builds, change to Qt `6.10.3` from `6.10.1` to pick up Qt bugfixes. ([#9030](https://github.com/transmission/transmission/pull/9030))
* On Windows 7 builds, change to Qt `5.15.19` from `5.15.17` to pick up Qt bugfixes. ([#9030](https://github.com/transmission/transmission/pull/9030))
* Improved UI code to use less CPU. ([#9161](https://github.com/transmission/transmission/pull/9161))

### GTK Client

* The torrent list now re-sorts and re-applies its filter when the torrent's top folder's name changes, eg after a rename or after a magnet link's metadata arrives. ([#9159](https://github.com/transmission/transmission/pull/9159))
* Opening a torrent's folder now opens the correct folder when a single-file torrent uses a directory structure. ([#9160](https://github.com/transmission/transmission/pull/9160))
* Fixed a bug that prevented `transmission-gtk`'s app icon from being found on some desktops. ([#9032](https://github.com/transmission/transmission/pull/9032))

### Web Client

* Fixed a display bug that sometimes listed torrent  names as 'Unknown'. ([#9135](https://github.com/transmission/transmission/pull/9135))

### transmission-remote

* Fixed a 4.0.0 bug that broke `transmission-remote --list-groups`. ([#9157](https://github.com/transmission/transmission/pull/9157))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

* @danudey ([Daniel Fox](https://github.com/danudey)): Code review. ([#9032](https://github.com/transmission/transmission/pull/9032))
* @jgalat ([Jorge Galat](https://github.com/jgalat)): Fixed per-torrent session speed-limit overrides so they apply to both downloads and uploads. ([#9154](https://github.com/transmission/transmission/pull/9154))
* @h1de0x ([Kirill Shtumf](https://github.com/h1de0x)): Opening a torrent's folder now opens the correct folder when a single-file torrent uses a directory structure. ([#9160](https://github.com/transmission/transmission/pull/9160))

