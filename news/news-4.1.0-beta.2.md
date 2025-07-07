# Transmission 4.1.0-beta.2

This is Transmission 4.1.0-beta.2. We're not in feature freeze yet,
so this release includes some new features as well as bugfixes and
performance improvements.


## What's New in 4.1.0-beta.2

### Highlights

* Support trackers that only support the old BEP-7 with `&ipv4=` and `&ipv6=`. ([#7481](https://github.com/transmission/transmission/pull/7481))

### All Platforms

* Added support for using a proxy server for web connections. ([#5038](https://github.com/transmission/transmission/pull/5038))
* Added optional sequential downloading. ([#6893](https://github.com/transmission/transmission/pull/6893), [#7047](https://github.com/transmission/transmission/pull/7047))
* Disconnect blocklisted peers immediately upon blocklist update. ([#7167](https://github.com/transmission/transmission/pull/7167))
* New files are assigned a file mode per the process _umask_ defined in `settings.json`. ([#7195](https://github.com/transmission/transmission/pull/7195))
* Harden the HTTP tracker response parser. ([#7326](https://github.com/transmission/transmission/pull/7326))
* Fixed an issue where the speed limits are not effective below 16KiB/s. ([#7339](https://github.com/transmission/transmission/pull/7339))
* Added workaround for crashes related to [Curl bug 10936](https://github.com/curl/curl/issues/10936). ([#7416](https://github.com/transmission/transmission/pull/7416))
* Added a workaround for users affected by [Curl bug 6312](https://github.com/curl/curl/issues/6312). ([#7447](https://github.com/transmission/transmission/pull/7447))
* Better utilize high Internet bandwidth. ([#7029](https://github.com/transmission/transmission/pull/7029))
* Save upload/download queue order between sessions. ([#7332](https://github.com/transmission/transmission/pull/7332))

### macOS Client

* Fixed the context menu's appearance in compact mode. ([#7350](https://github.com/transmission/transmission/pull/7350))
* Added Afrikaans and Greek translations. ([#7477](https://github.com/transmission/transmission/pull/7477))

### GTK Client

* Fixing a bug in adding torrent in GTK application. ([#7247](https://github.com/transmission/transmission/pull/7247))

### Web Client

* Added a new alert message of a problem when renaming torrent or file name. ([#7394](https://github.com/transmission/transmission/pull/7394))
* Fixed a bug inflating per-torrent rows by long torrent names in compact view. ([#7336](https://github.com/transmission/transmission/pull/7336))
* Fixed incorrect text entry sensitivity when sessions changed. ([#7346](https://github.com/transmission/transmission/pull/7346))
* Added column mode for viewport unconstrained browsers. ([#7051](https://github.com/transmission/transmission/pull/7051))
* Fixed an issue where Transmission web's custom context menu does not close when clicking on some outside element. ([#7296](https://github.com/transmission/transmission/pull/7296))
* Implemented a new popup management system for web client to support multiple popups in a hierarchy-like system. ([#7297](https://github.com/transmission/transmission/pull/7297))

### Daemon

* Added optional sequential downloading. ([#7048](https://github.com/transmission/transmission/pull/7048))

### transmission-remote

* Improved error logging. ([#7034](https://github.com/transmission/transmission/pull/7034))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to All Platforms:

* @cdowen:
  * Disconnect blocklisted peers immediately upon blocklist update. ([#7167](https://github.com/transmission/transmission/pull/7167))
* @ile6695 ([Ilkka Kallioniemi](https://github.com/ile6695)):
  * Code review. ([#7457](https://github.com/transmission/transmission/pull/7457))
* @jggimi ([Josh Grosse](https://github.com/jggimi)):
  * New files are assigned a file mode per the process _umask_ defined in `settings.json`. ([#7195](https://github.com/transmission/transmission/pull/7195))
* @killemov:
  * Code review. ([#7047](https://github.com/transmission/transmission/pull/7047))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Code review. ([#5038](https://github.com/transmission/transmission/pull/5038), [#7195](https://github.com/transmission/transmission/pull/7195), [#7383](https://github.com/transmission/transmission/pull/7383))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Code review. ([#6892](https://github.com/transmission/transmission/pull/6892), [#7167](https://github.com/transmission/transmission/pull/7167), [#7177](https://github.com/transmission/transmission/pull/7177), [#7195](https://github.com/transmission/transmission/pull/7195), [#7355](https://github.com/transmission/transmission/pull/7355))
  * Handshake: add fire_timer() explicitly instead of overloading fire_done(). ([#6966](https://github.com/transmission/transmission/pull/6966))
  * Consume early pad a/b, improve handshake tests. ([#6987](https://github.com/transmission/transmission/pull/6987))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#5038](https://github.com/transmission/transmission/pull/5038), [#6966](https://github.com/transmission/transmission/pull/6966), [#6987](https://github.com/transmission/transmission/pull/6987), [#7167](https://github.com/transmission/transmission/pull/7167), [#7195](https://github.com/transmission/transmission/pull/7195), [#7313](https://github.com/transmission/transmission/pull/7313), [#7447](https://github.com/transmission/transmission/pull/7447), [#7461](https://github.com/transmission/transmission/pull/7461), [#7462](https://github.com/transmission/transmission/pull/7462), [#7469](https://github.com/transmission/transmission/pull/7469), [#7470](https://github.com/transmission/transmission/pull/7470), [#7471](https://github.com/transmission/transmission/pull/7471))
  * Refactor: store peers as benc in resume file. ([#6892](https://github.com/transmission/transmission/pull/6892))
  * Added optional sequential downloading. ([#6893](https://github.com/transmission/transmission/pull/6893), [#7047](https://github.com/transmission/transmission/pull/7047))
  * Fix: abort handshake if the torrent is stopped. ([#6947](https://github.com/transmission/transmission/pull/6947))
  * Refactor: save outgoing `len(PadA)`, `len(PadB)` and `len(IA)`. ([#6973](https://github.com/transmission/transmission/pull/6973))
  * Better utilize high Internet bandwidth. ([#7029](https://github.com/transmission/transmission/pull/7029))
  * Refactor: use new `tr_variant` API for resume. ([#7069](https://github.com/transmission/transmission/pull/7069))
  * Refactor: use evhttp public accessors in rpc server. ([#7112](https://github.com/transmission/transmission/pull/7112))
  * Fix: use message id to check for pex and metadata xfer support. ([#7177](https://github.com/transmission/transmission/pull/7177))
  * Feat: support the JSON `null` type in `tr_variant`. ([#7255](https://github.com/transmission/transmission/pull/7255))
  * Fix: shadowed variable warning in `tr_torrentVerify()`. ([#7305](https://github.com/transmission/transmission/pull/7305))
  * Harden the HTTP tracker response parser. ([#7326](https://github.com/transmission/transmission/pull/7326))
  * Save upload/download queue order between sessions. ([#7332](https://github.com/transmission/transmission/pull/7332))
  * Fixed an issue where the speed limits are not effective below 16KiB/s. ([#7339](https://github.com/transmission/transmission/pull/7339))
  * Refactor: set peer io socket in constructor. ([#7355](https://github.com/transmission/transmission/pull/7355))
  * Chore: bump wide-integer. ([#7383](https://github.com/transmission/transmission/pull/7383))
  * Added workaround for crashes related to [Curl bug 10936](https://github.com/curl/curl/issues/10936). ([#7416](https://github.com/transmission/transmission/pull/7416))
  * Experimental fix for frequent corrupt pieces and stuck progress. ([#7443](https://github.com/transmission/transmission/pull/7443))
  * Feat: warn about problematic curl versions. ([#7457](https://github.com/transmission/transmission/pull/7457))
  * Support trackers that only support the old BEP-7 with `&ipv4=` and `&ipv6=`. ([#7481](https://github.com/transmission/transmission/pull/7481))
  * Refactor: rename unreleased quarks to snake_case. ([#7483](https://github.com/transmission/transmission/pull/7483))
* @Terentyev ([Alexander Terentyev](https://github.com/Terentyev)):
  * Added support for using a proxy server for web connections. ([#5038](https://github.com/transmission/transmission/pull/5038))
* @ThinkChaos:
  * Code review. ([#5038](https://github.com/transmission/transmission/pull/5038))
* @userwiths ([Bark](https://github.com/userwiths)):
  * Fix: Take into account only the private that is inside info. ([#7313](https://github.com/transmission/transmission/pull/7313))
* @wegood9 ([pathC](https://github.com/wegood9)):
  * Added a workaround for users affected by [Curl bug 6312](https://github.com/curl/curl/issues/6312). ([#7447](https://github.com/transmission/transmission/pull/7447))

### Contributions to macOS Client:

* @michalsrutek ([Michal Šrůtek](https://github.com/michalsrutek)):
  * Fixed MacStadium opensource URL. ([#7289](https://github.com/transmission/transmission/pull/7289))

### Contributions to Qt Client:

* @H5117:
  * Qt: refactor Application. ([#7092](https://github.com/transmission/transmission/pull/7092))
* @killemov:
  * Code review. ([#7092](https://github.com/transmission/transmission/pull/7092))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7479](https://github.com/transmission/transmission/pull/7479))

### Contributions to GTK Client:

* @cloppingemu ([cloppingemu](https://github.com/cloppingemu)):
  * Fixing a bug in adding torrent in GTK application. ([#7247](https://github.com/transmission/transmission/pull/7247))

### Contributions to Web Client:

* @Rukario:
  * Code review. ([#7340](https://github.com/transmission/transmission/pull/7340), [#7346](https://github.com/transmission/transmission/pull/7346))
  * Refactor: alternative x/y coords to account for zoomed in browser. ([#6945](https://github.com/transmission/transmission/pull/6945))
  * Added column mode for viewport unconstrained browsers. ([#7051](https://github.com/transmission/transmission/pull/7051))
  * Removed per-torrent start/pause button from web client. ([#7292](https://github.com/transmission/transmission/pull/7292))
  * Fixed an issue where Transmission web's custom context menu does not close when clicking on some outside element. ([#7296](https://github.com/transmission/transmission/pull/7296))
  * Implemented a new popup management system for web client to support multiple popups in a hierarchy-like system. ([#7297](https://github.com/transmission/transmission/pull/7297))
  * Refactor: multiple popups code refinement. ([#7310](https://github.com/transmission/transmission/pull/7310))
  * Refactor: pointer device listener code refinement. ([#7311](https://github.com/transmission/transmission/pull/7311))
  * Fixed a bug inflating per-torrent rows by long torrent names in compact view. ([#7336](https://github.com/transmission/transmission/pull/7336))
  * Refactor: drop className `.full` in favor of `:not(.compact)`. ([#7354](https://github.com/transmission/transmission/pull/7354))
  * Added a new alert message of a problem when renaming torrent or file name. ([#7394](https://github.com/transmission/transmission/pull/7394))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#6945](https://github.com/transmission/transmission/pull/6945), [#7051](https://github.com/transmission/transmission/pull/7051), [#7297](https://github.com/transmission/transmission/pull/7297), [#7310](https://github.com/transmission/transmission/pull/7310), [#7354](https://github.com/transmission/transmission/pull/7354))
  * Fix(webui): dispatch `close` events when closing popups. ([#7340](https://github.com/transmission/transmission/pull/7340))
  * Fixed incorrect text entry sensitivity when sessions changed. ([#7346](https://github.com/transmission/transmission/pull/7346))

### Contributions to Daemon:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Added optional sequential downloading. ([#7048](https://github.com/transmission/transmission/pull/7048))

### Contributions to transmission-cli:

* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Refactor: convert `tr_net_init_mgr` to singleton. ([#6914](https://github.com/transmission/transmission/pull/6914))

### Contributions to transmission-remote:

* @bheesham ([Bheesham Persaud](https://github.com/bheesham)):
  * Improved error logging. ([#7034](https://github.com/transmission/transmission/pull/7034))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#7034](https://github.com/transmission/transmission/pull/7034))

### Contributions to Everything Else:

* @bitigchi ([Emir SARI](https://github.com/bitigchi)):
  * Use en and em dashes where appropriate. ([#7402](https://github.com/transmission/transmission/pull/7402))
* @mhadam ([Michael Hadam](https://github.com/mhadam)):
  * Updated rpc-spec.md. ([#7387](https://github.com/transmission/transmission/pull/7387))
* @nevack ([Dzmitry Neviadomski](https://github.com/nevack)):
  * Fixed building transmission with C++23. ([#6832](https://github.com/transmission/transmission/pull/6832))
* @reardonia ([reardonia](https://github.com/reardonia)):
  * Code review. ([#7408](https://github.com/transmission/transmission/pull/7408))
* @tearfur ([Yat Ho](https://github.com/tearfur)):
  * Code review. ([#6832](https://github.com/transmission/transmission/pull/6832))
  * Test(dht): use static IP address. ([#7408](https://github.com/transmission/transmission/pull/7408))

