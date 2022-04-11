## Peer-ID
From version 0.80 onward, Transmission's peer-id is formatted in Azureus' style with one digit for the major version, two digits for the minor version and one character to denote a stable release (`0`), nightly build (`Z`) or prerelease beta (`X`). For example:
  * `-TR1330-` &mdash; Official 1.33 release
  * `-TR133Z-` &mdash; Nightly build between 1.33 and 1.34
  * `-TR133X-` &mdash; Beta release of 1.34

Rationale: This differentiates between official and unofficial releases in a way which is easy for trackers to detect with simple string comparison. An official release (`-TR1330-`) is lexicographically smaller than its post-release unsupported versions (`-TR133Z-` and `-TR133X-`), which in turn are lexicographically smaller than the next official release (`-TR1340-`).

Before 0.80, versions of Transmission used two digits for the major version and two for the minor version. For example, `-TR0072-` was Transmission 0.72.

## User-Agent
Its User-Agent header follows a similar format, plus the VCS revision in parentheses:
  * Transmission/1.30X (6416) &mdash; Beta release leading up to version 1.30
  * Transmission/1.32 (6455) &mdash; Official 1.32 release
  * Transmission/1.32+ (6499) &mdash; Nightly build between 1.32 and 1.33
