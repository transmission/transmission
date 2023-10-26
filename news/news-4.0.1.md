# Transmission 4.0.1

The `4.0.0` release two weeks ago generated a lot of [useful feedback](https://github.com/transmission/transmission/issues) (thank you!!) so we've been busy fixing bugs. This release has only bug fixes, documentation improvements, and build script fixes to make packaging easier.

RIP, Yeeshkul. TJ, your tracker is one of the first things that got me (@ckerr) interested in torrents when I started contributing to the Transmission project back in 2007. So long and thanks for all the fish.

## What's New in 4.0.1

### Highlights

* Added Qt 5 builds for older Windows system, since Qt 6 [requires](https://doc.qt.io/qt-6/windows.html) Windows 10 or newer. ([#4855](https://github.com/transmission/transmission/pull/4855))
* Fixed `4.0.0` bug that caused some torrents to have poor speed and some of their checksums to be incorrectly marked as failed. ([#4879](https://github.com/transmission/transmission/pull/4879), [#4880](https://github.com/transmission/transmission/pull/4880), [#4890](https://github.com/transmission/transmission/pull/4890))
* Fixed `4.0.0` bug that caused [beachballing](https://en.wiktionary.org/wiki/beachball#Verb) / [jank](https://en.wiktionary.org/wiki/jank#Noun). ([#4936](https://github.com/transmission/transmission/pull/4936))
* Fixed `4.0.0` bug that caused blocklists to use more memory than necessary. ([#4953](https://github.com/transmission/transmission/pull/4953))
* Fixed `4.0.0` issue that failed to migrate magnet links from Transmission 3. ([#4840](https://github.com/transmission/transmission/pull/4840))

### libtransmission (All Platforms)

* Follow [BEP 27](https://www.bittorrent.org/beps/bep_0027.html)'s placement of `private` field when creating new torrents. ([#4809](https://github.com/transmission/transmission/pull/4809))
* Fixed `4.0.0` bug that failed to retry to connect to peers with TCP if UTP failed first. ([#4897](https://github.com/transmission/transmission/pull/4897))
* Fixed `4.0.0` bug that could prevent port forwarding settings from being saved. ([#4842](https://github.com/transmission/transmission/pull/4842))
* Fixed `4.0.0` crash that occurred when removing a webseed torrent while downloading. ([#4847](https://github.com/transmission/transmission/pull/4847))
* Fixed `4.0.0` regression that paused magnet links when adding them. ([#4856](https://github.com/transmission/transmission/pull/4856))
* Fixed `4.0.0` illegal instruction exception on some x86 Windows machines. ([#4886](https://github.com/transmission/transmission/pull/4886))
* Fixed `4.0.0` build failure due to incompatible system and bundled libutp headers. ([#4877](https://github.com/transmission/transmission/pull/4877))
* Fixed `4.0.0` build failure on NetBSD. ([#4863](https://github.com/transmission/transmission/pull/4863))
* Fixed `4.0.0` build error when building bundled libb64 and libutp. ([#4762](https://github.com/transmission/transmission/pull/4762), [#4810](https://github.com/transmission/transmission/pull/4810))
* Fixed `4.0.0` build failure when compiling with Clang on Windows. ([#4978](https://github.com/transmission/transmission/pull/4978))
* Fixed `4.0.0` build issue that prevented distro versions of libdeflate from being used. ([#4968](https://github.com/transmission/transmission/pull/4968))
* Removed a harmless "unable to read resume file" error message to avoid confusion. ([#4799](https://github.com/transmission/transmission/pull/4799))
* Fixed `4.0.0` libtransmission compiler warnings. ([#4805](https://github.com/transmission/transmission/pull/4805))

### macOS Client

* Added padding to widgets in macOS client to avoid being hidden below scroller. ([#4788](https://github.com/transmission/transmission/pull/4788))
* Fixed `4.0.0` layout issue in the pieces view. ([#4884](https://github.com/transmission/transmission/pull/4884))
* Fixed `4.0.0` bug that didn't highlight the current selection in `View > Use Groups`. ([#4896](https://github.com/transmission/transmission/pull/4896))
* Fixed dock icon badge colors to Apple accessibility guidelines. ([#4813](https://github.com/transmission/transmission/pull/4813))

### Qt Client

* Fixed incorrect display of some trackers' announce URLs. ([#4846](https://github.com/transmission/transmission/pull/4846))
* Fixed Qt 6 deprecation warnings. ([#4710](https://github.com/transmission/transmission/pull/4710))
* Fixed "Open Folder" feature for local sessions. ([#4963](https://github.com/transmission/transmission/pull/4963))

### GTK Client

* Fixed `4.0.0` crash on opening torrent details dialog. ([#4859](https://github.com/transmission/transmission/pull/4859))
* Fixed `4.0.0` bug that froze the app after showing or hiding via system tray (GTK 3 only). ([#4939](https://github.com/transmission/transmission/pull/4939))
* Fixed `4.0.0` rounding error in the progressbar's percentage display. ([#4933](https://github.com/transmission/transmission/pull/4933))
* Fixed `4.0.0` blurred progress bars in main window. ([#4756](https://github.com/transmission/transmission/pull/4756))
* Fixed awkward grammar in the Details Dialog's running-time row. ([#4898](https://github.com/transmission/transmission/pull/4898))

### Web Client

* Fixed `4.0.0` bug that failed to apply settings changes immediately. ([#4839](https://github.com/transmission/transmission/pull/4839))
* Fixed label searches that have spaces or hyphens. ([#4932](https://github.com/transmission/transmission/pull/4932))
* Fixed highlight color of selected context menu rows in dark mode. ([#4984](https://github.com/transmission/transmission/pull/4984))

### Everything Else

* Changed default build to skip `clang-tidy` linting due to [upstream](https://github.com/llvm/llvm-project/issues/59492) bug. ([#4824](https://github.com/transmission/transmission/pull/4824))
* Build: Use CXX symbol checking to verify system libutp. ([#4909](https://github.com/transmission/transmission/pull/4909))
* Build: add option to disable installation of web assets. ([#4906](https://github.com/transmission/transmission/pull/4906))a
* Build: set /utf-8 flag when using MSVC. ([#4975](https://github.com/transmission/transmission/pull/4975))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to libtransmission (All Platforms):

* [@0-wiz-0 (Thomas Klausner)](https://github.com/0-wiz-0):
  * Fixed `4.0.0` build failure on NetBSD. ([#4863](https://github.com/transmission/transmission/pull/4863))
* [@Berbe](https://github.com/Berbe):
  * Fixed `4.0.0` compiler warnings when building libtransmission. ([#4805](https://github.com/transmission/transmission/pull/4805))
* [@GaryElshaw (Gary Elshaw)](https://github.com/GaryElshaw):
  * Code review for [#4856](https://github.com/transmission/transmission/pull/4856)
* [@goldsteinn](https://github.com/goldsteinn):
  * Fixed `4.0.0` illegal instruction exception on some x86 Windows machines. ([#4886](https://github.com/transmission/transmission/pull/4886))
* [@reardonia](https://github.com/reardonia):
  * Code review for [#4826](https://github.com/transmission/transmission/pull/4826)
* [@sweetppro (SweetPPro)](https://github.com/sweetppro):
  * Code review for [#4856](https://github.com/transmission/transmission/pull/4856), [#4895](https://github.com/transmission/transmission/pull/4895)
* [@xavery (Daniel Kamil Kozar)](https://github.com/xavery):
  * Follow [BEP 27](https://www.bittorrent.org/beps/bep_0027.html)'s placement of `private` field when creating new torrents. ([#4809](https://github.com/transmission/transmission/pull/4809))

### Contributions to macOS Client:

* [@GaryElshaw (Gary Elshaw)](https://github.com/GaryElshaw):
  * Fixed dock icon badge colors to Apple accessibility guidelines. ([#4813](https://github.com/transmission/transmission/pull/4813))
* [@nevack (Dzmitry Neviadomski)](https://github.com/nevack):
  * Code review for [#4884](https://github.com/transmission/transmission/pull/4884)
* [@sweetppro (SweetPPro)](https://github.com/sweetppro):
  * Added padding to widgets in macOS client to avoid being hidden below scroller. ([#4788](https://github.com/transmission/transmission/pull/4788))
  * Fixed `4.0.0` layout issue in the pieces view. ([#4884](https://github.com/transmission/transmission/pull/4884))
  * Fixed `4.0.0` bug that didn't highlight the current selection in `View > Use Groups`. ([#4896](https://github.com/transmission/transmission/pull/4896))

### Contributions to Qt Client:

* [@dmantipov (Dmitry Antipov)](https://github.com/dmantipov):
  * Fixed Qt 6 deprecation warnings. ([#4710](https://github.com/transmission/transmission/pull/4710))

### Contributions to Web Client:

* [@elboletaire (Òscar Casajuana)](https://github.com/elboletaire):
  * Fixed label searches that have spaces or hyphens. ([#4932](https://github.com/transmission/transmission/pull/4932))
* [@GaryElshaw (Gary Elshaw)](https://github.com/GaryElshaw):
  * Fix: make context menu highlighted row readable in dark mode. ([#4984](https://github.com/transmission/transmission/pull/4984))

### Contributions to Everything Else:

* [@abubaca4](https://github.com/abubaca4):
  * Build: reduce minimum libdeflate version to 1.7. ([#4970](https://github.com/transmission/transmission/pull/4970))
* [@Berbe](https://github.com/Berbe):
  * Updated documentation. ([#4803](https://github.com/transmission/transmission/pull/4803))
  * Fix: Prevent lengthy compilation workflows to run needlessly. ([#4804](https://github.com/transmission/transmission/pull/4804))
  * Improved documentation on settings.json options. ([#4812](https://github.com/transmission/transmission/pull/4812))
* [@bheesham (Bheesham Persaud)](https://github.com/bheesham):
  * Build: Use CXX symbol checking to verify system libutp. ([#4909](https://github.com/transmission/transmission/pull/4909))
* [@fetzu (Julien)](https://github.com/fetzu):
  * Chore: normalized and updated copyright to 2023. ([#4834](https://github.com/transmission/transmission/pull/4834))
* [@fghzxm](https://github.com/fghzxm):
  * Build: do not test utils if not building utils. ([#4946](https://github.com/transmission/transmission/pull/4946))
  * Build: set /utf-8 flag when using MSVC. ([#4975](https://github.com/transmission/transmission/pull/4975))
* [@floppym (Mike Gilbert)](https://github.com/floppym):
  * Build: pass `--no-warn-unused-cli` to child CMake process. ([#4807](https://github.com/transmission/transmission/pull/4807))
* [@t-8ch (Thomas Weißschuh)](https://github.com/t-8ch):
  * Test: expose libtransmission gtests to ctest. ([#4731](https://github.com/transmission/transmission/pull/4731))
  * Build: install rebuilt web if available. ([#4865](https://github.com/transmission/transmission/pull/4865))
  * Build: add option to disable installation of web assets. ([#4906](https://github.com/transmission/transmission/pull/4906))
