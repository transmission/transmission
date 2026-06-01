# Transmission 4.1.2

This is Transmission 4.1.2, a bugfix release.
It fixes 20+ bugs and has a few performance improvements too.
All users are encouraged to upgrade to this version.

This progress was possible because of good bug reports and
performance logs reported by users. Thanks, and keep them coming!


## What's New in 4.1.2

### Highlights

* Fixed `4.1.0` bug that could cause duplicate HTTP announces to be sent to trackers. ([#8639](https://github.com/transmission/transmission/pull/8639))

### All Platforms

* Reject benc data that has invalid characters. ([#8577](https://github.com/transmission/transmission/pull/8577))
* Fixed a bug during the startup sequence where if one torrent failed to parse, subsequent torrents would also fail. ([#8605](https://github.com/transmission/transmission/pull/8605))
* Fixed a bug that stalled some downloads at 99%. ([#8654](https://github.com/transmission/transmission/pull/8654))
* Fixed a `4.1.0` upgrade bug that could overwrite `utp_enabled` and `tcp_enabled` settings. ([#8658](https://github.com/transmission/transmission/pull/8658))
* Fixed a `4.1.0` crash that could happen when a peer supplied `reqq` value smaller than 32 in LTEP handshake. ([#8713](https://github.com/transmission/transmission/pull/8713))
* Fixed a `4.1.0` regression that periodically wrote upload & download stats to disk even when Transmission had been idle since the last write, preventing the stats file's disk from hibernating while idle. ([#8722](https://github.com/transmission/transmission/pull/8722))
* Fixed a `4.1.0` bug that prevented TCP peer connections on some systems. ([#8748](https://github.com/transmission/transmission/pull/8748))
* Added safeguards to HTTP responses to prevent clickjacking. ([#8749](https://github.com/transmission/transmission/pull/8749))
* Fixed edge case that didn't preserve the order of a batch of torrents when moving their queue position up or down. ([#8782](https://github.com/transmission/transmission/pull/8782))
* Added sanitization for UTF-8 client names provided by peers during handshake. ([#8809](https://github.com/transmission/transmission/pull/8809))
* Stopped appending redundant zeros to blocklist files when downloaded from a remote URL. ([#8819](https://github.com/transmission/transmission/pull/8819))
* Fixed a build failure that occurred when building with link-time optimization. ([#8540](https://github.com/transmission/transmission/pull/8540))

### macOS Client

* Fixed a `4.1.0` memory leak. ([#8613](https://github.com/transmission/transmission/pull/8613))
* Fixed navigation focus issues in the Inspector. ([#8792](https://github.com/transmission/transmission/pull/8792), [#8810](https://github.com/transmission/transmission/pull/8810))
* Improved UI code to use less CPU. ([#8832](https://github.com/transmission/transmission/pull/8832), [#8833](https://github.com/transmission/transmission/pull/8833), [#8835](https://github.com/transmission/transmission/pull/8835), [#8836](https://github.com/transmission/transmission/pull/8836), [#8842](https://github.com/transmission/transmission/pull/8842), [#8846](https://github.com/transmission/transmission/pull/8846), [#8851](https://github.com/transmission/transmission/pull/8851))

### Qt Client

* Fixed a `4.1.0` crash when parsing some RPC responses from older Transmission servers. ([#8618](https://github.com/transmission/transmission/pull/8618))
* Fixed a `4.1.0` bug that saved both deprecated and current settings names to `settings.json`. ([#8623](https://github.com/transmission/transmission/pull/8623))

### GTK Client

* Fixed a `4.1.0` bug that did not show translated logging level strings. ([#8611](https://github.com/transmission/transmission/pull/8611))
* Fixed a `4.1.0` crash when toggling alternative speed limits. ([#8709](https://github.com/transmission/transmission/pull/8709))

### Web Client

* Fixed a `4.1.0` bug that displayed timestamps in some dropdowns as `6.75:45` instead of `6:45`. ([#8624](https://github.com/transmission/transmission/pull/8624))
* Fixed a bug that could show incorrect torrent status when reconnecting to the server after a lost connection. ([#8780](https://github.com/transmission/transmission/pull/8780), [#8783](https://github.com/transmission/transmission/pull/8783))

### transmission-remote

* Improved `transmission-remote` console output for JSON-RPC 2. ([#8799](https://github.com/transmission/transmission/pull/8799), [#8805](https://github.com/transmission/transmission/pull/8805))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @lpla ([Leopoldo Pla Sempere](https://github.com/lpla)):
  * Daemon: avoid periodic stats.json rewrites while idle. ([#8679](https://github.com/transmission/transmission/pull/8679))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Added sanitization for UTF-8 client names provided by peers during handshake. ([#8809](https://github.com/transmission/transmission/pull/8809))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#8679](https://github.com/transmission/transmission/pull/8679), [#8809](https://github.com/transmission/transmission/pull/8809))
  * Fixed a build failure that occurred when building with link-time optimization. ([#8540](https://github.com/transmission/transmission/pull/8540))
  * Reject benc data that has invalid characters. ([#8577](https://github.com/transmission/transmission/pull/8577))
  * Fixed a bug during the startup sequence where if one torrent failed to parse, subsequent torrents would also fail. ([#8605](https://github.com/transmission/transmission/pull/8605))
  * Fixed `4.1.0` bug that could cause duplicate HTTP announces to be sent to trackers. ([#8639](https://github.com/transmission/transmission/pull/8639))
  * Fixed a bug that stalled some downloads at 99%. ([#8654](https://github.com/transmission/transmission/pull/8654))
  * Fixed a `4.1.0` upgrade bug that could overwrite `utp_enabled` and `tcp_enabled` settings. ([#8658](https://github.com/transmission/transmission/pull/8658))
  * Fixed a `4.1.0` crash that could happen when a peer supplied `reqq` value smaller than 32 in LTEP handshake. ([#8713](https://github.com/transmission/transmission/pull/8713))
  * Fixed a `4.1.0` bug that prevented TCP peer connections on some systems. ([#8748](https://github.com/transmission/transmission/pull/8748))
  * Fixed edge case that didn't preserve the order of a batch of torrents when moving their queue position up or down. ([#8782](https://github.com/transmission/transmission/pull/8782))
  * Fix: don't use int when calculating number of blocklist rules. ([#8816](https://github.com/transmission/transmission/pull/8816))

### Contributions to macOS Client:

* @Abdull0100 ([Abdullah Tahir](https://github.com/Abdull0100)):
  * Fixed navigation focus issues in the Inspector. ([#8792](https://github.com/transmission/transmission/pull/8792))
* @lolgear ([Dmitry Lobanov](https://github.com/lolgear)):
  * Cache date formatter in tracker node. ([#8849](https://github.com/transmission/transmission/pull/8849))

### Contributions to Qt Client:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#8653](https://github.com/transmission/transmission/pull/8653))

### Contributions to GTK Client:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#8709](https://github.com/transmission/transmission/pull/8709))
  * Fixed `4.1.0` crash when toggling alternative speed limit on the GTK app. ([#8703](https://github.com/transmission/transmission/pull/8703))

### Contributions to Web Client:

* @aeriuskiller ([Gonçalo Marcelo](https://github.com/aeriuskiller)):
  * Fix: initialized torrent list after web reconnect. ([#8733](https://github.com/transmission/transmission/pull/8733))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#8733](https://github.com/transmission/transmission/pull/8733))
  * Fixed a bug that could show incorrect torrent status when reconnecting to the server after a lost connection. ([#8783](https://github.com/transmission/transmission/pull/8783))

### Contributions to transmission-remote:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Fixed #8122: right-align Down and Up headers in -pi peer output. ([#8784](https://github.com/transmission/transmission/pull/8784))
  * Improved `transmission-remote` console output for JSON-RPC 2. ([#8799](https://github.com/transmission/transmission/pull/8799))

### Contributions to Everything Else:

* @jaythomas ([Jay Thomas](https://github.com/jaythomas)):
  * Docs(rpc-spec): correct bandwidth group name field. ([#8840](https://github.com/transmission/transmission/pull/8840))

