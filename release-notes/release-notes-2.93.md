## [Transmission 2.93](https://github.com/transmission/transmission/releases/tag/2.93) (2018-01-23)
### All Platforms
 * Fix CVE-2018-5702 ([#468](https://github.com/transmission/transmission/pull/468))
 * Fix crash on handshake if establishing DH shared secret fails ([#27](https://github.com/transmission/transmission/pull/27))
 * Fix crash when switching to next tracker during announcement ([#297](https://github.com/transmission/transmission/pull/297))
 * Fix potential issue during password salt extraction in OOM situation ([#141](https://github.com/transmission/transmission/pull/141))
 * Workaround `glib_DEFUN`- and `glib_REQUIRE`-related configuration issue ([#215](https://github.com/transmission/transmission/pull/215))
 * Fix building against OpenSSL 1.1.0+ ([#24](https://github.com/transmission/transmission/pull/24))
### Mac Client
 * Fix uncaught exception when dragging multiple items between groups ([#51](https://github.com/transmission/transmission/pull/51))
 * Don't hard-code libcrypto version to 0.9.8 in Xcode project ([#71](https://github.com/transmission/transmission/pull/71))
