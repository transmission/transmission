# Transmission 4.0.6

This is a bugfix-only release. Everyone's feedback on 4.0.x has been very helpful -- thanks for all the suggestions, bug reports, and pull requests!


## What's New in 4.0.6

### All Platforms

* Improved parsing HTTP tracker announce response. ([#6223](https://github.com/transmission/transmission/pull/6223))
* Fixed `4.0.0` bug that caused some user scripts to have an invalid `TR_TORRENT_TRACKERS` environment variable. ([#6434](https://github.com/transmission/transmission/pull/6434))
* Fixed `4.0.0` bug where `alt-speed-enabled` had no effect in `settings.json`. ([#6483](https://github.com/transmission/transmission/pull/6483))
* Fixed `4.0.0` bug where the GTK client's "Use authentication" option was not saved between's sessions. ([#6514](https://github.com/transmission/transmission/pull/6514))
* Fixed `4.0.0` bug where the filename for single-file torrents aren't sanitized. ([#6846](https://github.com/transmission/transmission/pull/6846))

### macOS Client

* Fix: Sparkle support for handling beta version updates. ([#5263](https://github.com/transmission/transmission/pull/5263))
* Fixed app unable to start when having many torrents and TimeMachine enabled. ([#6523](https://github.com/transmission/transmission/pull/6523))
* Fix: Sparkle Version Comparator (#5263). ([#6623](https://github.com/transmission/transmission/pull/6623))

### Qt Client

* Fixed `4.0.0` bug where piece size description text and slider state in torrent creation dialog are not always up-to-date. ([#6516](https://github.com/transmission/transmission/pull/6516))

### GTK Client

* Fixed build when compiling with GTKMM 4. ([#6393](https://github.com/transmission/transmission/pull/6393))
* Added developer name to metainfo files. ([#6598](https://github.com/transmission/transmission/pull/6598))
* Added the launchable desktop-id to metainfo files. ([#6779](https://github.com/transmission/transmission/pull/6779))
* Fixed build when compiling on BSD. ([#6812](https://github.com/transmission/transmission/pull/6812))

### Web Client

* Fixed a `4.0.0` bug where the infinite ratio symbol was displayed incorrectly in the WebUI. ([#6491](https://github.com/transmission/transmission/pull/6491), [#6500](https://github.com/transmission/transmission/pull/6500))
* Fixed layout issue in speed display. ([#6570](https://github.com/transmission/transmission/pull/6570))
* General UI improvement related to filterbar and fixes download/upload speed info wrap. ([#6761](https://github.com/transmission/transmission/pull/6761))

### Daemon

* Fixed a couple of logging issues. ([#6463](https://github.com/transmission/transmission/pull/6463))

### Everything Else

* Updated flatpak release metainfo. ([#6357](https://github.com/transmission/transmission/pull/6357))
* Fixed libtransmission build on very old cmake versions. ([#6418](https://github.com/transmission/transmission/pull/6418))
* UTP peer connections follow user-defined speed limits better now. ([#6551](https://github.com/transmission/transmission/pull/6551))
* Only use a single concurrent queue for timeMachineExclude instead of one queue per torrent (#6523). ([#6558](https://github.com/transmission/transmission/pull/6558))
* Fixed `4.0.5` bug where svg and png icons in the WebUI might not be displayed. ([#6563](https://github.com/transmission/transmission/pull/6563))
* Fixed `4.0.0` bug where `alt-speed-enabled` had no effect in `settings.json`. ([#6564](https://github.com/transmission/transmission/pull/6564))
* Fixed `4.0.0` bugs where some RPC methods don't put torrents in `recently-active` anymore. ([#6565](https://github.com/transmission/transmission/pull/6565))
* Improved parsing HTTP tracker announce response. ([#6567](https://github.com/transmission/transmission/pull/6567))
* Fixed compatibility with clang-format 18. ([#6690](https://github.com/transmission/transmission/pull/6690))
* Fixed build when compiling with mbedtls 3.x . ([#6823](https://github.com/transmission/transmission/pull/6823))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Improved parsing HTTP tracker announce response. ([#6223](https://github.com/transmission/transmission/pull/6223))
  * Fix: add check for `EWOULDBLOCK`. ([#6350](https://github.com/transmission/transmission/pull/6350))
  * Fixed `4.0.0` bug where `alt-speed-enabled` had no effect in `settings.json`. ([#6483](https://github.com/transmission/transmission/pull/6483))
  * Fixed `4.0.0` bug where the GTK client's "Use authentication" option was not saved between's sessions. ([#6514](https://github.com/transmission/transmission/pull/6514))
  * Fixed `4.0.0` bug where the filename for single-file torrents aren't sanitized. ([#6846](https://github.com/transmission/transmission/pull/6846))

### Contributions to macOS Client:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#5263](https://github.com/transmission/transmission/pull/5263), [#6523](https://github.com/transmission/transmission/pull/6523))
* @zorgiepoo ([Zorg](https://github.com/zorgiepoo)):
  * Code review. ([#5263](https://github.com/transmission/transmission/pull/5263))

### Contributions to GTK Client:

* @brad0 ([Brad Smith](https://github.com/brad0)):
  * Fixed build when compiling on BSD. ([#6812](https://github.com/transmission/transmission/pull/6812))
* @wjt ([Will Thompson](https://github.com/wjt)):
  * Added developer name to metainfo files. ([#6598](https://github.com/transmission/transmission/pull/6598))
  * Added launchable tag to metainfo files. ([#6720](https://github.com/transmission/transmission/pull/6720))
  * Added the launchable desktop-id to metainfo files. ([#6779](https://github.com/transmission/transmission/pull/6779))

### Contributions to Web Client:

* @Rukario:
  * General UI improvement related to filterbar and fixes download/upload speed info wrap. ([#6761](https://github.com/transmission/transmission/pull/6761))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#6570](https://github.com/transmission/transmission/pull/6570), [#6761](https://github.com/transmission/transmission/pull/6761))
  * Fixed a `4.0.0` bug where the infinite ratio symbol was displayed incorrectly in the WebUI. ([#6491](https://github.com/transmission/transmission/pull/6491), [#6500](https://github.com/transmission/transmission/pull/6500))
* @tessus ([Helmut K. C. Tessarek](https://github.com/tessus)):
  * Fixed layout issue in speed display. ([#6570](https://github.com/transmission/transmission/pull/6570))

### Contributions to Everything Else:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#6418](https://github.com/transmission/transmission/pull/6418))
* @orangepizza:
  * Fixed build when compiling with mbedtls 3.x . ([#6823](https://github.com/transmission/transmission/pull/6823))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * UTP peer connections follow user-defined speed limits better now. ([#6551](https://github.com/transmission/transmission/pull/6551))
  * Fixed `4.0.5` bug where svg and png icons in the WebUI might not be displayed. ([#6563](https://github.com/transmission/transmission/pull/6563))
  * Fixed `4.0.0` bug where `alt-speed-enabled` had no effect in `settings.json`. ([#6564](https://github.com/transmission/transmission/pull/6564))
  * Fixed `4.0.0` bugs where some RPC methods don't put torrents in `recently-active` anymore. ([#6565](https://github.com/transmission/transmission/pull/6565))
  * Fix: add check for `EWOULDBLOCK` (#6350). ([#6566](https://github.com/transmission/transmission/pull/6566))
  * Improved parsing HTTP tracker announce response. ([#6567](https://github.com/transmission/transmission/pull/6567))
* @wjt ([Will Thompson](https://github.com/wjt)):
  * Updated flatpak release metainfo. ([#6357](https://github.com/transmission/transmission/pull/6357))

