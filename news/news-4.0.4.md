# Transmission 4.0.4

This is a bugfix-only release. Everyone's feedback on 4.0.x has been very helpful -- thanks for all the suggestions, bug reports, and pull requests!


## What's New in 4.0.4

### All Platforms

* Fixed bug in sending torrent metadata to peers. ([#5460](https://github.com/transmission/transmission/pull/5460))
* Avoid unnecessary heap memory allocations. ([#5520](https://github.com/transmission/transmission/pull/5520), [#5527](https://github.com/transmission/transmission/pull/5527))
* Fixed filename collision edge case when renaming files. ([#5563](https://github.com/transmission/transmission/pull/5563))
* Fixed locale errors that broke number rounding when displaying statistics, e.g. upload / download ratios. ([#5587](https://github.com/transmission/transmission/pull/5587))
* Always use a fixed-length key query in tracker announces. This isn't required by the [spec](https://www.bittorrent.org/beps/bep_0007.html), but some trackers rely on that fixed length because it's common practice by other BitTorrent clients. ([#5652](https://github.com/transmission/transmission/pull/5652))
* Fixed potential Windows crash when [getstdhandle()](https://learn.microsoft.com/en-us/windows/console/getstdhandle) returns `NULL`. ([#5675](https://github.com/transmission/transmission/pull/5675))
* Fixed `4.0.0` bug where the port numbers in LDP announces are sometimes malformed. ([#5825](https://github.com/transmission/transmission/pull/5825))
* Fixed a bug that prevented editing the query part of a tracker URL. ([#5871](https://github.com/transmission/transmission/pull/5871))
* Fixed a bug where Transmission may not announce LPD on its listening interface. ([#5896](https://github.com/transmission/transmission/pull/5896))
* Made small performance improvements in libtransmission. ([#5715](https://github.com/transmission/transmission/pull/5715))

### macOS Client

* Updated code that had been using deprecated API. ([#5633](https://github.com/transmission/transmission/pull/5633))

### Qt Client

* Fixed torrent name rendering when showing magnet links in compact view. ([#5491](https://github.com/transmission/transmission/pull/5491))
* Fixed bug that broke the "Move torrent file to trash" setting. ([#5505](https://github.com/transmission/transmission/pull/5505))
* Fixed Qt 6.4 deprecation warning. ([#5552](https://github.com/transmission/transmission/pull/5552))
* Fixed poor resolution of Qt application icon. ([#5570](https://github.com/transmission/transmission/pull/5570))

### GTK Client

* Fixed missing 'Remove torrent' tooltip. ([#5777](https://github.com/transmission/transmission/pull/5777))

### Web Client

* Don't show `null` as a tier name in the inspector's tier list. ([#5462](https://github.com/transmission/transmission/pull/5462))
* Fixed truncated play / pause icons. ([#5771](https://github.com/transmission/transmission/pull/5771))
* Fixed overflow when rendering peer lists and made speed indicators honor `prefers-color-scheme` media queries. ([#5814](https://github.com/transmission/transmission/pull/5814))
* Made the main menu accessible even on smaller displays. ([#5827](https://github.com/transmission/transmission/pull/5827))

### transmission-cli

* Fixed "no such file or directory" warning when adding a magnet link. ([#5426](https://github.com/transmission/transmission/pull/5426))
* Fixed bug that caused the wrong decimal separator to be used in some locales. ([#5444](https://github.com/transmission/transmission/pull/5444))

### transmission-remote

* Fixed display bug that failed to show some torrent labels. ([#5572](https://github.com/transmission/transmission/pull/5572))

### Everything Else

* Ran all PNG files through lossless compressors to make them smaller. ([#5586](https://github.com/transmission/transmission/pull/5586))
* Fixed potential build issue when compiling on macOS with gcc. ([#5632](https://github.com/transmission/transmission/pull/5632))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @basilefff ([Василий Чай](https://github.com/basilefff)):
  * Fixed filename collision edge case when renaming files. ([#5563](https://github.com/transmission/transmission/pull/5563))
* @G-Ray ([Geoffrey Bonneville](https://github.com/G-Ray)):
  * Fixed potential Windows crash when [getstdhandle()](https://learn.microsoft.com/en-us/windows/console/getstdhandle) returns `NULL`. ([#5675](https://github.com/transmission/transmission/pull/5675))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Typos in libtransmission source code comments. ([#5473](https://github.com/transmission/transmission/pull/5473))
* @KyleSanderson ([Kyle Sanderson](https://github.com/KyleSanderson)):
  * Fixed uninitialized session_id_t values. ([#5396](https://github.com/transmission/transmission/pull/5396))
* @qzydustin ([Zhenyu Qi](https://github.com/qzydustin)):
  * Fixed a bug that prevented editing the query part of a tracker URL. ([#5871](https://github.com/transmission/transmission/pull/5871))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#5871](https://github.com/transmission/transmission/pull/5871), [#5896](https://github.com/transmission/transmission/pull/5896))
  * Always use a fixed-length key query in tracker announces. This isn't required by the [spec](https://www.bittorrent.org/beps/bep_0007.html), but some trackers rely on that fixed length because it's common practice by other BitTorrent clients. ([#5652](https://github.com/transmission/transmission/pull/5652))
  * Refactor: share `CompareCacheBlockByKey` by constexpr static member. ([#5678](https://github.com/transmission/transmission/pull/5678))
  * Perf: convert comparator functors to static constexpr. ([#5687](https://github.com/transmission/transmission/pull/5687))
  * Perf: reduce copying in `enforceSwarmPeerLimit()`. ([#5731](https://github.com/transmission/transmission/pull/5731))
  * Fix: clamp down harder for upload as well. ([#5821](https://github.com/transmission/transmission/pull/5821))
  * Fixed `4.0.0` bug where the port numbers in LDP announces are sometimes malformed. ([#5825](https://github.com/transmission/transmission/pull/5825))

### Contributions to macOS Client:

* @i0ntempest ([Zhenfu Shi](https://github.com/i0ntempest)):
  * Fix: wrong case in AppKit.h. ([#5456](https://github.com/transmission/transmission/pull/5456))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#5456](https://github.com/transmission/transmission/pull/5456), [#5633](https://github.com/transmission/transmission/pull/5633))

### Contributions to Qt Client:

* @dmantipov ([Dmitry Antipov](https://github.com/dmantipov)):
  * Fixed Qt 6.4 deprecation warning. ([#5552](https://github.com/transmission/transmission/pull/5552))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Fixed poor resolution of Qt application icon. ([#5570](https://github.com/transmission/transmission/pull/5570))
* @sfan5:
  * Code review. ([#5505](https://github.com/transmission/transmission/pull/5505))

### Contributions to GTK Client:

* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Fixed missing 'Remove torrent' tooltip. ([#5777](https://github.com/transmission/transmission/pull/5777))

### Contributions to Web Client:

* @dareiff ([Derek Reiff](https://github.com/dareiff)):
  * Fixed overflow when rendering peer lists and made speed indicators honor `prefers-color-scheme` media queries. ([#5814](https://github.com/transmission/transmission/pull/5814))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Fixed truncated play / pause icons. ([#5771](https://github.com/transmission/transmission/pull/5771))
* @Rukario:
  * Made the main menu accessible even on smaller displays. ([#5827](https://github.com/transmission/transmission/pull/5827))

### Contributions to transmission-remote:

* @rsekman ([Robin Seth Ekman](https://github.com/rsekman)):
  * Fixed display bug that failed to show some torrent labels. ([#5572](https://github.com/transmission/transmission/pull/5572))

### Contributions to Everything Else:

* @barracuda156 ([Sergey Fedorov](https://github.com/barracuda156)):
  * Code review. ([#5632](https://github.com/transmission/transmission/pull/5632))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Typos in libtransmission tests source code comments. ([#5468](https://github.com/transmission/transmission/pull/5468))
* @JonatanWick ([Jonatan](https://github.com/JonatanWick)):
  * Docs: update translation site names. ([#5481](https://github.com/transmission/transmission/pull/5481))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#5632](https://github.com/transmission/transmission/pull/5632))
* @PHLAK ([Chris Kankiewicz](https://github.com/PHLAK)):
  * Docs: update documentation of logging levels (#5700). ([#5700](https://github.com/transmission/transmission/pull/5700))

