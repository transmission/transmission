# Transmission 4.1.1

This is Transmission 4.1.1, a bugfix release.
It fixes 20+ bugs and has a few performance improvements too.
All users are encouraged to upgrade to this version.

This progress was possible because of good bug reports and
performance logs reported by users. Thanks, and keep them coming!


## What's New in 4.1.1

### All Platforms

* Fixed a `4.1.0` bug that failed to report some filesystem errors to RPC clients who were querying the system's free space available. ([#8258](https://github.com/transmission/transmission/pull/8258))
* Fixed a `4.1.0` bug that kept a a torrent's updated queue position from being shown. ([#8298](https://github.com/transmission/transmission/pull/8298))
* Fixed a `4.1.0` bug that caused torrents' queuing order to sometimes be lost between sessions. ([#8306](https://github.com/transmission/transmission/pull/8306))
* Fixed "assertion failed: no timezone" error on OpenSolaris. ([#8358](https://github.com/transmission/transmission/pull/8358))
* Fixed a `4.0.0` bug that displayed the wrong mime-type icon for mp4 video files. ([#8411](https://github.com/transmission/transmission/pull/8411))
* Hardened .torrent parsing by exiting sooner if  `pieces` has an invalid size. ([#8412](https://github.com/transmission/transmission/pull/8412))
* Reverted a `4.1.0` RPC change that broke some 3rd party code by returning floating-point numbers, rather than integers, for speed limit fields. ([#8416](https://github.com/transmission/transmission/pull/8416))
* Fixed crash that could happen if a user paused a torrent and edited its tracker list at the same time. ([#8478](https://github.com/transmission/transmission/pull/8478))
* Fixed `4.1.0` crash on arm32 by switching crc32 libraries to Mark Madler's [crcany](https://github.com/madler/crcany). ([#8529](https://github.com/transmission/transmission/pull/8529))
* Require UTF-8 filenames in .torrent files, as required by the [BitTorrent spec](https://www.bittorrent.org/beps/bep_0003.html). ([#8541](https://github.com/transmission/transmission/pull/8541))
* Fixed crash that could occur when parsing a .torrent file with a bad `pieces` key. ([#8542](https://github.com/transmission/transmission/pull/8542))
* Fixed potential file descriptor leak when launching scripts on POSIX systems. ([#8549](https://github.com/transmission/transmission/pull/8549))
* Changed the network traffic algorithm to spread bandwidth more evenly amongst peers. ([#8259](https://github.com/transmission/transmission/pull/8259))
* Improved laggy user interface when bandwidth usage is high. ([#8454](https://github.com/transmission/transmission/pull/8454))

### macOS Client

* Fixed a `4.1.0` crash that occurred if deleting a torrent's files on macOS  returned a system error. ([#8275](https://github.com/transmission/transmission/pull/8275))
* Fixed a crash in the "Rename File ..." dialog when trying to rename a torrent right when the torrent finished downloading. ([#8425](https://github.com/transmission/transmission/pull/8425))
* Fixed `4.1.0` crash when removing a torrent that was being show in the Inspector. ([#8496](https://github.com/transmission/transmission/pull/8496))
* Improved performance of internal Torrent lookup code. ([#8505](https://github.com/transmission/transmission/pull/8505))
* Improved responsiveness when scrolling the torrent list with keyboard navigation. ([#8323](https://github.com/transmission/transmission/pull/8323))

### Qt Client

* Fixed a `4.1.0` bug where the RPC error response arguments were not handled. ([#8414](https://github.com/transmission/transmission/pull/8414))
* Fixed a long-standing bug that wouldn't let `file:///` URIs be added from the command line. ([#8448](https://github.com/transmission/transmission/pull/8448))
* Fixed broken icons in the torrent list on Windows. ([#8456](https://github.com/transmission/transmission/pull/8456))

### GTK Client

* Fixed a `4.1.0-beta.5` assertion failure when fetching a blocklist failed on a system compiled with `GLIBCXX_ASSERTIONS` enabled. ([#8273](https://github.com/transmission/transmission/pull/8273))
* Fixed a `4.1.0` bug that wouldn't let magnet links be added from the "Add URL" dialog. ([#8277](https://github.com/transmission/transmission/pull/8277))
* Fixed a `4.1.0` bug that broke keyboard shortcuts when built with GTK3. ([#8293](https://github.com/transmission/transmission/pull/8293))
* Fixed a crash that could happen when removing some torrents. ([#8340](https://github.com/transmission/transmission/pull/8340))
* Fixed a `4.1.0` bug that showed the wrong encryption mode being shown in the Preferences dialog. ([#8345](https://github.com/transmission/transmission/pull/8345))
* Fixed a `4.0.x` bug that prevented a handful of strings from being marked for translation. ([#8350](https://github.com/transmission/transmission/pull/8350))
* Fixed a `4.1.0` packaging error that prevented the Qt and GTK clients from being installed side-by-side on Arch. ([#8387](https://github.com/transmission/transmission/pull/8387))
* Fixed a `4.1.0` bug that wouldn't let magnet links be added from the command line. ([#8415](https://github.com/transmission/transmission/pull/8415))

### Web Client

* Reverted a `4.1.0` change that merged the "Remove torrent" and "Trash torrent" confirmation dialogs into a single dialog. ([#8355](https://github.com/transmission/transmission/pull/8355))
* Fixed a `4.1.0` bug that showed a "Connection failed" popup when opening the "Open torrent" dialog while the current download directory path was invalid. ([#8386](https://github.com/transmission/transmission/pull/8386))

### Everything Else

* Updated documentation. ([#8245](https://github.com/transmission/transmission/pull/8245), [#8526](https://github.com/transmission/transmission/pull/8526))
