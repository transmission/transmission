# [Transmission 4.0.0](https://github.com/transmission/transmission/releases/tag/4.0.0)

## Highlights

This is a major release, both in numbering and in effort! It's been in active development for over
a year and has a _huge_ list of changes -- over a thousand commits -- since Transmission 3.00.
Some of the highlights include:

- [**Resource Efficiency**](#resource-efficiency) - Use less memory and fewer CPU cycles
- [**Better Community**](#community) - Pull requests welcomed and used
- [**Code Modernization**](#code-modernization) - Migrated from C90 to modern C++
- [**New Features**](#new-features) - What would a major release be without them?

### Resource Efficiency

- The code has been extensively profiled and improved to fix inefficient code and memory use. For example, a stress test of starting transmission-daemon with 25,000 torrents is almost entirely IO-bound, using 50% fewer CPU cycles and 70% fewer memory allocations than Transmission 3.00.

- The remote control GUIs (transmission-qt and transmission-web) now use the RPC API "table" mode, resulting in smaller payloads / less bandwidth use.
- RPC payloads are now compressed using [libdeflate](https://github.com/ebiggers/libdeflate), a "heavily optimized [library that is] significantly faster than the zlib library."

### Community

- The project is much more responsive to bug reports and code submissions than it has been in the past.
- There is a new group of volunteer contributors who are working on Transmission!
- Transmission 4.0.0 includes over 350 new community commits (see [the Thank You section below](#thank-you) and in the previous 4.0.0 betas) since 3.00 and welcomes new contributors.
- Documentation has been moved into the `transmission/transmission` so that contributors can submit PRs to improve it.

### Code Modernization

- The entire codebase has been migrated from C to C++. In the process, we've removed thousands of lines of custom code and used standard C++ tools instead. The core's code has shrunk by 18%. The core codebase has been extensively refactored to be more testable and maintainable.
- The GTK client has been ported to [gtkmm](http://www.gtkmm.org/en/).
- The Web client has been rewritten in modern JavaScript and no longer uses jQuery. The entire gzipped bundle is now 68K.
- The unit tests have been expanded and ported to [Google Test](https://github.com/google/googletest). Clang sanitizer builds are run during CI.
- The core library is now fuzz tested.
- Transmission now uses Sonarcloud, Coverity, LGTM, and clang-tidy static analysis on new code. Several hundred code warnings have been fixed compared to Transmission 3.00.

### New Features

- Support for using [BitTorrent v2](http://bittorrent.org/beps/bep_0052.html) torrents and [hybrid](http://bittorrent.org/beps/bep_0052.html#upgrade-path) torrents. (Support for _creating_ v2 and hybrid torrents is slated for an upcoming release.)
- Users can now set "default" trackers that can be used to announce all public torrents.
- Newly-added seeds can start immediately and verify pieces on demand, instead of needing a full verify before seeding can begin. ([#2626](https://github.com/transmission/transmission/pull/2626))
- Added an option to omit potentially-identifying information (e.g. User-Agent and date created) when creating new torrents. ([#3452](https://github.com/transmission/transmission/pull/3452))
- The Web client has been rewritten and now supports mobile use.
- When creating new torrents, users can now specify the piece size. ([#3768](https://github.com/transmission/transmission/pull/3768), [#3145](https://github.com/transmission/transmission/pull/3145), [#2805](https://github.com/transmission/transmission/pull/2805))
- IPv6 blocklists are now supported. ([#3835](https://github.com/transmission/transmission/pull/3835))
- Beginning with 4.0.0-beta.1, Transmission releases now use [semver](https://semver.org/) versioning.
- Dozens of other new features -- too many to list here! We've been working on this for a year!

## What's New Since 4.0.0-beta.3

### libtransmission (All Platforms)

* Added a new setting, `torrent-added-verify-mode`, to force-verify added torrents. ([#4611](https://github.com/transmission/transmission/pull/4611))
* Improved handling of webseed servers that do not support [Range](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Range) requests or [206 Partial Content](https://developer.mozilla.org/en-US/docs/Web/HTTP/Status/206). ([#4601](https://github.com/transmission/transmission/pull/4601))
* Improved handling of webseed servers that return a content-encoding that was not requested. ([#4609](https://github.com/transmission/transmission/pull/4609))
* Fixed `4.0.0-beta.3` potential socket leak. ([#4616](https://github.com/transmission/transmission/pull/4616))
* Fixed `4.0.0-beta.3` regression that failed to detect largefile build flags on 32bit systems. ([#4627](https://github.com/transmission/transmission/pull/4627))
* Fixed `4.0.0-beta.1` UI bug when removing a tracker from the announce list. ([#4635](https://github.com/transmission/transmission/pull/4635))
* Improved error handling when receiving corrupt piece data from peers. ([#4665](https://github.com/transmission/transmission/pull/4665))
* Fixed `4.0.0-beta.1` FTBFS error on CentOS 7. ([#4673](https://github.com/transmission/transmission/pull/4673), [#4675](https://github.com/transmission/transmission/pull/4675))
* Made small performance improvements in libtransmission. ([#4577](https://github.com/transmission/transmission/pull/4577), [#4679](https://github.com/transmission/transmission/pull/4679))
* Dropped [libiconv](https://www.gnu.org/software/libiconv/) dependency in libtransmission. ([#4565](https://github.com/transmission/transmission/pull/4565))
* Updated 403 RPC error message. ([#4567](https://github.com/transmission/transmission/pull/4567))
* Bumped [libdeflate](https://github.com/ebiggers/libdeflate) snapshot to 1.17. ([#4596](https://github.com/transmission/transmission/pull/4596))

### macOS Client

* Fixed two Help menus in macOS. ([#4500](https://github.com/transmission/transmission/pull/4500))
* Fixed bug that caused magnet links to always be paused when added. ([#4528](https://github.com/transmission/transmission/pull/4528))
* Fixed `4.0.0-beta.1` regression that broke the Piece View "blinking" when a piece completes. ([#4587](https://github.com/transmission/transmission/pull/4587))
* Ensured that the preferences window is centered the first time it is shown. ([#4659](https://github.com/transmission/transmission/pull/4659))
* Fixed `4.0.0-beta.1` regression that incorrectly handled corrupt blocklists. ([#4705](https://github.com/transmission/transmission/pull/4705))
* Increased the font size in the main window's torrent list. ([#4557](https://github.com/transmission/transmission/pull/4557))
* Fixed [libpsl](https://github.com/rockdaboot/libpsl) dependency build issue on macOS. ([#4642](https://github.com/transmission/transmission/pull/4642))

### Qt Client

* Made display order of speed limits consistent between Properties, Details dialogs. ([#4677](https://github.com/transmission/transmission/pull/4677))

### GTK Client

* Fixed `4.0.0-beta.1` regression making it impossible to close "Set location" dialog (GTK 3 only). ([#4625](https://github.com/transmission/transmission/pull/4625))
* Fixed `4.0.0-beta.2` regression leading to crash during progress bars rendering on some systems. ([#4688](https://github.com/transmission/transmission/pull/4688))

### Web Client

* Fixed `4.0.0-beta.1` potential crash when detecting mime-types. ([#4569](https://github.com/transmission/transmission/pull/4569))
* Fixed `4.0.0-beta.1` regression that broke file priority buttons in the web client. ([#4610](https://github.com/transmission/transmission/pull/4610))

### Daemon

* Fixed stderr logging issue when running as a `systemd` unit. ([#4612](https://github.com/transmission/transmission/pull/4612))

### transmission-remote

* Fixed `4.0.0-beta.1` regression when displaying session info. ([#4624](https://github.com/transmission/transmission/pull/4624))
* Fixed `4.0.0-beta.1` regression when displaying tracker info. ([#4633](https://github.com/transmission/transmission/pull/4633))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these people who contributed to this release:

### Contributions to libtransmission (All Platforms):

* [@dmantipov (Dmitry Antipov)](https://github.com/dmantipov):
  * Chore: simplify announcer add callback. ([#4573](https://github.com/transmission/transmission/pull/4573))
  * Refactor: use std::function for announcer callback. ([#4575](https://github.com/transmission/transmission/pull/4575))
  * Refactor: switch to C++11-compatible tr_wait() from tr_wait_msec(). ([#4576](https://github.com/transmission/transmission/pull/4576))
  * Made small performance improvements in libtransmission. ([#4577](https://github.com/transmission/transmission/pull/4577))
  * Made small performance improvements in libtransmission. ([#4679](https://github.com/transmission/transmission/pull/4679))
* [@InsaneKnight](https://github.com/InsaneKnight):
  * Fixed `4.0.0-beta.3` potential socket leak. ([#4616](https://github.com/transmission/transmission/pull/4616))
* [@nevack (Dzmitry Neviadomski)](https://github.com/nevack):
  * Code review for [#4576](https://github.com/transmission/transmission/pull/4576)

### Contributions to macOS Client:

* [@cfauchereau (Clément Fauchereau)](https://github.com/cfauchereau):
  * Fixed two Help menus in macOS. ([#4500](https://github.com/transmission/transmission/pull/4500))
* [@DevilDimon (Dmitry Serov)](https://github.com/DevilDimon):
  * Code review for [#4500](https://github.com/transmission/transmission/pull/4500), [#4692](https://github.com/transmission/transmission/pull/4692)
* [@GaryElshaw (Gary Elshaw)](https://github.com/GaryElshaw):
  * Increased the font size in the main window's torrent list. ([#4557](https://github.com/transmission/transmission/pull/4557))
* [@nevack (Dzmitry Neviadomski)](https://github.com/nevack):
  * Code review for [#4500](https://github.com/transmission/transmission/pull/4500), [#4692](https://github.com/transmission/transmission/pull/4692)
  * Fixed `4.0.0-beta.1` regression that incorrectly handled corrupt blocklists. ([#4705](https://github.com/transmission/transmission/pull/4705))
* [@sweetppro (SweetPPro)](https://github.com/sweetppro):
  * Fixed bug that caused magnet links to always be paused when added. ([#4528](https://github.com/transmission/transmission/pull/4528))

### Contributions to Qt Client:

* [@anarcat (anarcat)](https://github.com/anarcat):
  * Added remote HTTPS support to Qt GUI. ([#4622](https://github.com/transmission/transmission/pull/4622))
* [@GaryElshaw (Gary Elshaw)](https://github.com/GaryElshaw):
  * Removed 'Message Log' item from Qt sys tray. ([#4656](https://github.com/transmission/transmission/pull/4656))
* [@Schlossgeist (Nick)](https://github.com/Schlossgeist):
  * Quick fix: changed order of some session prefs. ([#4676](https://github.com/transmission/transmission/pull/4676))

### Contributions to Web Client:

* [@dareiff (Derek Reiff)](https://github.com/dareiff):
  * Fixed `4.0.0-beta.1` regression that broke file priority buttons in the web client. ([#4610](https://github.com/transmission/transmission/pull/4610))

### Contributions to Daemon:

* [@dmantipov (Dmitry Antipov)](https://github.com/dmantipov):
  * Fixed stderr logging issue when running as a `systemd` unit. ([#4612](https://github.com/transmission/transmission/pull/4612))

### Contributions to transmission-remote:

* [@lajp (Luukas Pörtfors)](https://github.com/lajp):
  * Fixed `4.0.0-beta.1` regression when displaying session info. ([#4624](https://github.com/transmission/transmission/pull/4624))
  * Fixed `4.0.0-beta.1` regression when displaying tracker info. ([#4633](https://github.com/transmission/transmission/pull/4633))

### Contributions to Everything Else:

* [@dependabot[bot]](https://github.com/apps/dependabot):
  * Build(deps): bump json5 from 2.2.1 to 2.2.3 in /web. ([#4560](https://github.com/transmission/transmission/pull/4560))
* [@github-actions[bot]](https://github.com/apps/github-actions):
  * Chore: update generated transmission-web files. ([#4664](https://github.com/transmission/transmission/pull/4664))
* [@progiv (progiv)](https://github.com/progiv):
  * Fixed GitHub CI actions warning for set-output. ([#4564](https://github.com/transmission/transmission/pull/4564))
* [@t-8ch (Thomas Weißschuh)](https://github.com/t-8ch):
  * Code review for [#4637](https://github.com/transmission/transmission/pull/4637)

