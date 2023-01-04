# Transmission 4.0.0-beta.3

## What's New in 4.0.0-beta.3

### libtransmission (All Platforms)

* Dropped obsolete CyaSSL and PolarSSL crypto backends (WolfSSL and MbedTLS are still supported). ([#4495](https://github.com/transmission/transmission/pull/4495))
* Updated extension protocol handshake to include `yourip` value as suggested by [BEP 10](https://www.bittorrent.org/beps/bep_0010.html). ([#4504](https://github.com/transmission/transmission/pull/4504))
* Fixed out-of-order teardown bug that could cause a crash on shutdown. ([#4331](https://github.com/transmission/transmission/pull/4331), [#4348](https://github.com/transmission/transmission/pull/4348), [#4451](https://github.com/transmission/transmission/pull/4451))
* Fixed `4.0.0-beta.1` bug that broke detection of a peer's UDP port in a peer handshake. ([#4334](https://github.com/transmission/transmission/pull/4334))
* Fixed `4.0.0-beta.2` regression that broke port forwarding in some settings. ([#4343](https://github.com/transmission/transmission/pull/4343))
* Fixed `4.0.0-beta.2` bitfield crash. ([#4346](https://github.com/transmission/transmission/pull/4346))
* Fixed `4.0.0-beta.1` pattern matching in whitelist and host_whitelist. ([#4353](https://github.com/transmission/transmission/pull/4353))
* Fixed `4.0.0-beta.2` crash when pausing a torrent. ([#4358](https://github.com/transmission/transmission/pull/4358))
* Fixed `4.0.0-beta.2` IPv6 μTP socket binding regression. ([#4469](https://github.com/transmission/transmission/pull/4469))
* Followed [BEP 7](https://www.bittorrent.org/beps/bep_0007.html) suggestion to remove `&ipv4=` and `&ipv6=` query parameters from tracker announcements. ([#4502](https://github.com/transmission/transmission/pull/4502))
* Followed [BEP 7](https://www.bittorrent.org/beps/bep_0007.html) suggestion to make the tracker announce `&key=`  query parameter unique per-torrent. ([#4508](https://github.com/transmission/transmission/pull/4508))
* Updated the bookkeeping to ensure both TCP and uTP connections honor the connection limit. ([#4534](https://github.com/transmission/transmission/pull/4534))
* Made small performance improvements in libtransmission. ([#4393](https://github.com/transmission/transmission/pull/4393), [#4401](https://github.com/transmission/transmission/pull/4401), [#4404](https://github.com/transmission/transmission/pull/4404), [#4412](https://github.com/transmission/transmission/pull/4412), [#4424](https://github.com/transmission/transmission/pull/4424), [#4425](https://github.com/transmission/transmission/pull/4425), [#4431](https://github.com/transmission/transmission/pull/4431), [#4519](https://github.com/transmission/transmission/pull/4519))
* Improved test coverage in the code that checks for reserved IP address use. ([#4462](https://github.com/transmission/transmission/pull/4462))

### macOS Client

* Sorting by size now only uses the sizes of files that are wanted. ([#4365](https://github.com/transmission/transmission/pull/4365))
* Fixed memory leak in the blocklist downloader. ([#4309](https://github.com/transmission/transmission/pull/4309))
* Fixed UI issues in the main window when using Groups. ([#4333](https://github.com/transmission/transmission/pull/4333))
* Improved layout of macOS UI elements. ([#4366](https://github.com/transmission/transmission/pull/4366), [#4367](https://github.com/transmission/transmission/pull/4367), [#4460](https://github.com/transmission/transmission/pull/4460))
* Fixed the background style of torrents selected in the main window. ([#4458](https://github.com/transmission/transmission/pull/4458))
* Updated code that had been using deprecated API. ([#4308](https://github.com/transmission/transmission/pull/4308), [#4441](https://github.com/transmission/transmission/pull/4441))
* Removed unused or unnecessary code. ([#4374](https://github.com/transmission/transmission/pull/4374), [#4440](https://github.com/transmission/transmission/pull/4440))
* Fixed `4.0.0-beta.1` regression that showed an incorrect icon or name in the drag overlay. ([#4428](https://github.com/transmission/transmission/pull/4428))
* Fixed the file display of torrents that consist of just a single file in a single folder. ([#4454](https://github.com/transmission/transmission/pull/4454))

### Qt Client

* Fixed progress bars positioning on Mac. ([#4489](https://github.com/transmission/transmission/pull/4489))
* Added Qt dependencies for Windows build instructions and minor fixes. ([#4363](https://github.com/transmission/transmission/pull/4363))
* Updated Qt CMakeLists.txt to include support for building svg. ([#4437](https://github.com/transmission/transmission/pull/4437))

### GTK Client

* Fixed `4.0.0-beta.1` regression leading to potential crash on startup upon watch directory setup. ([#4355](https://github.com/transmission/transmission/pull/4355))
* Fixed `4.0.0-beta.1` regression that prevented closing the "update blocklist" dialog. ([#4391](https://github.com/transmission/transmission/pull/4391), [#4392](https://github.com/transmission/transmission/pull/4392))
* Fixed a bug that hid the "Enable µTP for peer communication" checkbox. ([#4349](https://github.com/transmission/transmission/pull/4349))
* Removed unused or unnecessary code. ([#4416](https://github.com/transmission/transmission/pull/4416))

### transmission-remote

* Fixed `4.0.0-beta.1` bug that showed the wrong ETA for some torrents. ([#4506](https://github.com/transmission/transmission/pull/4506))

### transmission-show

* Fixed `4.0.0-beta.2` regression that caused `transmission-show --scrape` to not exit cleanly. ([#4447](https://github.com/transmission/transmission/pull/4447))

### Everything Else

* Added Windows build manual. ([#4291](https://github.com/transmission/transmission/pull/4291))
* Removed Visual C++ redistributable libraries installation from the MSI package. ([#4339](https://github.com/transmission/transmission/pull/4339))
* Removed obsolete 'lightweight' build option. ([#4509](https://github.com/transmission/transmission/pull/4509))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to libtransmission (All Platforms):

* [@stefantalpalaru (Ștefan Talpalaru)](https://github.com/stefantalpalaru):
  * Fixed `4.0.0-beta.2` regression that broke port forwarding in some settings. ([#4343](https://github.com/transmission/transmission/pull/4343))
* [@tinselcity (Reed Morrison)](https://github.com/tinselcity):
  * Removed unused UTP Socket code. ([#4409](https://github.com/transmission/transmission/pull/4409))

### Contributions to macOS Client:

* [@nevack (Dzmitry Neviadomski)](https://github.com/nevack):
  * Code review for [#4308](https://github.com/transmission/transmission/pull/4308), [#4309](https://github.com/transmission/transmission/pull/4309), [#4333](https://github.com/transmission/transmission/pull/4333), [#4366](https://github.com/transmission/transmission/pull/4366), [#4428](https://github.com/transmission/transmission/pull/4428), [#4440](https://github.com/transmission/transmission/pull/4440), [#4473](https://github.com/transmission/transmission/pull/4473)
* [@sweetppro (SweetPPro)](https://github.com/sweetppro):
  * Code review for [#4308](https://github.com/transmission/transmission/pull/4308), [#4310](https://github.com/transmission/transmission/pull/4310), [#4367](https://github.com/transmission/transmission/pull/4367), [#4414](https://github.com/transmission/transmission/pull/4414), [#4417](https://github.com/transmission/transmission/pull/4417), [#4418](https://github.com/transmission/transmission/pull/4418), [#4428](https://github.com/transmission/transmission/pull/4428), [#4461](https://github.com/transmission/transmission/pull/4461)
  * Fixed UI issues in the main window when using Groups. ([#4333](https://github.com/transmission/transmission/pull/4333))
  * Improved layout of macOS UI elements. ([#4366](https://github.com/transmission/transmission/pull/4366))
  * Removed unused or unnecessary code. ([#4374](https://github.com/transmission/transmission/pull/4374))
  * Removed unused or unnecessary code. ([#4440](https://github.com/transmission/transmission/pull/4440))
  * Refactor ActivityView. ([#4448](https://github.com/transmission/transmission/pull/4448))
  * Fixed the background style of torrents selected in the main window. ([#4458](https://github.com/transmission/transmission/pull/4458))
  * Improved layout of macOS UI elements. ([#4460](https://github.com/transmission/transmission/pull/4460))

### Contributions to Qt Client:

* [@GaryElshaw (Gary Elshaw)](https://github.com/GaryElshaw):
  * Updated Qt CMakeLists.txt to include support for building svg. ([#4437](https://github.com/transmission/transmission/pull/4437))
* [@smrtrfszm (Szepesi Tibor)](https://github.com/smrtrfszm):
  * Added Qt dependencies for Windows build instructions and minor fixes. ([#4363](https://github.com/transmission/transmission/pull/4363))

### Contributions to transmission-remote:

* [@lajp (Luukas Pörtfors)](https://github.com/lajp):
  * Fixed `4.0.0-beta.1` bug that showed the wrong ETA for some torrents. ([#4506](https://github.com/transmission/transmission/pull/4506))

### Contributions to Everything Else:

* [@dmantipov (Dmitry Antipov)](https://github.com/dmantipov):
  * Refactor, ci: switch clang-format to LLVM 15. ([#4297](https://github.com/transmission/transmission/pull/4297))
* [@ile6695 (Ilkka Kallioniemi)](https://github.com/ile6695):
  * Code review for [#4291](https://github.com/transmission/transmission/pull/4291)
* [@LaserEyess](https://github.com/LaserEyess):
  * Code review for [#4291](https://github.com/transmission/transmission/pull/4291)
* [@Petrprogs (Peter)](https://github.com/Petrprogs):
  * Added Windows build manual. ([#4291](https://github.com/transmission/transmission/pull/4291))

