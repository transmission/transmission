# Transmission 4.0.2

This is another bugfix-only release. Thanks for all the suggestions, bug reports, and code -- the feedback on 4.0.x has been very helpful!

Transmission 4 needs translators! Check out [this page](https://github.com/transmission/transmission/blob/main/docs/Translating.md) if you'd like to help.


## What's New in 4.0.2

### Highlights

* Fixed `4.0.1` bug where some torrents thought they were magnet links. ([#5025](https://github.com/transmission/transmission/pull/5025))
* Fixed `4.0.0` bug that broke watchdirs in the macOS client. ([#5058](https://github.com/transmission/transmission/pull/5058), macOS Client)
* Fixed `4.0.0` bug where download speed limits were ignored for uTP peers. ([#5086](https://github.com/transmission/transmission/pull/5086))
* Fixed `4.0.0` bug using `announce-list` when creating single-tracker private torrents. ([#5106](https://github.com/transmission/transmission/pull/5106))

### All Platforms

* Restored support for `path.utf-8` keys in torrent info dictionaries. ([#3454](https://github.com/transmission/transmission/pull/3454))
* Fixed value of `TR_TIME_LOCALTIME` environment variable in torrent scripts. ([#5006](https://github.com/transmission/transmission/pull/5006))
* Limited in-kernel file copying to 2 GiB blocks at a time to avoid potential issues with CIFS mounts. ([#5039](https://github.com/transmission/transmission/pull/5039))
* Simplified filename info in log messages. ([#5055](https://github.com/transmission/transmission/pull/5055))
* Fixed `std::clamp()` assertion failures. ([#5080](https://github.com/transmission/transmission/pull/5080), [#5203](https://github.com/transmission/transmission/pull/5203))
* Fixed small error calculating protocol overhead when receiving peer messages. ([#5091](https://github.com/transmission/transmission/pull/5091))
* Fixed incorrect escaping of non-BMP characters when generating JSON. ([#5096](https://github.com/transmission/transmission/pull/5096))
* Fixed `4.0.0` crash when receiving malformed piece data from peers. ([#5097](https://github.com/transmission/transmission/pull/5097))
* Fixed `4.0.0` potential crash when downloading from webseeds. ([#5161](https://github.com/transmission/transmission/pull/5161))
* Improved handling of the `leechers` param in trackers' announce responses. ([#5164](https://github.com/transmission/transmission/pull/5164))
* Fixed `4.0.0` regression that stopped increasing the download priority of files' first and last pieces. These pieces are important for making incomplete files previewable / playable while still being downloaded. ([#5167](https://github.com/transmission/transmission/pull/5167))
* Fixed display of IPv6 tracker URLs. ([#5174](https://github.com/transmission/transmission/pull/5174))
* Fixed code that could stop being interested in peers that have pieces we want to download. ([#5176](https://github.com/transmission/transmission/pull/5176))
* Improved sanity checking of magnet links added via RPC. ([#5202](https://github.com/transmission/transmission/pull/5202))
* Fixed a misleading error message when Transmission is unable to write to the incomplete-dir. ([#5217](https://github.com/transmission/transmission/pull/5217))
* Worked around an [older libdht bug](https://github.com/jech/dht/issues/29) that could provide invalid peer info. ([#5218](https://github.com/transmission/transmission/pull/5218))
* Restored RPC `torrentGet.wanted` return value to match 3.00 behavior. ([#5170](https://github.com/transmission/transmission/pull/5170))

### macOS Client

* Fixed minor UI bugs, e.g. layout and control alignment. ([#5016](https://github.com/transmission/transmission/pull/5016), [#5018](https://github.com/transmission/transmission/pull/5018), [#5019](https://github.com/transmission/transmission/pull/5019), [#5021](https://github.com/transmission/transmission/pull/5021), [#5035](https://github.com/transmission/transmission/pull/5035), [#5066](https://github.com/transmission/transmission/pull/5066))
* Added up / down arrows to upload / download badge info. ([#5095](https://github.com/transmission/transmission/pull/5095))
* Fixed `4.0.0` bug where macOS users could see some of their old torrents reappear after removing & restarting. ([#5117](https://github.com/transmission/transmission/pull/5117))
* Fixed "Unrecognized colorspace number -1" error messages from macOS. ([#5219](https://github.com/transmission/transmission/pull/5219))
* Fixed bug that caused local data to not be found when adding a new torrent in a custom folder. ([#5060](https://github.com/transmission/transmission/pull/5060))
* Fixed crash on startup in `copyWithZone()`. ([#5079](https://github.com/transmission/transmission/pull/5079))

### Qt Client

* Ensured that "Open File" opens the torrent's folder for multi-file torrents. ([#5115](https://github.com/transmission/transmission/pull/5115))
* Fixed `4.0.0` bug that prevented batch-adding trackers to multiple torrents at once. ([#5122](https://github.com/transmission/transmission/pull/5122))
* Fixed per-torrent ratio display in main window. ([#5193](https://github.com/transmission/transmission/pull/5193))

### GTK Client

* Fixed `4.0.0` ignoring `-m`/`--minimized` command line option. ([#5175](https://github.com/transmission/transmission/pull/5175))
* Fixed assertion failure in the progress display when creating a new torrent. ([#5180](https://github.com/transmission/transmission/pull/5180))

### Web Client

* Fixed minor UI bugs, e.g. layout and control alignment. ([#5001](https://github.com/transmission/transmission/pull/5001))
* Fixed `4.0.0` bug that that failed to save alternate speed begin/end settings changes. ([#5033](https://github.com/transmission/transmission/pull/5033))
* Fixed broken keyboard shortcuts on desktop Safari. ([#5054](https://github.com/transmission/transmission/pull/5054))
* Improved colors in both light & dark mode. ([#5083](https://github.com/transmission/transmission/pull/5083), [#5114](https://github.com/transmission/transmission/pull/5114), [#5151](https://github.com/transmission/transmission/pull/5151))

### Daemon

* Made the "unrecognized argument" error message more readable. ([#5029](https://github.com/transmission/transmission/pull/5029))

### transmission-remote

* Fixed a spurious error message when adding magnet links. ([#5088](https://github.com/transmission/transmission/pull/5088))

### Everything Else

* Documentation improvements. ([#4971](https://github.com/transmission/transmission/pull/4971), [#4980](https://github.com/transmission/transmission/pull/4980), [#5099](https://github.com/transmission/transmission/pull/5099), [#5135](https://github.com/transmission/transmission/pull/5135), [#5214](https://github.com/transmission/transmission/pull/5214), [#5225](https://github.com/transmission/transmission/pull/5225))
* Updated translations. ([#5182](https://github.com/transmission/transmission/pull/5182))
* Fixed `4.0.1` failure to discover tests when cross-compiling without an emulator. ([#5197](https://github.com/transmission/transmission/pull/5197))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @HAkos1:
  * Simplified filename info in log messages. ([#5055](https://github.com/transmission/transmission/pull/5055))
* @Lanzaa ([Colin B.](https://github.com/Lanzaa)):
  * Restored support for `path.utf-8` keys in torrent info dictionaries. ([#3454](https://github.com/transmission/transmission/pull/3454))
* @reardonia:
  * Improved handling of the `leechers` param in trackers' announce responses. ([#5164](https://github.com/transmission/transmission/pull/5164))
  * Restored RPC `torrentGet.wanted` return value to match 3.00 behavior. ([#5170](https://github.com/transmission/transmission/pull/5170))
* @wiz78:
  * Fixed value of `TR_TIME_LOCALTIME` environment variable in torrent scripts. ([#5006](https://github.com/transmission/transmission/pull/5006))
* @xavery ([Daniel Kamil Kozar](https://github.com/xavery)):
  * Fixed incorrect escaping of non-BMP characters when generating JSON. ([#5096](https://github.com/transmission/transmission/pull/5096))

### Contributions to macOS Client:

* @andreygursky:
  * Code review. ([#5060](https://github.com/transmission/transmission/pull/5060))
* @DevilDimon ([Dmitry Serov](https://github.com/DevilDimon)):
  * Code review. ([#5079](https://github.com/transmission/transmission/pull/5079))
* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Code review. ([#5095](https://github.com/transmission/transmission/pull/5095))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#5058](https://github.com/transmission/transmission/pull/5058), [#5095](https://github.com/transmission/transmission/pull/5095), [#5168](https://github.com/transmission/transmission/pull/5168))
  * Fixed minor UI bugs, e.g. layout and control alignment. ([#5016](https://github.com/transmission/transmission/pull/5016), [#5018](https://github.com/transmission/transmission/pull/5018), [#5019](https://github.com/transmission/transmission/pull/5019), [#5021](https://github.com/transmission/transmission/pull/5021), [#5035](https://github.com/transmission/transmission/pull/5035))

### Contributions to Web Client:

* @dareiff ([Derek Reiff](https://github.com/dareiff)):
  * Code review. ([#5151](https://github.com/transmission/transmission/pull/5151))
  * Improved colors in both light & dark mode. ([#5083](https://github.com/transmission/transmission/pull/5083), [#5114](https://github.com/transmission/transmission/pull/5114))
* @tessus ([Helmut K. C. Tessarek](https://github.com/tessus)):
  * Fixed broken keyboard shortcuts on desktop Safari. ([#5054](https://github.com/transmission/transmission/pull/5054))
* @wsy2220:
  * Improved colors in both light & dark mode. ([#5151](https://github.com/transmission/transmission/pull/5151))

### Contributions to Daemon:

* @maxz ([Max Zettlmeißl](https://github.com/maxz)):
  * Code review. ([#5029](https://github.com/transmission/transmission/pull/5029))
* @RoeyFuchs ([Roey Fuchs](https://github.com/RoeyFuchs)):
  * Made the "unrecognized argument" error message more readable. ([#5029](https://github.com/transmission/transmission/pull/5029))

### Contributions to Everything Else:

* @GaryElshaw ([Gary Elshaw](https://github.com/GaryElshaw)):
  * Docs: warn users about bannable actions in the issue template. ([#5059](https://github.com/transmission/transmission/pull/5059))
* @ile6695 ([Ilkka Kallioniemi](https://github.com/ile6695)):
  * Documentation improvements. ([#5225](https://github.com/transmission/transmission/pull/5225))
* @maxz ([Max Zettlmeißl](https://github.com/maxz)):
  * Documentation improvements. ([#4971](https://github.com/transmission/transmission/pull/4971))
* @midzer:
  * Documentation improvements. ([#4980](https://github.com/transmission/transmission/pull/4980))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#5059](https://github.com/transmission/transmission/pull/5059))
* @tearfur:
  * Docs: add libssl-dev to Ubuntu dependencies. ([#5134](https://github.com/transmission/transmission/pull/5134))
  * Documentation improvements. ([#5214](https://github.com/transmission/transmission/pull/5214))
* @trim21 ([Trim21](https://github.com/trim21)):
  * Documentation improvements. ([#5099](https://github.com/transmission/transmission/pull/5099))

