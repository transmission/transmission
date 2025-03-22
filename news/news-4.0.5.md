# Transmission 4.0.5

This is a bugfix-only release. Everyone's feedback on 4.0.x has been very helpful -- thanks for all the suggestions, bug reports, and pull requests!

## What's New in 4.0.5

### Highlights

* Fixed `4.0.0` bug where the `IP address` field in UDP announces were not encoded in network byte order. [[BEP-15](https://www.bittorrent.org/beps/bep_0015.html)]. ([#6132](https://github.com/transmission/transmission/pull/6132))
* Fixed a bug that incorrectly escaped JSON strings in some locales. ([#6005](https://github.com/transmission/transmission/pull/6005), [#6133](https://github.com/transmission/transmission/pull/6133))
* Fixed `4.0.4` decreased download speeds for people who set a low upload bandwidth limit. ([#6134](https://github.com/transmission/transmission/pull/6134))

### All Platforms

* Fixed bug that prevented editing trackers on magnet links. ([#5957](https://github.com/transmission/transmission/pull/5957))
* Fixed HTTP tracker announces and scrapes sometimes failing after adding a torrent file by HTTPS URL. ([#5969](https://github.com/transmission/transmission/pull/5969))
* In RPC responses, change the default sort order of torrents to match Transmission 3.00. ([#5604](https://github.com/transmission/transmission/pull/5604))
* Fixed `tr_sys_path_copy()` behavior on some Synology Devices. ([#5974](https://github.com/transmission/transmission/pull/5974))

### macOS Client

* Support Sonoma when building from sources. ([#6016](https://github.com/transmission/transmission/pull/6016), [#6051](https://github.com/transmission/transmission/pull/6051))
* Fixed early truncation of long group names in groups list. ([#6104](https://github.com/transmission/transmission/pull/6104))

### Qt Client

* Fix: only append `.added` suffix to watchdir files. ([#5705](https://github.com/transmission/transmission/pull/5705))

### GTK Client

* Fixed crash when opening torrent file from "Recently used" section in GTK 4. ([#6131](https://github.com/transmission/transmission/pull/6131), [#6142](https://github.com/transmission/transmission/pull/6142))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @chantzish:
  * Fixed bug that prevented editing trackers on magnet links. ([#5957](https://github.com/transmission/transmission/pull/5957))
* @hgy59:
  * Fixed tr_sys_path_copy in file-posix.cc for some Synology Devices (#5966). ([#5974](https://github.com/transmission/transmission/pull/5974))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#5974](https://github.com/transmission/transmission/pull/5974))
  * Fixed HTTP tracker announces and scrapes sometimes failing after adding a torrent file by HTTPS URL. ([#5969](https://github.com/transmission/transmission/pull/5969))
  * Fixed a bug where Transmission is incorrectly escaping JSON strings in some locales. ([#6005](https://github.com/transmission/transmission/pull/6005), [#6133](https://github.com/transmission/transmission/pull/6133))
  * Fixed `4.0.4` decreased download speeds for people who set a low upload bandwidth limit. ([#6082](https://github.com/transmission/transmission/pull/6082), [#6134](https://github.com/transmission/transmission/pull/6134))
  * Fixed `4.0.0` bug where the `IP address` field in UDP announces are not encoded in network byte order. [[BEP-15](https://www.bittorrent.org/beps/bep_0015.html)]. ([#6126](https://github.com/transmission/transmission/pull/6126), [#6132](https://github.com/transmission/transmission/pull/6132))
  * Fix: formatting cmdline help message. ([#6174](https://github.com/transmission/transmission/pull/6174))

### Contributions to macOS Client:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#6104](https://github.com/transmission/transmission/pull/6104))
* @sweetppro ([SweetPPro](https://github.com/sweetppro)):
  * Code review. ([#6016](https://github.com/transmission/transmission/pull/6016), [#6051](https://github.com/transmission/transmission/pull/6051))

### Contributions to GTK Client:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Fixed crash when opening torrent file from "Recently used" section in GTK 4. ([#6131](https://github.com/transmission/transmission/pull/6131), [#6142](https://github.com/transmission/transmission/pull/6142), [#6144](https://github.com/transmission/transmission/pull/6144))

