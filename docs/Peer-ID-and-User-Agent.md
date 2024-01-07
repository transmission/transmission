## Peer-ID

Transmission's peer-ids follow Azureus' style with code `TR`. Peer-id prefix is `-TR????-`, where `????` are four bytes encoding the client's version. Client version encoding changed over the client's history.

### Current scheme

Starting from version 3.00, peer-ids are formatted `-TRXYZR-` where `X`, `Y`, `Z` are [base62](https://en.wikipedia.org/wiki/Base62) major, minor, patch version numbers, and `R` is one of `0` (stable release), `B` (beta release), or `Z` (dev build). For example:

* `-TR40aZ-` &mdash; 4.0.36 Dev
* `-TR400B-` &mdash; 4.0.0 Beta
* `-TR4A00-` &mdash; 4.11.0

The suffix scheme was changed after 3.00 for consistency with other clients.

### 0.80 up to 3.00

From version 0.80 up to 3.00, Transmission's peer-ids were formatted `-TRXYYR-`, where `X` was one base10 digit for the major version and `YY` were two base10 digits for the minor version. `R` was a suffix denoting a stable release (`0`), nightly build (`Z`), or prerelease beta (`X`). For example:

* `-TR133Z-` &mdash; Nightly build between 1.33 and 1.34
* `-TR133X-` &mdash; 1.34 Beta
* `-TR1330-` &mdash; 1.33

Rationale at the time: this differentiates between official and unofficial releases in a way which is easy for trackers to detect with simple string comparison. An official release (`-TR1330-`) is lexicographically smaller than its post-release unsupported versions (`-TR133Z-` and `-TR133X-`), which in turn are lexicographically smaller than the next official release (`-TR1340-`).

### Before 0.80

Before 0.80, Transmission used two base10 digits for the major version and two base10 digits for the minor version. For example:

* `-TR0072-` &mdash; 0.72
* `-TR0006-` &mdash; 0.6

## User-Agent

Its User-Agent header follows a similar format, plus the VCS revision in parentheses:

  * Transmission/1.30X (6416) &mdash; Beta release leading up to version 1.30
  * Transmission/1.32 (6455) &mdash; Official 1.32 release
  * Transmission/1.32+ (6499) &mdash; Nightly build between 1.32 and 1.33
