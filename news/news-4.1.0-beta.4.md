# Transmission 4.1.0-beta.4

This is Transmission 4.1.0-beta.4. We're not in feature freeze yet,
so this release includes some new features as well as bugfixes and
performance improvements.


## What's New in 4.1.0-beta.4

### Highlights

* Unify RPC and `settings.json` strings to snake_case, backwards compatible. ([#7108](https://github.com/transmission/transmission/pull/7108))
* New [JSON-RPC 2.0](https://www.jsonrpc.org/specification)-compliant RPC API. ([#7269](https://github.com/transmission/transmission/pull/7269))
* Use native icons for menus and toolbars: SF Symbols on macOS, Segoe Fluent on Windows 11, Segoe MDL2 on Windows 10, and XDG standard icon names everywhere else. ([#7819](https://github.com/transmission/transmission/pull/7819), Qt Client)

### All Platforms

* Added an option to verify a torrent immediately after it finishes downloading. ([#4178](https://github.com/transmission/transmission/pull/4178))
* Added BEP-21 downloader count to `tr_tracker_view` and RPC. ([#6936](https://github.com/transmission/transmission/pull/6936))
* Added peer traffic statistics to `torrent-get` rpc method. ([#7172](https://github.com/transmission/transmission/pull/7172))
* Added bytesCompleted field to torrent-get rpc call. ([#7173](https://github.com/transmission/transmission/pull/7173))
* Deprecate `tcp-enabled` and `udp-enabled` in favour of `preferred_transports`. ([#7473](https://github.com/transmission/transmission/pull/7473))
* Added raw PeerID to RPC interface. ([#7514](https://github.com/transmission/transmission/pull/7514))
* IPv4 patterns in the RPC whitelist can now match with IPv4-mapped IPv6 addresses. ([#7523](https://github.com/transmission/transmission/pull/7523))
* Improved libtransmission code to use less CPU. ([#7800](https://github.com/transmission/transmission/pull/7800))
* Support dual stack by manually creating and binding socket on Windows platform. ([#6548](https://github.com/transmission/transmission/pull/6548))
* Generate imported targets for MbedTLS. ([#7631](https://github.com/transmission/transmission/pull/7631))
* Added support for libevent 2.2.1-alpha-dev. ([#7765](https://github.com/transmission/transmission/pull/7765))
* Bumped [miniupnpc](http://miniupnp.free.fr/) from 2.2.8 to 2.3.3. ([#7783](https://github.com/transmission/transmission/pull/7783))
* Bumped [`{fmt}`](https://github.com/fmtlib/fmt) to 12.1.0. ([#7793](https://github.com/transmission/transmission/pull/7793))

### macOS Client

* Fixed missing tooltips for Group rows in Torrent Table View. ([#7828](https://github.com/transmission/transmission/pull/7828))

### Qt Client

* Added the ability to use a custom URL path when connecting to remote Transmission servers. ([#7561](https://github.com/transmission/transmission/pull/7561))

### Web Client

* Implemented a context menu for file list in web app making way to rename or copy name of individual file. ([#7389](https://github.com/transmission/transmission/pull/7389))
* Updated turtle for web app. ([#6940](https://github.com/transmission/transmission/pull/6940))
* Added checkbox to delete data while removing torrents. ([#7000](https://github.com/transmission/transmission/pull/7000))
* Gave labels to the mainwin buttons for web client. ([#6985](https://github.com/transmission/transmission/pull/6985))

### transmission-remote

* Added support to download sequentially from a specific piece. This can enable apps to seek within media files for streaming use cases. ([#7808](https://github.com/transmission/transmission/pull/7808), [#7809](https://github.com/transmission/transmission/pull/7809))
* Fixed `4.1.0-beta.1` issue that displayed incorrect speeds when using `transmission-remote -pi`. ([#7796](https://github.com/transmission/transmission/pull/7796))

### Everything Else

* Apply Xcode 26.0 recommendations. ([#7823](https://github.com/transmission/transmission/pull/7823))
* Updated documentation. ([#7826](https://github.com/transmission/transmission/pull/7826), [#7829](https://github.com/transmission/transmission/pull/7829), [#7830](https://github.com/transmission/transmission/pull/7830), [#7836](https://github.com/transmission/transmission/pull/7836), [#7840](https://github.com/transmission/transmission/pull/7840))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @cdowen:
  * Added peer traffic statistics to `torrent-get` rpc method. ([#7172](https://github.com/transmission/transmission/pull/7172))
  * Added bytesCompleted field to torrent-get rpc call. ([#7173](https://github.com/transmission/transmission/pull/7173))
  * Added raw PeerID to RPC interface. ([#7514](https://github.com/transmission/transmission/pull/7514))
* @G-Ray ([Geoffrey Bonneville](https://github.com/G-Ray)):
  * Feat: allow downloading sequentially from a specific piece. ([#7502](https://github.com/transmission/transmission/pull/7502))
* @Ghost-chu ([Ghost_chu](https://github.com/Ghost-chu)):
  * Code review. ([#7172](https://github.com/transmission/transmission/pull/7172))
* @github-advanced-security[bot]:
  * Code review. ([#7514](https://github.com/transmission/transmission/pull/7514))
* @herbyuan:
  * Support dual stack by manually creating and binding socket on Windows platform. ([#6548](https://github.com/transmission/transmission/pull/6548))
* @killemov:
  * Code review. ([#7269](https://github.com/transmission/transmission/pull/7269))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#4178](https://github.com/transmission/transmission/pull/4178), [#7520](https://github.com/transmission/transmission/pull/7520), [#7802](https://github.com/transmission/transmission/pull/7802), [#7810](https://github.com/transmission/transmission/pull/7810))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Code review. ([#6936](https://github.com/transmission/transmission/pull/6936), [#7520](https://github.com/transmission/transmission/pull/7520))
  * Upload_only fixes, check for partial seeds. ([#7785](https://github.com/transmission/transmission/pull/7785))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#4178](https://github.com/transmission/transmission/pull/4178), [#6548](https://github.com/transmission/transmission/pull/6548), [#7172](https://github.com/transmission/transmission/pull/7172), [#7173](https://github.com/transmission/transmission/pull/7173), [#7502](https://github.com/transmission/transmission/pull/7502), [#7514](https://github.com/transmission/transmission/pull/7514), [#7743](https://github.com/transmission/transmission/pull/7743), [#7785](https://github.com/transmission/transmission/pull/7785), [#7797](https://github.com/transmission/transmission/pull/7797), [#7799](https://github.com/transmission/transmission/pull/7799), [#7800](https://github.com/transmission/transmission/pull/7800))
  * Added BEP-21 downloader count to `tr_tracker_view` and RPC. ([#6936](https://github.com/transmission/transmission/pull/6936))
  * Unify RPC and `settings.json` strings to snake_case, backwards compatible. ([#7108](https://github.com/transmission/transmission/pull/7108))
  * New [JSON-RPC 2.0](https://www.jsonrpc.org/specification)-compliant RPC API. ([#7269](https://github.com/transmission/transmission/pull/7269))
  * Deprecate `tcp-enabled` and `udp-enabled` in favour of `preferred_transports`. ([#7473](https://github.com/transmission/transmission/pull/7473))
  * Fix: caching a source address doesn't imply public internet connectivity. ([#7520](https://github.com/transmission/transmission/pull/7520))
  * IPv4 patterns in the RPC whitelist can now match with IPv4-mapped IPv6 addresses. ([#7523](https://github.com/transmission/transmission/pull/7523))
  * Fix: don't bind udp sockets if no OS support. ([#7552](https://github.com/transmission/transmission/pull/7552))
  * Refactor: replace all `evutil_make_listen_socket_ipv6only` usages. ([#7779](https://github.com/transmission/transmission/pull/7779))
  * Refactor: torrent complete verify cleanup. ([#7802](https://github.com/transmission/transmission/pull/7802))
  * Refactor: overhaul `tr_address` special address checks. ([#7818](https://github.com/transmission/transmission/pull/7818))
  * Refactor: store BT peers with `std::shared_ptr`. ([#7837](https://github.com/transmission/transmission/pull/7837))
  * Perf: short circuit reset_blocks_bitfield(). ([#7838](https://github.com/transmission/transmission/pull/7838))
* @winterheart ([Azamat H. Hackimov](https://github.com/winterheart)):
  * Generate imported targets for MbedTLS. ([#7631](https://github.com/transmission/transmission/pull/7631))

### Contributions to macOS Client:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7762](https://github.com/transmission/transmission/pull/7762), [#7763](https://github.com/transmission/transmission/pull/7763), [#7787](https://github.com/transmission/transmission/pull/7787), [#7792](https://github.com/transmission/transmission/pull/7792))
  * Macos: Convert FileOutlineView to View-based table. ([#7760](https://github.com/transmission/transmission/pull/7760))
  * Try out xcbeautify and fix missing artifacts. ([#7824](https://github.com/transmission/transmission/pull/7824))
  * Fixed missing tooltips for Group rows in Torrent Table View. ([#7828](https://github.com/transmission/transmission/pull/7828))
  * Updated macos actions. ([#7845](https://github.com/transmission/transmission/pull/7845))
* @sweetppro ([SweetPPro](https://github.com/sweetppro)):
  * Code review. ([#7763](https://github.com/transmission/transmission/pull/7763), [#7828](https://github.com/transmission/transmission/pull/7828))

### Contributions to Qt Client:

* @htmltiger:
  * Added the ability to use a custom URL path when connecting to remote Transmission servers. ([#7561](https://github.com/transmission/transmission/pull/7561))

### Contributions to Web Client:

* @killemov:
  * Code review. ([#7389](https://github.com/transmission/transmission/pull/7389))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7389](https://github.com/transmission/transmission/pull/7389))
* @Rukario:
  * Updated turtle for web app. ([#6940](https://github.com/transmission/transmission/pull/6940))
  * Gave labels to the mainwin buttons for web client. ([#6985](https://github.com/transmission/transmission/pull/6985))
  * Added checkbox to delete data while removing torrents. ([#7000](https://github.com/transmission/transmission/pull/7000))
  * Implemented a context menu for file list in web app making way to rename or copy name of individual file. ([#7389](https://github.com/transmission/transmission/pull/7389))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7000](https://github.com/transmission/transmission/pull/7000))

### Contributions to Daemon:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Refactor: use new `tr_variant` API in daemon. ([#7103](https://github.com/transmission/transmission/pull/7103))

### Contributions to transmission-remote:

* @sanapci ([Elek, David](https://github.com/sanapci)):
  * Fixed `4.1.0-beta.1` issue that displayed incorrect speeds when using `transmission-remote -pi`. ([#7796](https://github.com/transmission/transmission/pull/7796))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Added support to download sequentially from a specific piece. This can enable apps to seek within media files for streaming use cases. ([#7808](https://github.com/transmission/transmission/pull/7808), [#7809](https://github.com/transmission/transmission/pull/7809))

### Contributions to Everything Else:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Apply Xcode 26.0 recommendations. ([#7823](https://github.com/transmission/transmission/pull/7823))
  * Fixed unreachable in tr_make_listen_socket_ipv6only. ([#7825](https://github.com/transmission/transmission/pull/7825))
  * Updated documentation. ([#7826](https://github.com/transmission/transmission/pull/7826), [#7829](https://github.com/transmission/transmission/pull/7829), [#7830](https://github.com/transmission/transmission/pull/7830), [#7840](https://github.com/transmission/transmission/pull/7840))
* @niol:
  * Search both `fmt/base.h` (fmt 11+) and `fmt/core.h` (fmt < 11) for `FMT_VERSION`. ([#7772](https://github.com/transmission/transmission/pull/7772))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7825](https://github.com/transmission/transmission/pull/7825))
  * Build: bump clang tools to 20. ([#7573](https://github.com/transmission/transmission/pull/7573))
  * Fix: libb64 submodule branch. ([#7855](https://github.com/transmission/transmission/pull/7855))
  * Ci: disable fail-fast for new macOS runners. ([#7857](https://github.com/transmission/transmission/pull/7857))

