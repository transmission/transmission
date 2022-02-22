## Peer-Id

From version 0.80 onward, Transmission's peer-id is formatted Azureus style with one digit for the major version, two digits for the minor version, and one character to denote a stable release (`0`), nightly build (`Z`), or prerelease beta (`X`). For example:
  * `-TR1330-` &mdash; Official 1.33 release
  * `-TR133Z-` &mdash; Nightly build between 1.33 and 1.34
  * `-TR133X-` &mdash; Beta release of 1.34

Rationale: This differentiates between official and unofficial releases in a way easy for trackers to detect with simple string comparison. An official release (`-TR1330-`) is lexigraphically smaller than its post-release unsupported versions (`-TR133Z-` and `-TR133X-`), which in turn are lexigraphically smaller than the next official release (`-TR1340-`).

Before 0.80, versions of Transmission used two digits for the major version and two for the minor. For example, `-TR0072-` was Transmission 0.72.

## User-Agent

Its User-Agent header follows a similar format, plus the VCS revision in parenthesis:
  * Transmission/1.30X (6416) &mdash; Beta release leading up to version 1.30
  * Transmission/1.32 (6455) &mdash; Official 1.32 release
  * Transmission/1.32+ (6499) &mdash; Nightly build between 1.32 and 1.33
