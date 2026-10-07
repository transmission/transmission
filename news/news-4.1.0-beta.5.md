# Transmission 4.1.0-beta.5

This is Transmission 4.1.0-beta.5. We're in feature freeze
for 4.1.0 -- testers and feedback are encouraged!


## What's New in 4.1.0-beta.5

### Highlights

* Fixed `4.1.0-beta.1` bug where some downloads could get stuck for a long time. ([#7899](https://github.com/transmission/transmission/pull/7899))
* Fixed `4.1.0-beta.4` bug that broke icons on Windows. ([#7931](https://github.com/transmission/transmission/pull/7931))
* Fixed `4.1.0-beta.4` issue that included backwards-incompatible keys in the settings file. ([#7917](https://github.com/transmission/transmission/pull/7917), [#7918](https://github.com/transmission/transmission/pull/7918), [#7932](https://github.com/transmission/transmission/pull/7932), [#7995](https://github.com/transmission/transmission/pull/7995), [#8002](https://github.com/transmission/transmission/pull/8002))
* Return `X-Transmission-Rpc-Version` header in RPC HTTP 409 response to indicate JSON-RPC support. ([#7958](https://github.com/transmission/transmission/pull/7958))

### All Platforms

* Fixed `4.1.0-beta.1` bug where torrents with unaligned piece and block boundaries could get stuck at 99%. ([#7944](https://github.com/transmission/transmission/pull/7944))
* Fixed intermittent crashes on macOS and GTK app. ([#7948](https://github.com/transmission/transmission/pull/7948))
* Reject incoming BT data if they are not selected for download. ([#7866](https://github.com/transmission/transmission/pull/7866))
* Fixed minor 4.1.0 beta regression to honor the `sleep_per_seconds_during_verify` setting when verifying local data. ([#7870](https://github.com/transmission/transmission/pull/7870))
* Fixed remote RPC bug where querying `recently_active` torrents missed some torrents. ([#8029](https://github.com/transmission/transmission/pull/8029))
* `torrent_get.wanted` is now an array of booleans in the JSON-RPC API. ([#7997](https://github.com/transmission/transmission/pull/7997))
* Encryption mode in `settings.json` and RPC are now serialized to the same set of strings. ([#8032](https://github.com/transmission/transmission/pull/8032))
* Renamed setting to `cache_size_mib` to reflect the correct size units. ([#7971](https://github.com/transmission/transmission/pull/7971))
* Renamed `peer_socket_tos` to `peer_socket_diffserv`. ([#8004](https://github.com/transmission/transmission/pull/8004))
* Deprecated `session_get.rpc_version` and `session_get.rpc_version_minimum` in favour of `session_get.rpc_version_semver` in RPC. ([#8022](https://github.com/transmission/transmission/pull/8022))

### macOS Client

* Fixed crash when opening the messages log. ([#8035](https://github.com/transmission/transmission/pull/8035))
* Fixed re-opening the filter bar is showing an incorrect selected filter. ([#7844](https://github.com/transmission/transmission/pull/7844))
* Removed menu icons on older Macs. ([#7994](https://github.com/transmission/transmission/pull/7994))

### Qt Client

* Fixed build script bug that could cause extra instances of Transmission to launch on Windows. ([#7841](https://github.com/transmission/transmission/pull/7841))
* Fixed "sequence not ordered" assertion error in debug builds. ([#8000](https://github.com/transmission/transmission/pull/8000))
* Fixed a Qt API deprecation warning when building with Qt >= 6.13. ([#7940](https://github.com/transmission/transmission/pull/7940))
* Raised the minimum Qt5 version to 5.15. ([#7943](https://github.com/transmission/transmission/pull/7943))

### Daemon

* Deprecated `tcp-enabled` and `udp-enabled` in favour of `preferred_transports`. ([#7988](https://github.com/transmission/transmission/pull/7988))

### transmission-remote

* `transmission-remote --blocklist-update` now prints blocklist size after update. ([#8021](https://github.com/transmission/transmission/pull/8021))
* Fixed layout bug that caused columns to be misaligned when transfer speed  was >= 10MB. ([#8019](https://github.com/transmission/transmission/pull/8019))
* Deprecated `--(no-)utp` in transmission-remote. ([#7990](https://github.com/transmission/transmission/pull/7990))

### Everything Else

* Updated documentation. ([#8039](https://github.com/transmission/transmission/pull/8039))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#7917](https://github.com/transmission/transmission/pull/7917))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Code review. ([#7866](https://github.com/transmission/transmission/pull/7866))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7877](https://github.com/transmission/transmission/pull/7877), [#7883](https://github.com/transmission/transmission/pull/7883), [#7892](https://github.com/transmission/transmission/pull/7892), [#7910](https://github.com/transmission/transmission/pull/7910), [#7917](https://github.com/transmission/transmission/pull/7917), [#7918](https://github.com/transmission/transmission/pull/7918), [#7923](https://github.com/transmission/transmission/pull/7923), [#7932](https://github.com/transmission/transmission/pull/7932), [#7953](https://github.com/transmission/transmission/pull/7953), [#7954](https://github.com/transmission/transmission/pull/7954), [#7956](https://github.com/transmission/transmission/pull/7956), [#7961](https://github.com/transmission/transmission/pull/7961), [#7985](https://github.com/transmission/transmission/pull/7985), [#8012](https://github.com/transmission/transmission/pull/8012))
  * Reject incoming BT data if they are not selected for download. ([#7866](https://github.com/transmission/transmission/pull/7866))
  * Fixed minor 4.1.0 beta regression to honor the `sleep_per_seconds_during_verify` setting when verifying local data. ([#7870](https://github.com/transmission/transmission/pull/7870))
  * Fix: FTBFS in C++20. ([#7880](https://github.com/transmission/transmission/pull/7880))
  * Perf: use iterator range insert in `Wishlist::next`. ([#7890](https://github.com/transmission/transmission/pull/7890))
  * Fixed `4.1.0-beta.1` bug where some downloads could get stuck for a long time. ([#7899](https://github.com/transmission/transmission/pull/7899))
  * Fix: only shrink wishlist block span if previous piece is wanted. ([#7900](https://github.com/transmission/transmission/pull/7900))
  * Fix: FTBFS with rapidjson 1.1.0 release on GCC < 12. ([#7933](https://github.com/transmission/transmission/pull/7933))
  * Fixed `4.1.0-beta.1` bug where torrents with unaligned piece and block boundaries could get stuck at 99%. ([#7944](https://github.com/transmission/transmission/pull/7944))
  * Fix: add client_has_piece check to recalculate_wanted_pieces. ([#7945](https://github.com/transmission/transmission/pull/7945))
  * Refactor: re-use `recalculate_wanted_pieces` logic in wishlist constructor. ([#7946](https://github.com/transmission/transmission/pull/7946))
  * Fixed intermittent crashes on macOS and GTK app. ([#7948](https://github.com/transmission/transmission/pull/7948))
  * Refactor: use `api-compat` for `settings.json`. ([#7950](https://github.com/transmission/transmission/pull/7950))
  * Return `X-Transmission-Rpc-Version` header in RPC HTTP 409 response to indicate JSON-RPC support. ([#7958](https://github.com/transmission/transmission/pull/7958))
  * Renamed setting to `cache_size_mib` to reflect the correct size units. ([#7971](https://github.com/transmission/transmission/pull/7971))
  * Refactor: use `apicompat` for `bandwidth-groups.json`. ([#7972](https://github.com/transmission/transmission/pull/7972))
  * Feat: generic support for optional fields in serializer. ([#7979](https://github.com/transmission/transmission/pull/7979))
  * Feat: sync the values of `preferred_transports` and `*_enabled`. ([#7980](https://github.com/transmission/transmission/pull/7980))
  * Fixed `4.1.0-beta.4` issue that included backwards-incompatible keys in the settings file. ([#7995](https://github.com/transmission/transmission/pull/7995))
  * `torrent_get.wanted` is now an array of booleans in the JSON-RPC API. ([#7997](https://github.com/transmission/transmission/pull/7997))
  * Renamed `peer_socket_tos` to `peer_socket_diffserv`. ([#8004](https://github.com/transmission/transmission/pull/8004))
  * Fix: rpcimpl array size. ([#8014](https://github.com/transmission/transmission/pull/8014))
  * Fix: detect `is_torrent` in rpc response too. ([#8015](https://github.com/transmission/transmission/pull/8015))
  * Fix: discard any non-int tag when converting to tr4. ([#8017](https://github.com/transmission/transmission/pull/8017))
  * Fix: dedupe `Access-Control-Expose-Headers` header. ([#8018](https://github.com/transmission/transmission/pull/8018))
  * Deprecated `session_get.rpc_version` and `session_get.rpc_version_minimum` in favour of `session_get.rpc_version_semver` in RPC. ([#8022](https://github.com/transmission/transmission/pull/8022))
  * Fixed remote RPC bug where querying `recently_active` torrents missed some torrents. ([#8029](https://github.com/transmission/transmission/pull/8029))
  * Encryption mode in `settings.json` and RPC are now serialized to the same set of strings. ([#8032](https://github.com/transmission/transmission/pull/8032))
  * Fix: remove block from wishlist when received. ([#8042](https://github.com/transmission/transmission/pull/8042))
  * Fix: optional serializer edge cases. ([#8044](https://github.com/transmission/transmission/pull/8044))
  * Fix: wishlist edge case when got bad piece in unaligned torrents. ([#8047](https://github.com/transmission/transmission/pull/8047))
  * Fix: unconst variable to be moved. ([#8048](https://github.com/transmission/transmission/pull/8048))

### Contributions to macOS Client:

* @beyondcompute ([Evgeny Kulikov](https://github.com/beyondcompute)):
  * Fixed re-opening the filter bar is showing an incorrect selected filter. ([#7844](https://github.com/transmission/transmission/pull/7844))
* @NickWick13 ([Nick](https://github.com/NickWick13)):
  * Copyright year typo. ([#8058](https://github.com/transmission/transmission/pull/8058))

### Contributions to Qt Client:

* @sanapci ([Elek, David](https://github.com/sanapci)):
  * Fixed build script bug that could cause extra instances of Transmission to launch on Windows. ([#7841](https://github.com/transmission/transmission/pull/7841))
  * Fixed "sequence not ordered" assertion error in debug builds. ([#8000](https://github.com/transmission/transmission/pull/8000))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7909](https://github.com/transmission/transmission/pull/7909), [#7959](https://github.com/transmission/transmission/pull/7959), [#7963](https://github.com/transmission/transmission/pull/7963), [#7987](https://github.com/transmission/transmission/pull/7987))
  * Refactor(qt): use enum values to define encryption dropdown. ([#8034](https://github.com/transmission/transmission/pull/8034))

### Contributions to Daemon:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Deprecated `tcp-enabled` and `udp-enabled` in favour of `preferred_transports`. ([#7988](https://github.com/transmission/transmission/pull/7988))
  * Fix(daemon): missing break statement in switch. ([#7991](https://github.com/transmission/transmission/pull/7991))

### Contributions to transmission-remote:

* @sanapci ([Elek, David](https://github.com/sanapci)):
  * Fixed layout bug that caused columns to be misaligned when transfer speed  was >= 10MB. ([#8019](https://github.com/transmission/transmission/pull/8019))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7986](https://github.com/transmission/transmission/pull/7986), [#8019](https://github.com/transmission/transmission/pull/8019))
  * Deprecated `--(no-)utp` in transmission-remote. ([#7990](https://github.com/transmission/transmission/pull/7990))
  * `transmission-remote --blocklist-update` now prints blocklist size after update. ([#8021](https://github.com/transmission/transmission/pull/8021))
  * Feat(remote): console message for HTTP 204 response. ([#8027](https://github.com/transmission/transmission/pull/8027))
  * Fix(remote): attach id to methods that can return errors. ([#8028](https://github.com/transmission/transmission/pull/8028))

### Contributions to Everything Else:

* @ChaseKnowlden ([Chase Knowlden](https://github.com/ChaseKnowlden)):
  * Fix: build with Visual Studio 2026. ([#7924](https://github.com/transmission/transmission/pull/7924))
* @namoen0301:
  * GHA CI: Add support for windows arm64. ([#7758](https://github.com/transmission/transmission/pull/7758))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Ci: set ENABLE_TESTS to make-tests on alpine. ([#7914](https://github.com/transmission/transmission/pull/7914))
  * Docs: update `settings.json` docs for api-compat. ([#7955](https://github.com/transmission/transmission/pull/7955))
  * Test: variant constructor and assignment operator. ([#7957](https://github.com/transmission/transmission/pull/7957))
  * Docs(rpc): add missing changelog for recent commits. ([#8031](https://github.com/transmission/transmission/pull/8031))
  * Docs: deprecate `session_get.tcp_enabled`. ([#8036](https://github.com/transmission/transmission/pull/8036))

