# Transmission 4.1.3

This is Transmission 4.1.3, a bugfix release. It fixes a potential
CRSF security issue for users who enable remote access to Transmission.
Users are encouraged to upgrade to this version.

## What's New in 4.1.3

### All Platforms

* Fixed a CORS bug that leaked the anti-CSRF nonce. ([#8938](https://github.com/transmission/transmission/pull/8938))
* Fixed a use-after-free bug in peer code. ([#8921](https://github.com/transmission/transmission/pull/8921))
* Fixed build error when compiling with [fmt](https://github.com/fmtlib/fmt) 12.2.0. ([#8942](https://github.com/transmission/transmission/pull/8942))

### Everything Else

* Fixed a `4.1.2` build error in tests. ([#8881](https://github.com/transmission/transmission/pull/8881))
