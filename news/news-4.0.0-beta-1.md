## [Transmission 4.0.0-beta.1](https://github.com/transmission/transmission/releases/tag/4.0.0-beta.1) (2022-mm-dd)

### Project

TODO

 - Transmission has migrated from C to C++ over the course of 1,100 commits since 3.00
 - Use CMake for building; autotools build files have been removed (#1465)
 - Use GTest for unit tests (#1383)
 - Update libevent to 2.1.12-stable (#1588)

### Core

TODO

- Remove the 1024 open files limit previously required by how libcurl was used (#893)
 - Add configurable anti-brute force settings (#1447)
 - Fetch metadata of stopped magnets ([#1080](https://github.com/transmission/transmission/pull/1080)
 - Dmitry Serov <barbari100@gmail.com> (Objective-C code modernization)
 - Rob Crowston <crowston@protonmail.com> (in-kernel file copying support)

### Mac Client

TODO

- Updates for macOS 11 Big Sur ([#1535](https://github.com/transmission/transmission/pull/1535)
  - Compressed toolbar
  - Updated toolbar icons (using SF Symbol)
- A wonderful new icon designed for Big Sur by Rodger Werner ([#1650](https://github.com/transmission/transmission/pull/1650)
- Upgrade Sparkle

### GTK+ Client

TODO

- Fix deprecation warnings. (#1370, #1380

### Qt Client

TODO

- Nicer error handling when duplicate torrents are added (#1410)
- More efficient use of RPC (#1234, #1322, #1333)
- More efficient state updates (#1334, #1335, #1336, #1428, #1430, #1432, #1433)
- Slightly more efficient RPC requests (#1373)
- Better caching of tracker favicons (#1402)
- Fix memory leaks (#1378)
- Fix FreeSpaceLabel craash (#1604)
- Add remote server version info in the About dialog (#1603)
- Support `TR_RPC_VERBOSE` environment variable for debugging RPC calls (#1435)

### Daemon

TODO

### Web Client

The web client has been given a major overhaul. (#1476)

User-visible highlights include:
* Mobile is now fully supported.
* Added fullscreen support on mobile.
* Better support for dark mode.
* Added mime icons to the torrent list.
* Improved theme consistency across the app.

Maintainer highlights include:
* Updated code to use ES6 APIs.
* No longer uses jQuery UI.
* No longer uses jQuery.
* Use Webpack to bundle the Javascript, CSS, and assets together -- the entire bundle size is now 68K gzipped.
* Added eslint / prettier / stylelint tooling.
* Uses torrent-get's 'table' mode for more efficient RPC calls.

### Utils
- Display more progress information during torrent creation in transmission-create (#1405)
- Improve man pages (#1364)

## [Transmission 3.00](https://github.com/transmission/transmission/releases/tag/3.00) (2020-05-03)

## Thank You

Last but certainly not least, a big **Thank You** to the people who contributed to this release.

TODO(ckerr): add contributors from https://github.com/transmission/transmission/commits/main?after=3fe2ed893f1958501634e7608e3df19a67e49c62+909&branch=main&qualified_name=refs%2Fheads%2Fmain and earlier

### Pull Requests

#### Contributons to the `Core`:

- [1100101](https://github.com/1100101) (Frank Aurich):
  - Fix overflow bugs on 32bit platforms with torrents > 4GB (#2391, #2378)
  - Fix RPC 'table' mode not being properly activated (#2058)
  - Fix RPC response payload when removing torrents (#2040)

- [anacrolix](https://github.com/anacrolix) (Matt Joiner):
  - Reject cancels when fast extension enabled (#2275)

- [lajp](https://github.com/lajp) (Luukas Pörtfors):
  - Add renaming support in transmission-remote (#2905)

- [AndreyPavlenko](https://github.com/AndreyPavlenko) (Andrey Pavlenko):
  - Use android logger on Android (#585)

- [bkkuhls](https://github.com/bkuhls):
  - fix cross-compile build error (#2576)

- [clickyotomy](https://github.com/clickyotomy) (Srinidhi Kaushik):
  - magnet link improvements incl. regression tests for invalid magnets (#2483)

- [dbeinder](https://github.com/dbeinder) (David Beinder):
  - Fix unreleased regression that broke RPC bitfield values (#2768)

- Dan Walters:
  - Apply optional peer socket TOS to UDP sockets (#1043)

- [dgcampea](https://github.com/dgcampea):
  - support DSCP classes in socket iptos (#2594)

- [goildsteinn](https://github.com/goldsteinn):
  - Performance improvements to bitfield.cc (#2933, #2950)

- [ile6695](https://github.com/ile6695) (Ilkka):
  - Add more decimals for low wratios (#2508)
  - Add regression tests for tr_strlcpy

- [johman10](https://github.com/johman10) (Johan):
  - Add total disk space to free-space RPC request (#1682)

- [JP-Ellis](https://github.com/JP-Ellis) (JP Ellis):
  - Fix detection of PSL library (#2812)

- [kakuhen](https://github.com/kakuhen):
  - Clarify documentation on torrent-add result on duplicate torrents (#2690)

- [LaserEyess](https://github.com/LaserEyess):
  - Add labels to torrent-add RPC method (#2539)
  - Support binding RPC to a Unix socket (#2574)
  - Add bind-address-ipv4 to UPnP (#845)

- [MatanZ](https://github.com/MatanZ) (Matan Ziv-Av):
  - Add support for bandwidth groups (#2761, #2818, #2852)

- [mhadam](https://github.com/mhadam) (Michael Hadam):
  - Add support for torrent-get calls with the key percentComplete (#2615)

- [miickaelifs](https://github.com/mickaelifs):
  - NAT-PMP private/public port and lifetime fix (#1602)

- [narthorn](https://github.com/Narthorn):
  - Don't follow symlinks when removing junk files (#1638)

- [neheb](https://github.com/neheb) (Rosen Penev):
  - fix runtime with wolfSSL and fastmath (#1950)

- [noobsai](https://github.com/Noobsai):
  - Fix building for XFS (#2192)

- [pyrovski](https://github.com/pyrovski) (Peter Bailey):
  - Allow stopping torrents in verification states (#715)

- [qu1ck](https://github.com/qu1ck):
  - Add label support to the core (#822, #868)
  - Fix unreleased fields handling regression in RPC (#2972)

- [razaq](https://github.com/razaqq):
  - add TR_TORRENT_TRACKERS env variable to script call (#2053)

- [RobCrowston](https://github.com/RobCrowston) (Rob Crowston):
  - Add in-kernel file copying for several platforms. (#1092)
  - Allow the OS to set the size of the listen queue connection backlog. (#922)
  - Refactor tr_torrentFindFile2() (#921)

- [sandervankasteel](https://github.com/sandervankasteel) (Sander van Kasteel):
  - Added primitive CORS header support (#1885)

- [stefantalpalaru](https://github.com/stefantalpalaru) (Ștefan Talpalaru):
  - Add support for default public trackers (#2668)

- [TimoPtr](https://github.com/TimoPtr):
  - Add support for running a script when done seeding (#2621)

- [uprt](https://github.com/uprt) (Kirill):
  - Replace NULL back with nullptr (mistake after auto-rebase) (#1933)
  - Slashes fixes (#857)

- [vjunk](https://github.com/vjunk):
  - Add support for adding torrents by raw hash values (#2608)

- [vuori](https://github.com/vuori):
  - Fixed wrong ipv6 addr used in announces. (#265)

#### Contributons to the `GTK Client`:

- [cpba](https://github.com/cpba) (Carles Pastor Badosa):
  - Add content_rating to appdata (#1487)

- [jonasmalacofilho](https://github.com/jonasmalacofilho) (Jonas Malaco):
  - Accept dropping URLs from browsers onto the main window (#2232)

- [meskoabalazs](https://github.com/meskobalazs) (Balázs Meskó):
  - Fix xgettext markup issues (#3210)

- [noobsai](https://github.com/Noobsai):
  - Fixed showing popup menu on RMB at tray icon (#1210)

- [okias](https://github.com/okias) (David Heidelberg):
  - Use metainfo folder instead of appdata (#2624)

- [orbital-mango](https://github.com/orbital-mango):
  - Add missing accelerators in File menu (#3213)
  - Add piece size selection when creating torrents (#3145)
  - Show torrent-added date/time in Torrent Details dialog (#3124)

- [stefantalpalaru](https://github.com/stefantalpalaru) (Ștefan Talpalaru):
  - Add support for default public trackers (#2668)

- [TimoPtr](https://github.com/TimoPtr):
  - Add support for running a script when done seeding (#2621)

- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add a "Start Now" action for newly added torrent notifications (#849)
  - Add support for creating torrents with a source flag (#443)
  - Remove unnecessary "id" member of TrNotification in gtk/notify.c (#851)


#### Contributons to the `Qt Client`:

- [buckmelanoma](https://github.com/buckmelanoma):
  - Set stock icon for speed, options and statistics buttons in Qt (#2179)

- [saidinesh5](https://github.com/saidinesh5) (Dinesh Manajipet):
  - Feature: Support Batch Adding Tracker Urls in Qt UI (#1161)

- [sewe2000](https://github.com/sewe2000) (Seweryn Pajor):
  - Show torrent-added date/time in Torrent Details dialog (#3121)

- [stefantalpalaru](https://github.com/stefantalpalaru) (Ștefan Talpalaru):
  - Add support for default public trackers (#2668)

- [TimoPtr](https://github.com/TimoPtr):
  - Add support for running a script when done seeding (#2621)

- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add a "Start Now" action for newly added torrent notifications (#849)
  - Add support for creating torrents with a source flag (#443)

- [varesa](https://github.com/varesa) (Esa Varemo):
  - Add the option of auto-adding URLs from the clipboard on window focus (#1633)


#### Contributons to the `Web Client`:

- [1100101](https://github.com/1100101) (Frank Aurich):
  - Fix display of size values in web interface (#2703)

- [CodeWitchBella](https://github.com/CodeWitchBella) (Isabella Skořepová):
  - Add magnet link support to transmission web (#2874)

- [deepwell](https://github.com/deepwell) (Mark Deepwell):
  - Fix webpack start dev server (#2579)

- [jfredrickson](https://github.com/jfredrickson) (Jeff Fredrickson):
  - allow decimal values in seedRatioLimit input (#2618)

- [moben](https://github.com/moben) (Benedikt McMullin):
  - fix sorting of renamed torrents by name (#1892)

- [stefantalpalaru](https://github.com/stefantalpalaru) (Ștefan Talpalaru):
  - Add support for default public trackers (#2668)

- [trainto](https://github.com/trainto) (Hakjoon Sim):
  - CSS fixes (#2609)

- [vchimishuk](https://github.com/vchimishuk) (Viacheslav Chimishuk):
  - Add label support (#2596, #1406)
  - Fix broken Cancel and Rename buttons on RenameDialog. (#2577)

- [ToKe79](https://github.com/ToKe79) (Tomáš Kelemen):
  - Add support for incomplete-dir (#2183)
  - Add option to alter the download queue (#2071)

- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add support for creating torrents with a source flag (#443)


#### Contributons to the `macOS Client`:

- [alimony](https://github.com/alimony) ((Markus Amalthea Magnuson):
  - Added ability to filter on error status. (#19)

- [azy5030](https://github.com/azy5030) (Ali):
  - Add ability to change piece size during torrent creation (#2416)

- [Coeur](https://github.com/Coeur) (A Cœur):
  - macOS client icon improvements (#3250, #3224, #3094, #3065)
  - Fix QuickLook (#3001)
  - Support pasting multimple magnet links (#3087, #3086)
  - Add "Verify Local Data" to context menu (#3025)\
  - Fix build warnings, code cleanup (#3222, #3051, #3031, #3042, #3059, #3052, #3041, #2973)
  - Adopting lightweight generics (#2974)
  - Fix 3.00 bug where the display window was incorrectly enabled on start (#3056)
  - Add ⌘C support (#3072)
  - Documentation improvements (#2955, #2975, #2985, #2986)

- [DevilDimon](https://github.com/DevilDimon) (Dmitry Serov):
  - macOS client code modernization (#2453, #509)

- [fxcoudert](https://github.com/fxcoudert) (FX Coudert):
  - Improve crash report debug information (#2471)
  - Change accent color to match macOS red choice
  - Mac client uses freed memory (#2234)
  - macOS: remove quitting badge (#2495)
  - fix build warnings (#3174)

- [GaryElshaw](https://github.com/GaryElshaw) (Gary Elshaw):
  - Update some app icons (#3178, #3238, #3130, #31128, #2779)

- [kvakvs](https://github.com/kvakvs) (Dmytro Lytovchenko):
  - C++ migration in libtransmission (#3108, #2010, #331927, #1917, #1914, #1895)

- [MaddTheSane](https://github.com/MaddTheSane) (C.W. Betts):
  - Move private interfaces to interface extensions (#932)
  - macOS: use SDK's libCurl. (#1542)

- [maxz](https://github.com/maxz) (Max Zettlmeißl):
  - Specify umask and IPC socket permission as strings (#2984, #3248)
  - Documentation improvements (#2875, #2889, #2900)

- [nevack](https://github.com/nevack) (Dzmitry Neviadomski):
  - Xcode wrangling (#2141, #3266, #3267)
  - Update info window (#2269)
  - Update blocklist downloader (#2101, #2191)
  - Fix macOS client deprecation warnings (#2038, #2074, #2090, #2113)
  - Fix CMake + Ninja builds on macOS (#2036)

- [Oleg-Chashko](https://github.com/Oleg-Chashko) (Oleg Chashko):
  - Fix *many* macOS UI issues (#1923, #1955, #1973, #1991, #2008, #1905, #2013, #2019)

- [rsekman](https://github.com/rsekman) (Robin Seth Ekman):
  - fix daemon invocation regression: deprecated --log-error -> --log-level=error (#3201)

- [SweetPPro](https://github.com/sweetppro) SweetPPro:
  - macOS client icon improvements (#3221, 3113)
  - Update macOS group indicators (#3183)
  - Fullscreen mode fixes (#195)
  - Fix for editing magnet links' tracker lists (#2793)
  - Magnet link improvements (#2654, #2702)

- [wiz78](https://github.com/wiz78) (Simone Tellini):
  - disable App Nap. (#874)

- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add support for creating torrents with a source flag (#443)


#### Contributons to the `Daemon`:

- [candrews](https://github.com/candrews) (Craig Andrews):
  - Deny memory wx in transmission-daemon.service (#2573)

- [catadropa](https://github.com/catadropa):
  - Small fixes to i18n markup formatting (#2901)

- [ewtoombs](https://github.com/ewtoombs):
  - Wrote a guide on headless usage. (#3049)

- [FallenWarrior2k](https://github.com/FallenWarrior2k):;
  - Delay start of daemon systemd service until network is configured (#2721)

- [jelly](https://github.com/jelly) (Jelle van der Waa):
  - Add ProtectSystem and PrivateTmp to systemd service (#1452)

- [piec](https://github.com/piec) (Pierre Carru):
  - add magnet file support to watchdir (#1328)

- [rsekman](https://github.com/rsekman) (Robin Seth Ekman):
  - fix daemon invocation regression: deprecated --log-error -> --log-level=error (#3201)

- [timtas](https://github.com/timtaAS) (Tim Tassonis):
  - add pid-file to transmission-daemon manpage (#2784)


#### Contributons to the `Utilities`:

- [1100101](https://github.com/1100101) (Frank Aurich):
  - Fix size display for torrents larger than 4GB (#2029)

- [BunioFH](https://github.com/BunioFH) (Michal Kubiak):
  - Allow multiple calls to --tracker-add/or --tracker-remove in transmission-remote (#2284)

- [evils](https://github.com/evils):;
  - man pages: remove commas in option listings #2204

- [lajp](https://github.com/lajp) (Luukas Pörtfors):
  - Add renaming support in transmission-remote (#2905)

- [MatanZ](https://github.com/MatanZ) (Matan Ziv-Av):
  - Filtering torrents in transmission-remote (#3125) 
  - Allow control of transmission-show output (#2825)

- [mlopezfm](https://github.com/mlopezfm) (Michael Lopez):
  - Fix man pages listing invalid cli options (#2549)

- [TimoPtr](https://github.com/TimoPtr):
  - Add seeding-done script to transmission-remote (#2621)

- [vchimishuk](https://github.com/vchimishuk) (Viacheslav Chimishuk):
  - Fix Labels section in transmission-remote output. (#2597)
  - Add argument description to --labels line option in transmission-remote manpage (#1364)

- [xavery](https://github.com/xavery) (Daniel Kamil Kozar):
  - Add support for creating torrents with a source flag (#443)

- [shelvacu](https://github.com/shelvacu):
  - Explain port-forwarding-enabled in documentation (#1900)
