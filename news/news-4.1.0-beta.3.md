# Transmission 4.1.0-beta.3

This is Transmission 4.1.0-beta.3. We're not in feature freeze yet,
so this release includes some new features as well as bugfixes and
performance improvements.


## What's New in 4.1.0-beta.3

### All Platforms

* Added support for using a proxy server for web connections. ([#7486](https://github.com/transmission/transmission/pull/7486))
* When downloading in sequential mode, flush pieces to disk as soon as they're completed and pass their checksum test. This helps apps that are trying to use the data in realtime, e.g. streaming media. ([#7489](https://github.com/transmission/transmission/pull/7489))
* Respect the `min interval` and `interval` keys from any tracker responses. ([#7493](https://github.com/transmission/transmission/pull/7493))
* Announce port-forwarded peer port instead of local peer port on DHT. ([#7511](https://github.com/transmission/transmission/pull/7511))
* Fixed `4.1.0-beta.1` bug where UDP tracker announces were delayed for 10+ seconds on some systems. ([#7634](https://github.com/transmission/transmission/pull/7634))
* Fixed `4.1.0-beta.1` bug where opening the Torrent Properties window while downloading would hang the Qt client. ([#7638](https://github.com/transmission/transmission/pull/7638))
* Fixed `4.1.0-beta.2` bug that could crash when a peer's client name had invalid UTF-8. ([#7667](https://github.com/transmission/transmission/pull/7667))
* Improved libtransmission code to use less CPU. ([#7744](https://github.com/transmission/transmission/pull/7744))
* Deprecated the [RPC](https://github.com/transmission/transmission/blob/main/docs/rpc-spec.md) field `torrent-get.manualAnnounceTime`. ([#7497](https://github.com/transmission/transmission/pull/7497))

### macOS Client

* Reimplemented QuickLook previews for torrent files with Quick Look preview extension API on macOS 12+. ([#7213](https://github.com/transmission/transmission/pull/7213))
* Use modern macOS APIs to prevent idle system sleep and add support for Low Power Mode. ([#7543](https://github.com/transmission/transmission/pull/7543))
* Updated app icon for Liquid Glass. ([#7736](https://github.com/transmission/transmission/pull/7736))

### Qt Client

* Use custom colored progress bars in Qt client for torrent states differentiation. ([#7756](https://github.com/transmission/transmission/pull/7756))

### Web Client

* Added new options for web client to filter torrents by their privacy or error status. ([#6977](https://github.com/transmission/transmission/pull/6977))
* Added accept torrent files in web. ([#7683](https://github.com/transmission/transmission/pull/7683))
* Fixed filtering torrents by tracker after a torrent's tracker list is edited. ([#7761](https://github.com/transmission/transmission/pull/7761))
* Added waiting 1/4 seconds of typing in the search bar before executing and a new button to clear the search. ([#6948](https://github.com/transmission/transmission/pull/6948))
* Updated viewport-sensitive layout and style to uniform across browsers of varying viewport. ([#7328](https://github.com/transmission/transmission/pull/7328))

### Daemon

* Use `Type=notify-reload` in the systemd service file. ([#7570](https://github.com/transmission/transmission/pull/7570))
* The daemon systemd service file now uses the CMake install prefix. ([#7571](https://github.com/transmission/transmission/pull/7571))

### transmission-remote

* Expose the `torrent-get.percentDone` key in transmission-remote. ([#7622](https://github.com/transmission/transmission/pull/7622))

### Everything Else

* Require CMake 3.16.3 or higher to build Transmission. ([#7576](https://github.com/transmission/transmission/pull/7576))
* Configuring Transmission's CMake project no longer inserts third-party submodules to CMake's user package registry. ([#7648](https://github.com/transmission/transmission/pull/7648))
* Sync translations. ([#7768](https://github.com/transmission/transmission/pull/7768))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @flowerey ([flower](https://github.com/flowerey)):
  * Code review. ([#7649](https://github.com/transmission/transmission/pull/7649))
* @G-Ray ([Geoffrey Bonneville](https://github.com/G-Ray)):
  * When downloading in sequential mode, flush pieces to disk as soon as they're completed and pass their checksum test. This helps apps that are trying to use the data in realtime, e.g. streaming media. ([#7489](https://github.com/transmission/transmission/pull/7489))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7489](https://github.com/transmission/transmission/pull/7489), [#7496](https://github.com/transmission/transmission/pull/7496), [#7510](https://github.com/transmission/transmission/pull/7510), [#7525](https://github.com/transmission/transmission/pull/7525), [#7595](https://github.com/transmission/transmission/pull/7595), [#7638](https://github.com/transmission/transmission/pull/7638))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Code review. ([#6981](https://github.com/transmission/transmission/pull/6981), [#7489](https://github.com/transmission/transmission/pull/7489), [#7525](https://github.com/transmission/transmission/pull/7525), [#7580](https://github.com/transmission/transmission/pull/7580), [#7595](https://github.com/transmission/transmission/pull/7595), [#7638](https://github.com/transmission/transmission/pull/7638))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7489](https://github.com/transmission/transmission/pull/7489), [#7667](https://github.com/transmission/transmission/pull/7667), [#7738](https://github.com/transmission/transmission/pull/7738))
  * Feat: use canonical peer priority to decide which peers to keep. ([#6981](https://github.com/transmission/transmission/pull/6981))
  * Refactor: use new `tr_variant` API for `stats.json`. ([#7098](https://github.com/transmission/transmission/pull/7098))
  * Refactor: prioritise peers slots for downloading torrents. ([#7306](https://github.com/transmission/transmission/pull/7306))
  * Added support for using a proxy server for web connections. ([#7486](https://github.com/transmission/transmission/pull/7486))
  * Respect the `min interval` and `interval` keys from any tracker responses. ([#7493](https://github.com/transmission/transmission/pull/7493))
  * Refactor: announcer code housekeeping. ([#7496](https://github.com/transmission/transmission/pull/7496))
  * Deprecated the [RPC](https://github.com/transmission/transmission/blob/main/docs/rpc-spec.md) field `torrent-get.manualAnnounceTime`. ([#7497](https://github.com/transmission/transmission/pull/7497))
  * Feat: allow optional arguments in `tr_getopt()`. ([#7510](https://github.com/transmission/transmission/pull/7510))
  * Announce port-forwarded peer port instead of local peer port on DHT. ([#7511](https://github.com/transmission/transmission/pull/7511))
  * Fix: ignore whitespaces in whitelist. ([#7522](https://github.com/transmission/transmission/pull/7522))
  * Fix: correctly get Windows socket error codes in `get_source_address`. ([#7524](https://github.com/transmission/transmission/pull/7524))
  * Refactor: add more details to logs in ip cache. ([#7525](https://github.com/transmission/transmission/pull/7525))
  * Chore: bump libnatpmp. ([#7536](https://github.com/transmission/transmission/pull/7536))
  * Chore: bump rapidjson. ([#7537](https://github.com/transmission/transmission/pull/7537))
  * Chore: bump googletest. ([#7538](https://github.com/transmission/transmission/pull/7538))
  * Fix: Band-Aid fix for utp speed limit. ([#7541](https://github.com/transmission/transmission/pull/7541))
  * Refactor: removed redundant type check in `tr_ip_cache::set_global_addr()`. ([#7551](https://github.com/transmission/transmission/pull/7551))
  * Chore: bump libpsl. ([#7575](https://github.com/transmission/transmission/pull/7575))
  * Chore: bump dht. ([#7577](https://github.com/transmission/transmission/pull/7577))
  * Chore: bump libevent. ([#7578](https://github.com/transmission/transmission/pull/7578))
  * Fix: process failure response from a non-BEP-7 HTTP announcement. ([#7580](https://github.com/transmission/transmission/pull/7580))
  * Fix: accept either one of udp announce response. ([#7583](https://github.com/transmission/transmission/pull/7583))
  * Fix: load `.torrent` then `.magnet`. ([#7585](https://github.com/transmission/transmission/pull/7585))
  * Chore: silence clang-tidy warnings. ([#7586](https://github.com/transmission/transmission/pull/7586))
  * Fix: remove duplicated mime types and prefer iana source. ([#7590](https://github.com/transmission/transmission/pull/7590))
  * Fixup! chore: move away from fmt/core.h (#7557). ([#7595](https://github.com/transmission/transmission/pull/7595))
  * Chore: bump {fmt} to 11.2.0 and fix compatibility. ([#7612](https://github.com/transmission/transmission/pull/7612))
  * Fix: include wolfssl library configurations header. ([#7632](https://github.com/transmission/transmission/pull/7632))
  * Fixed `4.1.0-beta.1` bug where UDP tracker announces were delayed for 10+ seconds on some systems. ([#7634](https://github.com/transmission/transmission/pull/7634))
  * Fixed `4.1.0-beta.1` bug where opening the Torrent Properties window while downloading would hang the Qt client. ([#7638](https://github.com/transmission/transmission/pull/7638))
  * Fix: FTBFS for libevent <= 2.1.8. ([#7640](https://github.com/transmission/transmission/pull/7640))
  * Chore: implement TR_CONSTEXPR23. ([#7649](https://github.com/transmission/transmission/pull/7649))
  * Build: conditionally compile `utils.mm` using generator expression. ([#7704](https://github.com/transmission/transmission/pull/7704))
  * Fix: update wishlist when files wanted changed. ([#7733](https://github.com/transmission/transmission/pull/7733))
  * Improved libtransmission code to use less CPU. ([#7744](https://github.com/transmission/transmission/pull/7744))
  * Refactor: more generic http announce failure handling. ([#7745](https://github.com/transmission/transmission/pull/7745))
  * Chore: add suffix to private and protected members. ([#7746](https://github.com/transmission/transmission/pull/7746))
  * Perf: reduce memory profile in announce failed handling. ([#7748](https://github.com/transmission/transmission/pull/7748))
* @uranix ([Ivan Tsybulin](https://github.com/uranix)):
  * Fixed `4.1.0-beta.2` bug that could crash when a peer's client name had invalid UTF-8. ([#7667](https://github.com/transmission/transmission/pull/7667))

### Contributions to macOS Client:

* @DevilDimon ([Dmitry Serov](https://github.com/DevilDimon)):
  * Code review. ([#7213](https://github.com/transmission/transmission/pull/7213), [#7543](https://github.com/transmission/transmission/pull/7543))
* @fxcoudert ([FX Coudert](https://github.com/fxcoudert)):
  * Code review. ([#7213](https://github.com/transmission/transmission/pull/7213))
* @MaddTheSane ([C.W. Betts](https://github.com/MaddTheSane)):
  * Code review. ([#7213](https://github.com/transmission/transmission/pull/7213))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Reimplemented QuickLook previews for torrent files with Quick Look preview extension API on macOS 12+. ([#7213](https://github.com/transmission/transmission/pull/7213))
  * Use modern macOS APIs to prevent idle system sleep and add support for Low Power Mode. ([#7543](https://github.com/transmission/transmission/pull/7543))
  * Improved building crc32c target with Xcode. ([#7754](https://github.com/transmission/transmission/pull/7754))

### Contributions to GTK Client:

* @niol:
  * Fixed 4.1.0-beta.2 regression that stopped updating some torrents' progress in the main window. ([#7613](https://github.com/transmission/transmission/pull/7613))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7613](https://github.com/transmission/transmission/pull/7613))
  * Build: define `GDK_VERSION_MIN_REQUIRED`. ([#7633](https://github.com/transmission/transmission/pull/7633))

### Contributions to Web Client:

* @killemov:
  * Code review. ([#6948](https://github.com/transmission/transmission/pull/6948), [#6977](https://github.com/transmission/transmission/pull/6977))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7683](https://github.com/transmission/transmission/pull/7683))
* @Rukario:
  * Code review. ([#7003](https://github.com/transmission/transmission/pull/7003))
  * Added waiting 1/4 seconds of typing in the search bar before executing and a new button to clear the search. ([#6948](https://github.com/transmission/transmission/pull/6948))
  * Added new options for web client to filter torrents by their privacy or error status. ([#6977](https://github.com/transmission/transmission/pull/6977))
  * Introduced submenu for Transmission web's custom context menu to group away long list of context menu items. ([#7263](https://github.com/transmission/transmission/pull/7263))
  * Refactor: offload icons from HTMLdoc to JavaScript for icon deployment. ([#7277](https://github.com/transmission/transmission/pull/7277))
  * Updated viewport-sensitive layout and style to uniform across browsers of varying viewport. ([#7328](https://github.com/transmission/transmission/pull/7328))
  * Tweaked font size for web app. ([#7329](https://github.com/transmission/transmission/pull/7329))
  * Fixed icons displaying incorrectly for multi-file torrents in compact view. ([#7352](https://github.com/transmission/transmission/pull/7352))
  * Refactor: clean up Flexbox style rules in CSS. ([#7358](https://github.com/transmission/transmission/pull/7358))
  * Refactor: centralize checkbox creation in `overflow-menu.js`. ([#7369](https://github.com/transmission/transmission/pull/7369))
  * Updated color for priority buttons in web app. ([#7651](https://github.com/transmission/transmission/pull/7651))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#6977](https://github.com/transmission/transmission/pull/6977), [#7277](https://github.com/transmission/transmission/pull/7277), [#7651](https://github.com/transmission/transmission/pull/7651))
  * Dropped `lodash.isequal` in favour of `fast-deep-equal`. ([#7003](https://github.com/transmission/transmission/pull/7003))
* @wrrrzr:
  * Added accept torrent files in web. ([#7683](https://github.com/transmission/transmission/pull/7683))

### Contributions to Daemon:

* @Managor:
  * Transmission-daemon man page: add missing long options. ([#7559](https://github.com/transmission/transmission/pull/7559))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7570](https://github.com/transmission/transmission/pull/7570))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Use `Type=notify-reload` in the systemd service file. ([#7570](https://github.com/transmission/transmission/pull/7570))
  * The daemon systemd service file now uses the CMake install prefix. ([#7571](https://github.com/transmission/transmission/pull/7571))

### Contributions to transmission-cli:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7557](https://github.com/transmission/transmission/pull/7557))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Chore: move away from fmt/core.h. ([#7557](https://github.com/transmission/transmission/pull/7557))

### Contributions to transmission-remote:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7535](https://github.com/transmission/transmission/pull/7535))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Fix(remote): print label separators correctly. ([#7535](https://github.com/transmission/transmission/pull/7535))
  * Expose the `torrent-get.percentDone` key in transmission-remote. ([#7622](https://github.com/transmission/transmission/pull/7622))
  * Fix(remote): no-sequential-download description. ([#7646](https://github.com/transmission/transmission/pull/7646))

### Contributions to Everything Else:

* @flowerey ([flower](https://github.com/flowerey)):
  * Code review. ([#7627](https://github.com/transmission/transmission/pull/7627))
* @ile6695 ([Ilkka Kallioniemi](https://github.com/ile6695)):
  * Code review. ([#7648](https://github.com/transmission/transmission/pull/7648))
  * Updated documentation. ([#7664](https://github.com/transmission/transmission/pull/7664))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7519](https://github.com/transmission/transmission/pull/7519), [#7526](https://github.com/transmission/transmission/pull/7526), [#7549](https://github.com/transmission/transmission/pull/7549), [#7588](https://github.com/transmission/transmission/pull/7588))
  * Fix: use correct defines when building libpsl with Xcode after roll. ([#7615](https://github.com/transmission/transmission/pull/7615))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Code review. ([#7519](https://github.com/transmission/transmission/pull/7519))
* @Rukario:
  * Fix: also share `grid-area: icon;` across full-compact modes. ([#7356](https://github.com/transmission/transmission/pull/7356))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7615](https://github.com/transmission/transmission/pull/7615))
  * Updated documentation. ([#7232](https://github.com/transmission/transmission/pull/7232), [#7519](https://github.com/transmission/transmission/pull/7519), [#7549](https://github.com/transmission/transmission/pull/7549))
  * Ci: run clang-tidy for tests. ([#7526](https://github.com/transmission/transmission/pull/7526))
  * Build: lint header files with clang-tidy. ([#7527](https://github.com/transmission/transmission/pull/7527))
  * Build: tell clang-tidy to suggest uppercase literal suffixes. ([#7529](https://github.com/transmission/transmission/pull/7529))
  * Require CMake 3.16.3 or higher to build Transmission. ([#7576](https://github.com/transmission/transmission/pull/7576))
  * Chore: move `.gitattributes` to root of repository. ([#7588](https://github.com/transmission/transmission/pull/7588))
  * Ci: use qt6 in ci. ([#7627](https://github.com/transmission/transmission/pull/7627))
  * Configuring Transmission's CMake project no longer inserts third-party submodules to CMake's user package registry. ([#7648](https://github.com/transmission/transmission/pull/7648))
* @uglygus:
  * Google-Glasnost was shut down in 2017. ([#7555](https://github.com/transmission/transmission/pull/7555))

