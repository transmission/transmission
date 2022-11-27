
> transmission-release-notes@0.0.1 render
> node lib/render.js

## Highlights

* Add support for GTK 4 ([#3916](https://github.com/transmission/transmission/pull/3916), GTK Client)
* Prefer ayatana-appindicator over appindicator, if present ([#4001](https://github.com/transmission/transmission/pull/4001), GTK Client)

## libtransmission (All Platforms)

* Fixed `4.0.0-beta.1` regression that could misformat the port forwarding log messages. ([#3911](https://github.com/transmission/transmission/pull/3911))
* Fixed `4.0.0-beta.1` regression that could crash when mixing IPv4 and IPv6 addresses in an IP blocklist. ([#4011](https://github.com/transmission/transmission/pull/4011))
* Better detection of preinstalled versions of libutp. ([#4072](https://github.com/transmission/transmission/pull/4072))
* Use a newer version of fast_float as a fallback when no preinstalled version can be found on the system. ([#4098](https://github.com/transmission/transmission/pull/4098))
* Fixed `4.0.0-beta.1` regression that could break serving web client files from Windows. ([#4099](https://github.com/transmission/transmission/pull/4099))
* Minor efficiency improvements in libtransmission. ([#4116](https://github.com/transmission/transmission/pull/4116), [#4216](https://github.com/transmission/transmission/pull/4216), [#4220](https://github.com/transmission/transmission/pull/4220), [#4224](https://github.com/transmission/transmission/pull/4224), [#4226](https://github.com/transmission/transmission/pull/4226))
* Improved DHT bootstrapping on startup. ([#4122](https://github.com/transmission/transmission/pull/4122))
* Made host lookups slightly more efficient for users that disable CA verification. ([#4159](https://github.com/transmission/transmission/pull/4159))
* Fix 4.0.0-beta.1 bug that returned an incorrect key in group-get RPC responses. ([#4171](https://github.com/transmission/transmission/pull/4171))
* Updated the mime-types list from https://github.com/jshttp/mime-db. ([#4246](https://github.com/transmission/transmission/pull/4246))

## macOS Client

* Support UserNotifications framework ([#3040](https://github.com/transmission/transmission/pull/3040))
* Fix "Capture of autoreleasing out parameter inside autorelease pool that may exit before method returns" ([#3886](https://github.com/transmission/transmission/pull/3886))
* Rollback #3871 ([#3889](https://github.com/transmission/transmission/pull/3889))
* macos: Hide Title (App Name) on BigSur and later. ([#3919](https://github.com/transmission/transmission/pull/3919))
* Sort order by activity is reversed ([#3924](https://github.com/transmission/transmission/pull/3924))
* Fixed various XCode and CMake build issues. ([#3940](https://github.com/transmission/transmission/pull/3940), [#3946](https://github.com/transmission/transmission/pull/3946), [#3951](https://github.com/transmission/transmission/pull/3951), [#4156](https://github.com/transmission/transmission/pull/4156), [#4195](https://github.com/transmission/transmission/pull/4195), [#4231](https://github.com/transmission/transmission/pull/4231), [#4234](https://github.com/transmission/transmission/pull/4234))
* feat: Align active filter macOS ([#3944](https://github.com/transmission/transmission/pull/3944))
* Fixed macOS API deprecation warnings. ([#3950](https://github.com/transmission/transmission/pull/3950), [#4112](https://github.com/transmission/transmission/pull/4112), [#4190](https://github.com/transmission/transmission/pull/4190), [#4221](https://github.com/transmission/transmission/pull/4221))
* Update to the message log window on macOS ([#3962](https://github.com/transmission/transmission/pull/3962))
* Remove preprocessor defines from macOS client ([#3974](https://github.com/transmission/transmission/pull/3974))
* macOS fix a potential hang when updating the blocklist ([#4010](https://github.com/transmission/transmission/pull/4010))
* Fix "User-facing text should use localized string macro" ([#4030](https://github.com/transmission/transmission/pull/4030))
* Fix "(arm64)  could not find object file symbol for symbol" ([#4031](https://github.com/transmission/transmission/pull/4031))
* macOS: Change BadgeView.mm so that up is up and down is down ([#4055](https://github.com/transmission/transmission/pull/4055))
* Auto enlarge search field when editing ([#4067](https://github.com/transmission/transmission/pull/4067))
* Fix "nil passed to a callee that requires a non-null 1st parameter" ([#4084](https://github.com/transmission/transmission/pull/4084))
* Fix: values above INT_MAX (68 years) are interpreted as negative values ([#4085](https://github.com/transmission/transmission/pull/4085))
* Adopt localizedStringWithFormat for displayed quantities ([#4109](https://github.com/transmission/transmission/pull/4109))
* ignoring deprecation warning on NSUnarchiver ([#4113](https://github.com/transmission/transmission/pull/4113))
* hook action in the xib ([#4135](https://github.com/transmission/transmission/pull/4135))
* Fixed `4.0.0-beta.1` Inspector selection bug. ([#4138](https://github.com/transmission/transmission/pull/4138))
* Fixed `4.0.0-beta.1` regression that could crash when displaying some torrents that contain invalid UTF-8. ([#4144](https://github.com/transmission/transmission/pull/4144))
* Fix create torrent out-of-range piece size ([#4145](https://github.com/transmission/transmission/pull/4145))
* Accessibility description for images ([#4149](https://github.com/transmission/transmission/pull/4149))
* Fixed missing `4.0.0-beta.1` macOS translations. ([#4161](https://github.com/transmission/transmission/pull/4161))
* Fix cmake CFBundleVersion and LSMinimumSystemVersion ([#4185](https://github.com/transmission/transmission/pull/4185))
* Appropriate and improve VDKQueue ([#4202](https://github.com/transmission/transmission/pull/4202))
* fixed a long-standing bug that could freeze the UI on startup while Time Machine was active. ([#4208](https://github.com/transmission/transmission/pull/4208))

## GTK Client

* Fixed `4.0.0-beta.1` regression that made the About dialog difficult to close. ([#3892](https://github.com/transmission/transmission/pull/3892))
* Don't show duplicate add/edit tracker error dialogs ([#3898](https://github.com/transmission/transmission/pull/3898))
* Add support for GTK 4 ([#3916](https://github.com/transmission/transmission/pull/3916))
* Make torrents context menu look more like one ([#3957](https://github.com/transmission/transmission/pull/3957))
* Don't scroll to message log bottom with no messages ([#3959](https://github.com/transmission/transmission/pull/3959))
* Fixed `4.0.0-beta.1` regression that could cause a crash after completing a download. ([#3963](https://github.com/transmission/transmission/pull/3963))
* use [libfmt](https://github.com/fmtlib/fmt) for string formatting. ([#3967](https://github.com/transmission/transmission/pull/3967))
* Increase message log window width, disable overflow menu ([#3971](https://github.com/transmission/transmission/pull/3971))
* Change progress bar color depending on torrent state (GTK client) ([#3976](https://github.com/transmission/transmission/pull/3976))
* Catch dbus exceptions in async callbacks on completion (GTK client) ([#3997](https://github.com/transmission/transmission/pull/3997))
* Prefer ayatana-appindicator over appindicator, if present ([#4001](https://github.com/transmission/transmission/pull/4001))
* Reload files list in details dialog, unless already loaded (GTK client) ([#4004](https://github.com/transmission/transmission/pull/4004))
* Adjust `Gio::File::query_info()` error handling ([#4079](https://github.com/transmission/transmission/pull/4079))
* Turned on more `clang-tidy` checks and fixed warnings. ([#4127](https://github.com/transmission/transmission/pull/4127), [#4137](https://github.com/transmission/transmission/pull/4137), [#4158](https://github.com/transmission/transmission/pull/4158), [#4160](https://github.com/transmission/transmission/pull/4160), [#4167](https://github.com/transmission/transmission/pull/4167), [#4174](https://github.com/transmission/transmission/pull/4174), [#4183](https://github.com/transmission/transmission/pull/4183))
* fix: eta display in GTK client details dialog ([#4227](https://github.com/transmission/transmission/pull/4227))

## Web Client

* Refresh web interface across desktop and mobile ([#3985](https://github.com/transmission/transmission/pull/3985))
* fix: chrome needs vendor prefix(-webkit-) for mask ([#4056](https://github.com/transmission/transmission/pull/4056))
* chore: get rid of webpack error while creating source map ([#4058](https://github.com/transmission/transmission/pull/4058))
* fix: open torrent dialog layout adjusted ([#4063](https://github.com/transmission/transmission/pull/4063))
* fix: inspector icon should be disabled as default ([#4093](https://github.com/transmission/transmission/pull/4093))
* fix: some style edited for chrome ([#4095](https://github.com/transmission/transmission/pull/4095))

## transmission-remote

* Fixed a `4.0.0-beta.1` regression that misformatted the display of a torrent's start time. ([#3909](https://github.com/transmission/transmission/pull/3909))
* improve and document the 'rename' command ([#3973](https://github.com/transmission/transmission/pull/3973))
* fixup! refactor: remove TR_PRIsv macros (#3842) ([#3992](https://github.com/transmission/transmission/pull/3992))

## transmission-create

* Fixed a `4.0.0-beta.1` regression that misformatted the display of the number of files in a torrent. ([#3996](https://github.com/transmission/transmission/pull/3996))

## Everything Else

* Improve libtransmission unit tests. ([#3812](https://github.com/transmission/transmission/pull/3812), [#4121](https://github.com/transmission/transmission/pull/4121), [#4170](https://github.com/transmission/transmission/pull/4170), [#4173](https://github.com/transmission/transmission/pull/4173))
* Updated documentation. ([#3904](https://github.com/transmission/transmission/pull/3904), [#3927](https://github.com/transmission/transmission/pull/3927), [#3982](https://github.com/transmission/transmission/pull/3982), [#4101](https://github.com/transmission/transmission/pull/4101))
* Bump deps versions used for Windows release builds ([#4092](https://github.com/transmission/transmission/pull/4092))
* Add CodeQL workflow ([#4125](https://github.com/transmission/transmission/pull/4125))
* Use a newer version of libb64 as a fallback when no preinstalled version can be found on the system. ([#4129](https://github.com/transmission/transmission/pull/4129))
* Turned on more `clang-tidy` checks and fixed warnings. ([#4131](https://github.com/transmission/transmission/pull/4131))

## Thank You!

Last but certainly not least, a big ***Thank You*** to these contributors:

### Contributions to `libtransmission (All Platforms)`:

* [@GermanAizek (Herman Semenov)](https://github.com/GermanAizek):
* [@depler (null)](https://github.com/depler):
  * Fixed `4.0.0-beta.1` regression that could misformat the port forwarding log messages. [#3911](https://github.com/transmission/transmission/pull/3911)
* [@sweetppro (SweetPPro)](https://github.com/sweetppro):
  * Fixed `4.0.0-beta.1` regression that could crash when mixing IPv4 and IPv6 addresses in an IP blocklist. [#4011](https://github.com/transmission/transmission/pull/4011)

### Contributions to `macOS Client`:

* [@DevilDimon (Dmitry Serov)](https://github.com/DevilDimon):
  * Code review for [#3886](https://github.com/transmission/transmission/pull/3886), [#4084](https://github.com/transmission/transmission/pull/4084)
  * Remove preprocessor defines from macOS client [#3974](https://github.com/transmission/transmission/pull/3974)
* [@GaryElshaw (Gary Elshaw)](https://github.com/GaryElshaw):
  * macOS: Change BadgeView.mm so that up is up and down is down [#4055](https://github.com/transmission/transmission/pull/4055)
* [@sweetppro (SweetPPro)](https://github.com/sweetppro):
  * Code review for [#3919](https://github.com/transmission/transmission/pull/3919), [#4055](https://github.com/transmission/transmission/pull/4055)
  * macOS fix a potential hang when updating the blocklist [#4010](https://github.com/transmission/transmission/pull/4010)
* [@uiryuu (Yuze Jiang)](https://github.com/uiryuu):
  * Update to the message log window on macOS [#3962](https://github.com/transmission/transmission/pull/3962)

### Contributions to `GTK Client`:

* [@elfring (Markus Elfring)](https://github.com/elfring):
  * Code review for [#3967](https://github.com/transmission/transmission/pull/3967)

### Contributions to `Web Client`:

* [@dareiff (Derek Reiff)](https://github.com/dareiff):
  * Refresh web interface across desktop and mobile [#3985](https://github.com/transmission/transmission/pull/3985)
  * Remove images and align cmakelist with reality [#4013](https://github.com/transmission/transmission/pull/4013)
* [@trainto (Hakjoon Sim)](https://github.com/trainto):
  * fix: chrome needs vendor prefix(-webkit-) for mask [#4056](https://github.com/transmission/transmission/pull/4056)
  * chore: get rid of webpack error while creating source map [#4058](https://github.com/transmission/transmission/pull/4058)
  * fix: open torrent dialog layout adjusted [#4063](https://github.com/transmission/transmission/pull/4063)
  * fix: inspector icon should be disabled as default [#4093](https://github.com/transmission/transmission/pull/4093)
  * fix: some style edited for chrome [#4095](https://github.com/transmission/transmission/pull/4095)

### Contributions to `transmission-remote`:

* [@lajp (Luukas Pörtfors)](https://github.com/lajp):
  * Fixed a `4.0.0-beta.1` regression that misformatted the display of a torrent's start time. [#3909](https://github.com/transmission/transmission/pull/3909)
  * improve and document the 'rename' command [#3973](https://github.com/transmission/transmission/pull/3973)

### Contributions to `transmission-create`:

* [@yarons (Yaron Shahrabani)](https://github.com/yarons):
  * Fixed a `4.0.0-beta.1` regression that misformatted the display of the number of files in a torrent. [#3996](https://github.com/transmission/transmission/pull/3996)

### Contributions to `Everything Else`:

* [@Kwstubbs (Kevin Stubbings)](https://github.com/Kwstubbs):
  * Add CodeQL workflow [#4125](https://github.com/transmission/transmission/pull/4125)
* [@dependabot[bot] (null)](https://github.com/apps/dependabot):
* [@ile6695 (Ilkka Kallioniemi)](https://github.com/ile6695):
  * Updated documentation. [#4101](https://github.com/transmission/transmission/pull/4101)
* [@pudymody (Federico Scodelaro)](https://github.com/pudymody):
  * Updated documentation. [#3927](https://github.com/transmission/transmission/pull/3927)

