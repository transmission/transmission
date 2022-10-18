# [Transmission 4.0.0-beta.1](https://github.com/transmission/transmission/releases/tag/4.0.0-beta.1) (2022-08-06)

## Highlights

Welcome to the first beta release of Transmission 4.0.0!
It's been in active development for over a year and has a _huge_ list of changes -- over 1,000 commits -- since Transmission 3.00.
Some of the highlights include:

- [**Resource Efficiency**](#resource-efficiency) - Use less memory, fewer CPU cycles
- [**Better Community**](#community) - Pull requests welcomed and used
- [**Code Modernization**](#code-modernization) - Rewritten in C++
- [**New Features**](#newfFeatures) - What would a major release be without them?

### Resource Efficiency

- The code has been extensively profiled and improved to fix inefficient code and memory use. For example, a stress test of starting transmission-daemon with 25,000 torrents is almost entirely IO-bound, using 50% fewer CPU cycles and 70% fewer memory allocations than Transmission 3.00.

- The remote control GUIs (transmission-qt and transmission-web) now use the RPC API "table" mode, resulting in smaller payloads / less bandwidth use.
- RPC payloads are now compressed using [libdeflate](https://github.com/ebiggers/libdeflate), a "heavily optimized [library that is] significantly faster than the zlib library."

### Community

- The project is much more responsive to bug reports and code submissions than it has been in the past.
- There is a new group of volunteer contributors who are working on Transmission!
- Transmission 4.0.0-beta.1 includes over 300 new community commits (see [the Thank You section below](#Thank-You) for a list) and is welcoming new contributors.
- Documentation has been moved into the `transmission/transmission` so that contributors can submit PRs to improve it

### Code Modernization

- The entire codebase has been migrated from C to C++. In the process, we've removed thousands of lines of custom code and used standard C++ tools instead. The core's code has shrunk by 18%. The core codebase has extensively refactored to be more testable and maintainable.
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

## Other Changes

[[**All Platforms**](#All-Platforms)]
[[**macOS Client**](#macOS-Client)]
[[**Qt Client**](#qt-client)]
[[**GTK Client**](#gtk-client)]
[[**Daemon**](#Daemon)]
[[**Web Client**](#web-client-1)]
[[**Utils**](#utils)]
[[**Everything Else!**](#everything-else)]

### All Platforms

 - Fix small scrape/announce packet size that could trip SYN flood detection on some systems ([#3236](https://github.com/transmission/transmission/pull/3236))
 - Remove the 1024 open files limit previously required by how libcurl was used ([#893](https://github.com/transmission/transmission/pull/893))
 - Add configurable anti-brute force settings ([#1447](https://github.com/transmission/transmission/pull/1447))
 - Fetch metadata of stopped magnets ([#1080](https://github.com/transmission/transmission/pull/1080))
 - Stop logging excessive error messages after they repeat too many times. In some cases, repetitive messages had been spamming syslogs. ([#2756](https://github.com/transmission/transmission/pull/2756))

### macOS Client

- Transmission is now a Universal (Apple Silicon and Intel) app.
- Refresh of the UI to better fit into modern macOS.
- A new icon designed for Big Sur by Rodger Werner.

### Qt Client

- Support both Qt5 and Qt6 ([#2069](https://github.com/transmission/transmission/pull/2069))
- Nicer error handling when duplicate torrents are added ([#1410](https://github.com/transmission/transmission/pull/1410))
- More efficient use of RPC ([#1234](https://github.com/transmission/transmission/pull/1234), [#1322](https://github.com/transmission/transmission/pull/1322), [#1333](https://github.com/transmission/transmission/pull/1333))
- More efficient state updates ([#1334](https://github.com/transmission/transmission/pull/1334), [#1335](https://github.com/transmission/transmission/pull/1335), [#1336](https://github.com/transmission/transmission/pull/1336), [#1428](https://github.com/transmission/transmission/pull/1428), [#1430](https://github.com/transmission/transmission/pull/1430), [#1432](https://github.com/transmission/transmission/pull/1432), [#1433](https://github.com/transmission/transmission/pull/1433), [#1234](https://github.com/transmission/transmission/pull/1234))
- Slightly more efficient RPC requests ([#1373](https://github.com/transmission/transmission/pull/1373))
- Better caching of tracker favicons ([#1402](https://github.com/transmission/transmission/pull/1402))
- Fix memory leaks ([#1378](https://github.com/transmission/transmission/pull/1378))
- Fix FreeSpaceLabel crash ([#1604](https://github.com/transmission/transmission/pull/1604))
- Add remote server version info in the About dialog ([#1603](https://github.com/transmission/transmission/pull/1603))
- Support `TR_RPC_VERBOSE` environment variable for debugging RPC calls ([#1435](https://github.com/transmission/transmission/pull/1435))
- Allow filtering by info hash ([#1763](https://github.com/transmission/transmission/pull/1763))

### GTK Client

- Ported to [GTKMM](https://gtkmm.org/en/index.html)
- When creating new torrents, their piece size can be specified.
- The File menu now incorporates normal quick key operations.
- The Details dialog now includes the date a torrent was added.
- The Details dialog now renders large file lists faster. ([#2992](https://github.com/transmission/transmission/pull/2992], [#2993](https://github.com/transmission/transmission/pull/2993])
- Better i18n of phrases for past, present, and future tense ([#3214](https://github.com/transmission/transmission/issues/3214#event-6809248892))

### Daemon

TODO

### Web Client

The web client has been given a major overhaul. ([#1476](https://github.com/transmission/transmission/pull/1476))

User-visible highlights include:
* Mobile is now fully supported.
* Added fullscreen support on mobile.
* Better support for dark mode.
* Added mime icons to the torrent list.
* Improved theme consistency across the app.

Maintainer highlights include:
* Updated code to use ES6 APIs.
* No longer use jQuery UI.
* No longer use jQuery.
* Use Webpack to bundle the Javascript, CSS, and assets together -- the entire bundle size is now 68K gzipped.
* Added eslint / prettier / stylelint tooling.
* Uses torrent-get's 'table' mode for more efficient RPC calls.

### Utils
- Allow webseed URLs when creating torrents in transmission-create
- Display more progress information during torrent creation in transmission-create ([#1405](https://github.com/transmission/transmission/pull/1405))

### Everything Else!

- https://transmissionbt.com/ has been redesigned! Thanks to [@shatteredsite](https://github.com/shatteredsite) the redesign and to [@Oleg-Chashko](https://github.com/Oleg-Chashko) and [@GaryElshaw](https://github.com/GaryElshaw) for feedback!
- Roger Werner made a new Transmission icon!

## Thank You

Last but certainly not least, a big **Thank You** to the people who contributed to this release: acchang, Ali, Andrey, Balázs Meskó, beizmos, Berbe, bexnoss, bkuhls, buckmelanoma, Carles Pastor Badosa, Charles Kerr, Chris Young, Chrool, Cœur, Colin B, Craig Andrews, C.W. Betts, Dachtire, Daniel Kamil Kozar, Dan Walters, David Beinder, David Miguel Susano Pinto, dependabot[bot], depler, Dinesh Manajipet, Dmitry Antipov, Dmitry Serov, Dmytro Lytovchenko, dubhater, Dzmitry Neviadomski, Esa Varemo, evils, ewtoombs, FallenWarrior2k, Federico Bond, FluxState, Frank Aurich, FX Coudert, Gary Elshaw, goldsteinn, Greg Hazel, Guido Cella, Guido Vranken, Hakjoon Sim, Han Shen, Harm133, Ilkka Kallioniemi, IMurzich, Isabella Skořepová, Jelle van der Waa, Johan, Jonas Malaco, JP-Ellis, kakuhen, Kirill Ovchinnikov, Kobaxidze256, Koro, L2501, LaserEyess, Lucas Clemente Vella, lucaxvi, Luukas Pörtfors, luzpaz, Mark Deepwell, Markus Amalthea Magnuson, Matan Ziv-Av, Matt Joiner, maxz, Max Zettlmeißl, Michael Lopez, Michal Kubiak, mickaelifs, Mike Gelfand, Mike Gilbert, Mitchell Livingston, Nathan Benichou, Nicholas Guriev, Noobsai, Norbert Papke, Oleg Chashko, orbital-mango, OscarCunningham, Pavel Borzenkov, Pedro Scarapicchia Junior, Peter Bailey, Petrprogs, Pierre Carru, qu1ck, razaq, RobCrowston, Robert Palmer, Robin Seth Ekman, Rosen Penev, Sam Marcus, Sander van Kasteel, Sergey Fedoseev, sewe2000, shelvacu, Simone Tellini, Stefan Talpalaru, SweetPPro, Tomáš Kelemen, Tyler, Viacheslav Chimishuk, Vik, Vincent Vinel, Vitaly Potyarkin, vuori, wiz78, and Xist12gh!

These release notes were compiled manually. Since 4.0.0 has been in development for so long, this has been a time-consuming and error-prone process. If you contributed to Transmission and aren't listed here or below, we apologise in advance!

### Notable Code Contributions

#### Core contributions (`libtransmission`):

- [1100101](https://github.com/1100101) (Frank Aurich):
  - Fix overflow bugs on 32bit platforms with torrents larger than 4GB ([#2391](https://github.com/transmission/transmission/pull/2391), [#2378](https://github.com/transmission/transmission/pull/2378))
  - Fix RPC 'table' mode not being properly activated ([#2058](https://github.com/transmission/transmission/pull/2058))
  - Fix RPC response payload when removing torrents ([#2040](https://github.com/transmission/transmission/pull/2040))
- [anacrolix](https://github.com/anacrolix) (Matt Joiner):
  - Reject cancels when fast extension enabled ([#2275](https://github.com/transmission/transmission/pull/2275))
- [AndreyPavlenko](https://github.com/AndreyPavlenko) (Andrey Pavlenko):
  - use new environment variable `TR_CURL_PROXY_SSL_NO_VERIFY` ([#2622](https://github.com/transmission/transmission/pull/2622))
- [lajp](https://github.com/lajp) (Luukas Pörtfors):
  - Add renaming support in transmission-remote ([#2905](https://github.com/transmission/transmission/pull/2905))
- [AndreyPavlenko](https://github.com/AndreyPavlenko) (Andrey Pavlenko):
  - Use android logger on Android ([#585](https://github.com/transmission/transmission/pull/585))
- [bkkuhls](https://github.com/bkuhls):
  - Fix cross-compile build error ([#2576](https://github.com/transmission/transmission/pull/2576))
- [carandraug](https://github.com/carandraug) (David Miguel Susano Pinto):
  - Added `transmission-create` option to omit potentially-identifying information, such as User-Agent and date created, when creating torrents. ([#3452](https://github.com/transmission/transmission/pull/3452))
- [cfpp2p](https://github.com/cfpp2p):
  - add an option to run a script when a torrent is added ([#1896](https://github.com/transmission/transmission/pull/1896))
- [Chrool](https://github.com/Chrool)
  - Make anti-brute force RPC server configurable ([#1447](https://github.com/transmission/transmission/pull/1447))
- [clickyotomy](https://github.com/clickyotomy) (Srinidhi Kaushik):
  - magnet link improvements incl. regression tests for invalid magnets ([#2483](https://github.com/transmission/transmission/pull/2483))
- [dbeinder](https://github.com/dbeinder) (David Beinder):
  - Fix unreleased regression that broke RPC bitfield values ([#2768](https://github.com/transmission/transmission/pull/2799))
  - Return correct bitfield when fully downloaded ([#2799](https://github.com/transmission/transmission/pull/2768))
- Dan Walters:
  - Apply optional peer socket TOS to UDP sockets ([#1043](https://github.com/transmission/transmission/pull/1043))
- [deepwell](https://github.com/deepwell) (Mark Deepwell):
  - Add unrecognized client names ([#1363](https://github.com/transmission/transmission/pull/1363), [#1681](https://github.com/transmission/transmission/pull/1681))
- [dgcampea](https://github.com/dgcampea):
  - support DSCP classes in socket iptos ([#2594](https://github.com/transmission/transmission/pull/2594))
- [Dmitry Antipov](https://github.com/dmantipov):
  - C++ify UDP core object ([#3794](https://github.com/transmission/transmission/pull/3794))
  - Fix warning issued during LTO builds ([#3766](https://github.com/transmission/transmission/pull/3766))
- [goldsteinn](https://github.com/goldsteinn):
  - Performance improvements to bitfield.cc ([#2933](https://github.com/transmission/transmission/pull/2933), [#2950](https://github.com/transmission/transmission/pull/2950))
- [guidovranken](https://github.com/guidovranken)
  - Fix out-of-bounds read in torrent parsing ([#3600](https://github.com/transmission/transmission/pull/3600))
- [Harm33](https://github.com/Harm133):
  - Blocklists now support IPv6 ([#3835](https://github.com/transmission/transmission/pull/3835))
- [Ivella](https://github.com/lvella) (Lucas Clemente Vella):
  - Implement latest version of BEP-7 for HTTP requests ([#1661](https://github.com/transmission/transmission/pull/1661))
- [ile6695](https://github.com/ile6695) (Ilkka Kallioniemi):
  - Fix filename reserved prefix detection. ([#3541](https://github.com/transmission/transmission/pull/3541))
  - Add more decimals for low wratios ([#2508](https://github.com/transmission/transmission/pull/2508))
  - Add regression tests for tr_strlcpy ([#2679](https://github.com/transmission/transmission/pull/2679))
- [johman10](https://github.com/johman10) (Johan):
  - Add total disk space to free-space RPC request ([#1682](https://github.com/transmission/transmission/pull/1682))
- [JP-Ellis](https://github.com/JP-Ellis) (JP Ellis):
  - Fix detection of PSL library ([#2812](https://github.com/transmission/transmission/pull/2812))
- [kakuhen](https://github.com/kakuhen):
  - Clarify documentation on torrent-add result on duplicate torrents ([#2690](https://github.com/transmission/transmission/pull/2690))
- [kvakvs](https://github.com/kvakvs) (Dmytro Lytovchenko):
  - C++ migration ([#3108](https://github.com/transmission/transmission/pull/3108), [#2010](https://github.com/transmission/transmission/pull/2010), [#1927](https://github.com/transmission/transmission/pull/1927), [#1917](https://github.com/transmission/transmission/pull/1917), [#1914](https://github.com/transmission/transmission/pull/1914), [#1895](https://github.com/transmission/transmission/pull/1895))
- [L2501](https://github.com/L2501):
  - Add recognition for more Peer IDs of other clients ([#3750](https://github.com/transmission/transmission/pull/3750), [#3838](https://github.com/transmission/transmission/pull/3838))
- [lanzaa](https://github.com/lanzaa) (Colin B):
  - Add basic support for BitTorrent v2 hashes ([#3380](https://github.com/transmission/transmission/pull/3380), [#3388](https://github.com/transmission/transmission/pull/3388)a
  - Support single-file hybrid torrents ([#3446](https://github.com/transmission/transmission/pull/3446))
- [LaserEyess](https://github.com/LaserEyess):
  - Add labels to torrent-add RPC method ([#2539](https://github.com/transmission/transmission/pull/2539))
  - Support binding RPC to a Unix socket ([#2574](https://github.com/transmission/transmission/pull/2574))
  - Add bind-address-ipv4 to UPnP ([#845](https://github.com/transmission/transmission/pull/845))
- [MatanZ](https://github.com/MatanZ) (Matan Ziv-Av):
  - Add support for bandwidth groups ([#2761](https://github.com/transmission/transmission/pull/2761), [#2818](https://github.com/transmission/transmission/pull/2818), [#2852](https://github.com/transmission/transmission/pull/2852))
- [mhadam](https://github.com/mhadam) (Michael Hadam):
  - Add support for torrent-get calls with the key percentComplete ([#2615](https://github.com/transmission/transmission/pull/2615))
- [miickaelifs](https://github.com/mickaelifs):
  - NAT-PMP private/public port and lifetime fix ([#1602](https://github.com/transmission/transmission/pull/1602))
- [narthorn](https://github.com/Narthorn):
  - Don't follow symlinks when removing junk files ([#1638](https://github.com/transmission/transmission/pull/1638))
- [neheb](https://github.com/neheb) (Rosen Penev):
  - fix runtime with wolfSSL and fastmath ([#1950](https://github.com/transmission/transmission/pull/1950))
- [noobsai](https://github.com/Noobsai):
  - Fix building for XFS ([#2192](https://github.com/transmission/transmission/pull/2192))
- [npapke](https://github.com/npapke) (Norbert Papke) and [cfpp2p](https://github.com/cfpp2p):
  - Fix "IPv4 DHT announce failed" error message ([#1619](https://github.com/transmission/transmission/pull/1619))
- [pyrovski](https://github.com/pyrovski) (Peter Bailey):
  - Allow stopping torrents in verification states ([#715](https://github.com/transmission/transmission/pull/715))
- [qu1ck](https://github.com/qu1ck):
  - Add label support to the core ([#822](https://github.com/transmission/transmission/pull/822), [#868](https://github.com/transmission/transmission/pull/868))
  - Fix unreleased fields handling regression in RPC ([#2972](https://github.com/transmission/transmission/pull/2972))
  - Fix peer port change in session rpc ([#3573](https://github.com/transmission/transmission/pull/3573))
- [razaq](https://github.com/razaqq):
  - add TR_TORRENT_TRACKERS env variable to script call ([#2053](https://github.com/transmission/transmission/pull/2053))
- [RobCrowston](https://github.com/RobCrowston) (Rob Crowston):
  - Add in-kernel file copying for several platforms. ([#1092](https://github.com/transmission/transmission/pull/1092))
  - Allow the OS to set the size of the listen queue connection backlog. ([#922](https://github.com/transmission/transmission/pull/922))
  - Refactor tr_torrentFindFile2() ([#921](https://github.com/transmission/transmission/pull/921))
  - Don't set thet path attribute when setting a cookie. ([#1893](https://github.com/transmission/transmission/pull/1893))
- [sandervankasteel](https://github.com/sandervankasteel) (Sander van Kasteel):
  - Added primitive CORS header support ([#1885](https://github.com/transmission/transmission/pull/1885))
- [sio](https://github.com/sio) (Vitaly Potyarkin):
  - Lift 1024 open files limit (switch to curl polling API) ([#893](https://github.com/transmission/transmission/pull/893))
- [stefantalpalaru](https://github.com/stefantalpalaru) (Ștefan Talpalaru):
  - Add support for default public trackers ([#2668](https://github.com/transmission/transmission/pull/2668))
- [TimoPtr](https://github.com/TimoPtr):
  - Add support for running a script when done seeding ([#2621](https://github.com/transmission/transmission/pull/2621))
- [uprt](https://github.com/uprt) (Kirill):
  - Replace NULL back with nullptr (mistake after auto-rebase) ([#1933](https://github.com/transmission/transmission/pull/1933))
  - Slashes fixes ([#857](https://github.com/transmission/transmission/pull/857))
- [Xist12gh](https://github.com/Xist12gh):
  - Add fallback for EXDEV error on Linux kernel 5.18 ([#3756](https://github.com/transmission/transmission/pull/3756))
- [vchimishuk](https://github.com/vchimishuk) (Viacheslav Chimishuk):
  - Add announce-ip and announce-ip-enabled parameters. ([#3461](https://github.com/transmission/transmission/pull/3461))
  - Fix broken tr_sessionSetPeerPortRandom() function. ([#3569](https://github.com/transmission/transmission/pull/3569))
  - Fix IPv6 socket binding interface. ([#3692](https://github.com/transmission/transmission/pull/3692))
- [vjunk](https://github.com/vjunk):
  - Add support for adding torrents by raw hash values ([#2608](https://github.com/transmission/transmission/pull/2608))
- [vuori](https://github.com/vuori):
  - Fixed wrong ipv6 addr used in announces. ([#265](https://github.com/transmission/transmission/pull/265))

#### `macOS Client` code contributions:

- [alimony](https://github.com/alimony) ((Markus Amalthea Magnuson):
  - Added ability to filter on error status. ([#19](https://github.com/transmission/transmission/pull/19))
- [azy5030](https://github.com/azy5030) (Ali):
  - Add ability to change piece size during torrent creation ([#2416](https://github.com/transmission/transmission/pull/2416))
- [Coeur](https://github.com/Coeur) (Cœur):
  - macOS client icon improvements ([#3250](https://github.com/transmission/transmission/pull/3250), [#3224](https://github.com/transmission/transmission/pull/3224), [#3094](https://github.com/transmission/transmission/pull/3094), [#3065](https://github.com/transmission/transmission/pull/3065))
  - Fix QuickLook ([#3001](https://github.com/transmission/transmission/pull/3001))
  - Support pasting multiple magnet links ([#3087](https://github.com/transmission/transmission/pull/3087), [#3086](https://github.com/transmission/transmission/pull/3086))
  - Add "Verify Local Data" to context menu ([#3025](https://github.com/transmission/transmission/pull/3025))
  - Fix build warnings, code cleanup ([#3222](https://github.com/transmission/transmission/pull/3222), [#3051](https://github.com/transmission/transmission/pull/3051), [#3031](https://github.com/transmission/transmission/pull/3031), [#3042](https://github.com/transmission/transmission/pull/3042), [#3059](https://github.com/transmission/transmission/pull/3059), [#3052](https://github.com/transmission/transmission/pull/3052), [#3041](https://github.com/transmission/transmission/pull/3041), [#2973](https://github.com/transmission/transmission/pull/2973))
  - Adopt lightweight generics ([#2974](https://github.com/transmission/transmission/pull/2974))
  - Fix 3.00 bug where the display window was incorrectly enabled on start ([#3056](https://github.com/transmission/transmission/pull/3056))
  - Add ⌘C support ([#3072](https://github.com/transmission/transmission/pull/3072))
  - Documentation improvements ([#2955](https://github.com/transmission/transmission/pull/2955), [#2975](https://github.com/transmission/transmission/pull/2975), [#2985](https://github.com/transmission/transmission/pull/2985), [#2986](https://github.com/transmission/transmission/pull/2986))
- [DevilDimon](https://github.com/DevilDimon) (Dmitry Serov):
  - Optimize framework imports ([#3323](https://github.com/transmission/transmission/pull/3323))
  - Use standard macOS toolbar buttons ([#3356](https://github.com/transmission/transmission/pull/3356))
  - macOS client code modernization ([#2453](https://github.com/transmission/transmission/pull/2453), [#509](https://github.com/transmission/transmission/pull/509))
- [federicobond](https://github.com/federicobond) (Federico Bond):
  - Replace deprecated NSRunAlertPanel call in Controller.m ([#1441](https://github.com/transmission/transmission/pull/1441))
- [floppym](https://github.com/floppym) (Mike Gilbert):
  - Restore support for the INSTALL_LIB option ([#1756](https://github.com/transmission/transmission/pull/1756))
- [fxcoudert](https://github.com/fxcoudert) (FX Coudert):
  - Update deprecated macOS code ([#3330](https://github.com/transmission/transmission/pull/3330))
  - Update links in macOS credits ([#3329](https://github.com/transmission/transmission/pull/3329))
  - Improve crash report debug information ([#2471](https://github.com/transmission/transmission/pull/2471))
  - Change accent color to match macOS red choice
  - Mac client uses freed memory ([#2234](https://github.com/transmission/transmission/pull/2234))
  - macOS: remove quitting badge ([#2495](https://github.com/transmission/transmission/pull/2495))
  - fix build warnings ([#3174](https://github.com/transmission/transmission/pull/3174))
- [GaryElshaw](https://github.com/GaryElshaw) (Gary Elshaw):
  - Icon updates ([#3178](https://github.com/transmission/transmission/pull/3178), [#3238](https://github.com/transmission/transmission/pull/3238), [#3130](https://github.com/transmission/transmission/pull/3130), [#31128](https://github.com/transmission/transmission/pull/31128), [#2779](https://github.com/transmission/transmission/pull/2779)), [#3239](https://github.com/transmission/transmission/pull/3239), [#3364](https://github.com/transmission/transmission/pull/3364), [#3348](https://github.com/transmission/transmission/pull/3348)
  - Font size changes ([#3571](https://github.com/transmission/transmission/pull/3571)
- [MaddTheSane](https://github.com/MaddTheSane) (Charles W. Betts):
  - Move private interfaces to interface extensions ([#932](https://github.com/transmission/transmission/pull/932))
  - macOS: use SDK's libCurl. ([#1542](https://github.com/transmission/transmission/pull/1542))
- [maxz](https://github.com/maxz) (Max Zettlmeißl):
  - Specify umask and IPC socket permission as strings ([#2984](https://github.com/transmission/transmission/pull/2984), [#3248](https://github.com/transmission/transmission/pull/3248))
  - Documentation improvements ([#2875](https://github.com/transmission/transmission/pull/2875), [#2889](https://github.com/transmission/transmission/pull/2889), [#2900](https://github.com/transmission/transmission/pull/2900))
- [nevack](https://github.com/nevack) (Dzmitry Neviadomski):
  - Revamp Toolbar Segments ([#3512](https://github.com/transmission/transmission/pull/3512))
  - Use NSAlert APIs to show alert suppression checkbox ([#3360](https://github.com/transmission/transmission/pull/3360))
  - Apply ObjC modernization ([#3358](https://github.com/transmission/transmission/pull/3358))
  - Hide NSPopover arrow for Torrent options ([#3294](https://github.com/transmission/transmission/pull/3294))
  - Fix group auto-assigning based on NSPredicate ([#3289](https://github.com/transmission/transmission/pull/3289))
  - Xcode wrangling ([#2141](https://github.com/transmission/transmission/pull/2141), [#3266](https://github.com/transmission/transmission/pull/3266), [#3267](https://github.com/transmission/transmission/pull/3267))
  - Update info window ([#2269](https://github.com/transmission/transmission/pull/2269))
  - Update blocklist downloader ([#2101](https://github.com/transmission/transmission/pull/2101), [#2191](https://github.com/transmission/transmission/pull/2191))
  - Fix macOS client deprecation warnings ([#2038](https://github.com/transmission/transmission/pull/2038), [#2074](https://github.com/transmission/transmission/pull/2074), [#2090](https://github.com/transmission/transmission/pull/2090), [#2113](https://github.com/transmission/transmission/pull/2113))
  - Fix CMake + Ninja builds on macOS ([#2036](https://github.com/transmission/transmission/pull/2036))
  - Update Preferences window sizing for Russian locale ([#3291](https://github.com/transmission/transmission/pull/3291))
  - Fix global popover clipping ([#3264](https://github.com/transmission/transmission/pull/3264))
  - Use DDG favicons service and migrate to NSURLSession [#3270](https://github.com/transmission/transmission/pull/3270))
- [Oleg-Chashko](https://github.com/Oleg-Chashko) (Oleg Chashko):
  - Fix *many* macOS UI issues ([#1923](https://github.com/transmission/transmission/pull/1923), [#1955](https://github.com/transmission/transmission/pull/1955), [#1973](https://github.com/transmission/transmission/pull/1973), [#1991](https://github.com/transmission/transmission/pull/1991), [#2008](https://github.com/transmission/transmission/pull/2008), [#1905](https://github.com/transmission/transmission/pull/1905), [#2013](https://github.com/transmission/transmission/pull/2013), [#2019](https://github.com/transmission/transmission/pull/2019))
  - Fix Tab selection in InfoWindow on Mojave ([#2599](https://github.com/transmission/transmission/pull/2599))
- [rsekman](https://github.com/rsekman) (Robin Seth Ekman):
  - fix daemon invocation regression: deprecated --log-error -> --log-level=error ([#3201](https://github.com/transmission/transmission/pull/3201))
- [SweetPPro](https://github.com/sweetppro) SweetPPro:
  - Unify the margin between headers and content on the Activity tab and the Options tab in the Info Window ([#3522](https://github.com/transmission/transmission/pull/3522))
  - macOS layout improvements ([#3326](https://github.com/transmission/transmission/pull/3326), [#3472](https://github.com/transmission/transmission/pull/3472), [#3497](https://github.com/transmission/transmission/pull/3497), [#3559](https://github.com/transmission/transmission/pull/3559), [#3604](https://github.com/transmission/transmission/pull/3604))
  - macOS disable green maximize button on Inspector window ([#3563](https://github.com/transmission/transmission/pull/3563))
  - OS fix clipping and crashing on the Info Inspector Options tab ([#3473](https://github.com/transmission/transmission/pull/3473))
  - macOS internationalization of xibs ([#3409](https://github.com/transmission/transmission/pull/3409))
  - Use the default toolbar button size on macOS 11+ ([#3433](https://github.com/transmission/transmission/pull/3433))
  - Remove gradient from PiecesView ([#3342](https://github.com/transmission/transmission/pull/3342))
  - macOS fix quicklook in the Torrent Inspector files view ([#3321](https://github.com/transmission/transmission/pull/3321))
  - macOS fullscreen and autosize fixes ([#3315](https://github.com/transmission/transmission/pull/3315))
  - macOS client icon improvements ([#3221](https://github.com/transmission/transmission/pull/3221), [#3113](https://github.com/transmission/transmission/pull/3113)
  - Update macOS group indicators ([#3183](https://github.com/transmission/transmission/pull/3183))
  - Fullscreen mode fixes ([#195](https://github.com/transmission/transmission/pull/195))
  - Fix for editing magnet links' tracker lists ([#2793](https://github.com/transmission/transmission/pull/2793))
  - Fix some window drawing issues [#3278](https://github.com/transmission/transmission/pull/3278))
  - Replace Groups indicators with dots. [#3268](https://github.com/transmission/transmission/pull/3268))
  - Fix a number of UI render issues in the torrent creator window [#3205](https://github.com/transmission/transmission/pull/3268))
  - Magnet link improvements ([#3205](https://github.com/transmission/transmission/pull/2654), [#2702](https://github.com/transmission/transmission/pull/2702))
- [wiz78](https://github.com/wiz78) (Simone Tellini):
  - Disable App Nap. ([#874](https://github.com/transmission/transmission/pull/874))
- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add support for creating torrents with a source flag ([#443](https://github.com/transmission/transmission/pull/443))

#### `GTK Client` code contributions:

- [bexnoss](https://github.com/bexnoss):
  - Fix conflicting hotkeys ([#2201](https://github.com/transmission/transmission/pull/2201), [#3720](https://github.com/transmission/transmission/pull/3720))
  - Remember 4 recent relocate folders ([#3691](https://github.com/transmission/transmission/pull/3691))
- [cpba](https://github.com/cpba) (Carles Pastor Badosa):
  - Add content_rating to appdata ([#1487](https://github.com/transmission/transmission/pull/1487))
- [jonasmalacofilho](https://github.com/jonasmalacofilho) (Jonas Malaco):
  - Accept dropping URLs from browsers onto the main window ([#2232](https://github.com/transmission/transmission/pull/2232))
- [meskoabalazs](https://github.com/meskobalazs) (Balázs Meskó):
  - Fix xgettext markup issues ([#3210](https://github.com/transmission/transmission/pull/3210))
- [noobsai](https://github.com/Noobsai):
  - Fixed showing popup menu on RMB at tray icon ([#1210](https://github.com/transmission/transmission/pull/1210))
  - Use correct variable for uploading speed ([#3560](https://github.com/transmission/transmission/pull/3560))
- [okias](https://github.com/okias) (David Heidelberg):
  - Use metainfo folder instead of appdata ([#2624](https://github.com/transmission/transmission/pull/2624))
- [orbital-mango](https://github.com/orbital-mango):
  - Allow piece size selection during torrent creation ([#3771](https://github.com/transmission/transmission/pull/3771))
  - Add missing accelerators in File menu ([#3213](https://github.com/transmission/transmission/pull/3213))
  - Add piece size selection when creating torrents ([#3145](https://github.com/transmission/transmission/pull/3145))
  - Show torrent-added date/time in Torrent Details dialog ([#3124](https://github.com/transmission/transmission/pull/3124))
- [sir-sigurd](https://github.com/sir-sigurd) (Sergey Fedoseev):
  - Add Sort by Queue menu item to popup menu ([#1040](https://github.com/transmission/transmission/pull/1040))
- [stefantalpalaru](https://github.com/stefantalpalaru) (Ștefan Talpalaru):
  - Add support for default public trackers ([#2668](https://github.com/transmission/transmission/pull/2668))
- [TimoPtr](https://github.com/TimoPtr) (Timothy Nibeaudeau):
  - Add support for running a script when done seeding ([#2621](https://github.com/transmission/transmission/pull/2621))
- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add a "Start Now" action for newly added torrent notifications ([#849](https://github.com/transmission/transmission/pull/849))
  - Add support for creating torrents with a source flag ([#443](https://github.com/transmission/transmission/pull/443))
  - Remove unnecessary "id" member of TrNotification in gtk/notify.c ([#851](https://github.com/transmission/transmission/pull/851))
- [lucaxvi](https://github.com/lucaxvi)
  - Use symbolic icons in status bar ([#3655](https://github.com/transmission/transmission/pull/3655))

#### `Qt Client` code contributions:

- [buckmelanoma](https://github.com/buckmelanoma):
  - Set stock icon for speed, options and statistics buttons in Qt ([#2179](https://github.com/transmission/transmission/pull/2179))
- [dubhater](https://github.com/dubhater):
  - Move icon after "Alternative Speed Limits" label ([#503](https://github.com/transmission/transmission/pull/503))
  - Use file selector in Set Location if session is local ([#502](https://github.com/transmission/transmission/pull/502))
  - Qt: Add tooltips for Options, Statistics buttons ([#501](https://github.com/transmission/transmission/pull/501))
- [GaryElshaw](https://github.com/GaryElshaw) (Gary Elshaw):
  - Icon updates ([#3377](https://github.com/transmission/transmission/pull/3377)
- [IMurzich](https://github.com/IMurzich):
  - Fix a progressBar direction for QT 5.15 for paused torrents ([#3739](https://github.com/transmission/transmission/pull/3739))
- [OscarCunningham](https://github.com/OscarCunningham):
  - Fix "sort by progress" for seeding torrents ([#3814](https://github.com/transmission/transmission/pull/3814))
- [saidinesh5](https://github.com/saidinesh5) (Dinesh Manajipet):
  - Feature: Support Batch Adding Tracker Urls in Qt UI ([#1161](https://github.com/transmission/transmission/pull/1161))
- [sewe2000](https://github.com/sewe2000) (Seweryn Pajor):
  - Show torrent-added date/time in Torrent Details dialog ([#3121](https://github.com/transmission/transmission/pull/3121))
- [stefantalpalaru](https://github.com/stefantalpalaru) (Ștefan Talpalaru):
  - Add support for default public trackers ([#2668](https://github.com/transmission/transmission/pull/2668))
- [TimoPtr](https://github.com/TimoPtr):
  - Add support for running a script when done seeding ([#2621](https://github.com/transmission/transmission/pull/2621))
- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add a "Start Now" action for newly added torrent notifications ([#849](https://github.com/transmission/transmission/pull/849))
  - Add support for creating torrents with a source flag ([#443](https://github.com/transmission/transmission/pull/443))
- [varesa](https://github.com/varesa) (Esa Varemo):
  - Add the option of auto-adding URLs from the clipboard on window focus ([#1633](https://github.com/transmission/transmission/pull/1633))


#### `Web Client` code contributions:

- [1100101](https://github.com/1100101) (Frank Aurich):
  - Fix display of size values in web interface ([#2703](https://github.com/transmission/transmission/pull/2703))
- [CodeWitchBella](https://github.com/CodeWitchBella) (Isabella Skořepová):
  - Add magnet link support to transmission web ([#2874](https://github.com/transmission/transmission/pull/2874))
- [deepwell](https://github.com/deepwell) (Mark Deepwell):
  - Fix webpack start dev server ([#2579](https://github.com/transmission/transmission/pull/2579))
- [jfredrickson](https://github.com/jfredrickson) (Jeff Fredrickson):
  - Allow decimal values in seedRatioLimit input ([#2618](https://github.com/transmission/transmission/pull/2618))
- [moben](https://github.com/moben) (Benedikt McMullin):
  - Fix sorting of renamed torrents by name ([#1892](https://github.com/transmission/transmission/pull/1892))
- [stefantalpalaru](https://github.com/stefantalpalaru) (Ștefan Talpalaru):
  - Add support for default public trackers ([#2668](https://github.com/transmission/transmission/pull/2668))
- [trainto](https://github.com/trainto) (Hakjoon Sim):
  - CSS fixes ([#2609](https://github.com/transmission/transmission/pull/2609))
  - Fix overflow menu should be closed when click it once again ([#1485](https://github.com/transmission/transmission/pull/1485))
- [ToKe79](https://github.com/ToKe79) (Tomáš Kelemen):
  - Add support for incomplete-dir ([#2183](https://github.com/transmission/transmission/pull/2183))
  - Add option to alter the download queue ([#2071](https://github.com/transmission/transmission/pull/2071))
- [vchimishuk](https://github.com/vchimishuk) (Viacheslav Chimishuk):
  - Fix broken state and labels filter ([#3311](https://github.com/transmission/transmission/pull/3311))
  - Add label support ([#2596](https://github.com/transmission/transmission/pull/2596), [#1406](https://github.com/transmission/transmission/pull/1406), [#3311](https://github.com/transmission/transmission/pull/3311))
  - Fix broken Cancel and Rename buttons on RenameDialog. ([#2577](https://github.com/transmission/transmission/pull/2577))
- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add support for creating torrents with a source flag ([#443](https://github.com/transmission/transmission/pull/443))


#### `transmission-daemon` code contributions:

- [candrews](https://github.com/candrews) (Craig Andrews):
  - Deny memory wx in transmission-daemon.service ([#2573](https://github.com/transmission/transmission/pull/2573))
- [catadropa](https://github.com/catadropa):
  - Small fixes to i18n markup formatting ([#2901](https://github.com/transmission/transmission/pull/2901))
- [ewtoombs](https://github.com/ewtoombs):
  - Wrote a guide on headless usage. ([#3049](https://github.com/transmission/transmission/pull/3049))
- [Dmitry Antipov](https://github.com/dmantipov):
  - C++ify transmission-daemon code ([#3767](https://github.com/transmission/transmission/pull/3767), [#3815](https://github.com/transmission/transmission/pull/3815))
  - Use signalfd-based signal handling if available ([#3778](https://github.com/transmission/transmission/pull/3778))
  - Do not `fsync()` log messages when they are written to a tty ([#3785](https://github.com/transmission/transmission/pull/3785))
- [FallenWarrior2k](https://github.com/FallenWarrior2k):
  - Delay start of daemon systemd service until network is configured ([#2721](https://github.com/transmission/transmission/pull/2721))
- [jelly](https://github.com/jelly) (Jelle van der Waa):
  - Add ProtectSystem and PrivateTmp to systemd service ([#1452](https://github.com/transmission/transmission/pull/1452))
- [piec](https://github.com/piec) (Pierre Carru):
  - add magnet file support to watchdir ([#1328](https://github.com/transmission/transmission/pull/1328))
- [rsekman](https://github.com/rsekman) (Robin Seth Ekman):
  - fix daemon invocation regression: deprecated --log-error -> --log-level=error ([#3201](https://github.com/transmission/transmission/pull/3201))
- [timtas](https://github.com/timtaAS) (Tim Tassonis):
  - add pid-file to transmission-daemon manpage ([#2784](https://github.com/transmission/transmission/pull/2784))


#### `utils` code contributions:

- [1100101](https://github.com/1100101) (Frank Aurich):
  - Fix size display for torrents larger than 4GB ([#2029](https://github.com/transmission/transmission/pull/2029))
- [BunioFH](https://github.com/BunioFH) (Michal Kubiak):
  - Allow multiple calls to --tracker-add/or --tracker-remove in transmission-remote ([#2284](https://github.com/transmission/transmission/pull/2284))
- [Kobaxidze256](https://github.com/Kobaxidze256):
  - Add a new flag in transmission-remote for connecting through a Unix domain socket ([#3552](https://github.com/transmission/transmission/pull/3552))
- [lajp](https://github.com/lajp) (Luukas Pörtfors):
  - Add renaming support in transmission-remote ([#2905](https://github.com/transmission/transmission/pull/2905))
- [LaserEyess](https://github.com/LaserEyess):
  - Add option to dump RPC responses as JSON ([#3819](https://github.com/transmission/transmission/pull/3819))
- [MatanZ](https://github.com/MatanZ) (Matan Ziv-Av):
  - Filtering torrents in transmission-remote ([#3125](https://github.com/transmission/transmission/pull/3125))
  - Allow control of transmission-show output ([#2825](https://github.com/transmission/transmission/pull/2825))
- [orbital-mango](https://github.com/orbital-mango):
  - transmission-edit can now edit the torrent 'source' tag ([#3760](https://github.com/transmission/transmission/pull/3760))
- [TimoPtr](https://github.com/TimoPtr):
  - Add seeding-done script to transmission-remote ([#2621](https://github.com/transmission/transmission/pull/2621))
- [vchimishuk](https://github.com/vchimishuk) (Viacheslav Chimishuk):
  - Fix Labels section in transmission-remote output. ([#2597](https://github.com/transmission/transmission/pull/2597))
- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add support for creating torrents with a source flag ([#443](https://github.com/transmission/transmission/pull/443))

### Documentation

- [aachang](https://github.com/acchang):
  - Added better documentation about Windows compatibility ([#3729](https://github.com/transmission/transmission/pull/3729))
- [BENICHN](https://github.com/BENICHN) (Nathan Benichou):
  - add status description to rpc-spec.txt ([#1760](https://github.com/transmission/transmission/pull/1760))
- [evils](https://github.com/evils):
  - man pages: remove commas in option listings ([#2204](https://github.com/transmission/transmission/pull/2204)
- [foobar-d](https://github.com/foobar-d) (beizmos):
  - Updated Port Forwarding guide ([#2847](https://github.com/transmission/transmission/pull/2847))
- [lluzpaz](https://github.com/luzpaz):
  - Fix various user-facing and non-user-facing typos ([#3743](https://github.com/transmission/transmission/pull/3743))
- [mlopezfm](https://github.com/mlopezfm) (Michael Lopez):
  - Fix man pages listing invalid cli options ([#2549](https://github.com/transmission/transmission/pull/2549))
- [Petrprogs](https://github.com/Petrprogs) (Pwrwe):
  - Lots of improvements in docs/, e.g. fixing broken markdown (
     [#3456](https://github.com/transmission/transmission/pull/3456),
     [#3475](https://github.com/transmission/transmission/pull/3475),
     [#3480](https://github.com/transmission/transmission/pull/3480),
     [#3490](https://github.com/transmission/transmission/pull/3490),
     [#3517](https://github.com/transmission/transmission/pull/3517),
     [#3518](https://github.com/transmission/transmission/pull/3518),
     [#3539](https://github.com/transmission/transmission/pull/3539),
     [#3555](https://github.com/transmission/transmission/pull/3555),
     [#3557](https://github.com/transmission/transmission/pull/3557),
     [#3564](https://github.com/transmission/transmission/pull/3564),
     [#3615](https://github.com/transmission/transmission/pull/3615),
     [#3622](https://github.com/transmission/transmission/pull/3622),
     [#3629](https://github.com/transmission/transmission/pull/3629),
     [#3655](https://github.com/transmission/transmission/pull/3655),
     [#3687](https://github.com/transmission/transmission/pull/3687),
    )
- [pedrinho](https://github.com/pedrinho) (Pedro Scarapicchia Junior):
  - Remove unused html ([#1313](https://github.com/transmission/transmission/pull/1313))
- [pborzenkov](https://github.com/pborzenkov) (Pavel Borzenkov):
  - fix typo in editDate arg description ([#1251](https://github.com/transmission/transmission/pull/1251))i
- [sammarcus](https://github.com/sammarcus):
  - Update README.md ([#459](https://github.com/transmission/transmission/pull/459))
- [shelvacu](https://github.com/shelvacu):
  - Explain port-forwarding-enabled in documentation ([#1900](https://github.com/transmission/transmission/pull/1900))
- [shenhanc78](https://github.com/shenhanc78) (Han Shen):
  - Add instruction in README.md to config a release build.([#1282](https://github.com/transmission/transmission/pull/1282))
- [shric](https://github.com/shric) (Chris Young):
  - Fix typo in rpc-spec.txt ([#1326](https://github.com/transmission/transmission/pull/1326))
- [tylergibbs2](https://github.com/tylergibbs2) (Tyler Gibbs):
  - document the Authentication header in rpc-spec.txt ([#1808](https://github.com/transmission/transmission/pull/1808))
- [vchimishuk](https://github.com/vchimishuk) (Viacheslav Chimishuk):
  - Add argument description to --labels line option in transmission-remote manpage ([#1364](https://github.com/transmission/transmission/pull/1364))
  - Add documentation for TR_CURL_SSL_NO_VERIFY environment variable. ([#3656](https://github.com/transmission/transmission/pull/3656))

