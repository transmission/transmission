# Transmission 4.0.3

This is a bugfix-only release. Everyone's feedback on 4.0.x has been very helpful -- thanks for all the suggestions, bug reports, and pull requests!


## What's New in 4.0.3

### All Platforms

* Fixed `4.0.2` higher CPU load while downloading. Regression introduced by [#5167](https://github.com/transmission/transmission/pull/5167). ([#5266](https://github.com/transmission/transmission/pull/5266), [#5273](https://github.com/transmission/transmission/pull/5273))
* Fixed `4.0.0` bug where the `torrentGet` RPC method returned wrong `trackerStats.tier` values. ([#5274](https://github.com/transmission/transmission/pull/5274))
* Fixed `4.0.0` HTTP announce behavior with `bind-address-ipv*` settings. ([#5296](https://github.com/transmission/transmission/pull/5296))
* Fixed `4.0.0` bug in code that detects the computer's IPv6 support. ([#5312](https://github.com/transmission/transmission/pull/5312))
* Silenced `4.0.0` minor log warnings for `cross_seed_entry` and `uid` entries in torrent files. ([#5365](https://github.com/transmission/transmission/pull/5365))
* When adding a duplicate torrent via the RPC API, the return value now matches Transmission 3's return value. ([#5370](https://github.com/transmission/transmission/pull/5370))
* Fixed use of metainfo display-name as a fallback name. ([#5378](https://github.com/transmission/transmission/pull/5378))
* Updated torrent Peer ID generation to happen once per session, even for public torrents. ([#5233](https://github.com/transmission/transmission/pull/5233))

### macOS Client

* Added support for non-UTF-8 magnets. ([#5244](https://github.com/transmission/transmission/pull/5244))
* Fixed potential memory leak in `tr_strv_convert_utf8()`. ([#5264](https://github.com/transmission/transmission/pull/5264))
* Fixed crash on launch from tapping on a notification. ([#5280](https://github.com/transmission/transmission/pull/5280))

### Qt Client

* Fixed `4.0.2` FTBFS on Qt 5.13. ([#5238](https://github.com/transmission/transmission/pull/5238))

### GTK Client

* Fixed `4.0.0` preferences dialog being too large for small displays. ([#5276](https://github.com/transmission/transmission/pull/5276), [#5360](https://github.com/transmission/transmission/pull/5360))
* Fixed `4.0.0` regression of percents, speeds, sizes, etc. not being i18nized properly. ([#5288](https://github.com/transmission/transmission/pull/5288))
* Fixed FTBFS in GTKMM 4.10. ([#5289](https://github.com/transmission/transmission/pull/5289), [#5295](https://github.com/transmission/transmission/pull/5295))

### Web Client

* Fixed confusing Inspector UI when waiting for initial data from the server. ([#5249](https://github.com/transmission/transmission/pull/5249))
* Fixed a keyboard shortcut conflict. ([#5318](https://github.com/transmission/transmission/pull/5318))
* Turned off keyboard shortcuts when input fields have focus. ([#5381](https://github.com/transmission/transmission/pull/5381))
* Show announce URL's origins in the inspector's tracker list. ([#5382](https://github.com/transmission/transmission/pull/5382))
* Added missing date-added field in the Inspector's info tab. ([#5386](https://github.com/transmission/transmission/pull/5386))

### Daemon

* Set the log level sooner at startup to ensure events aren't missed. ([#5345](https://github.com/transmission/transmission/pull/5345))

### transmission-remote

* Fixed `4.0.0` bug in the display of how much of a torrent has been downloaded. ([#5265](https://github.com/transmission/transmission/pull/5265))

### Everything Else

* Bumped fallback version of [`libdeflate`](https://github.com/ebiggers/libdeflate) from v1.17 to bugfix release v1.18. ([#5388](https://github.com/transmission/transmission/pull/5388))
* Documentation improvements. ([#5278](https://github.com/transmission/transmission/pull/5278))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @tearfur:
  * Fixed `4.0.0` HTTP announce behavior with `bind-address-ipv*` settings. ([#5296](https://github.com/transmission/transmission/pull/5296))
  * Fixed `4.0.0` bug in code that detects the computer's IPv6 support. ([#5312](https://github.com/transmission/transmission/pull/5312))

### Contributions to macOS Client:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#5280](https://github.com/transmission/transmission/pull/5280))

### Contributions to GTK Client:

* @albino ([lawrence](https://github.com/albino)):
  * Fix: restore accidentally-deleted copyright notice (GTK). ([#5372](https://github.com/transmission/transmission/pull/5372))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Fixed `4.0.0` preferences dialog being too large for small displays. ([#5360](https://github.com/transmission/transmission/pull/5360))

### Contributions to Web Client:

* @dareiff ([Derek Reiff](https://github.com/dareiff)):
  * Fixed confusing Inspector UI when waiting for initial data from the server. ([#5249](https://github.com/transmission/transmission/pull/5249))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Code review. ([#5249](https://github.com/transmission/transmission/pull/5249))
* @sfan5:
  * Code review. ([#5318](https://github.com/transmission/transmission/pull/5318))
* @timtas ([Tim Tassonis](https://github.com/timtas)):
  * Code review. ([#5318](https://github.com/transmission/transmission/pull/5318))

### Contributions to Daemon:

* @tearfur:
  * Set the log level sooner at startup to ensure events aren't missed. ([#5345](https://github.com/transmission/transmission/pull/5345))

### Contributions to transmission-remote:

* @hoimic:
  * Fixed `4.0.0` bug in the display of how much of a torrent has been downloaded. ([#5265](https://github.com/transmission/transmission/pull/5265))

### Contributions to Everything Else:

* @G-Ray ([Geoffrey Bonneville](https://github.com/G-Ray)):
  * Documentation improvements. ([#5278](https://github.com/transmission/transmission/pull/5278))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#5286](https://github.com/transmission/transmission/pull/5286))

