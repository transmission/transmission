# libutp - The uTorrent Transport Protocol library.
Copyright (c) 2010 BitTorrent, Inc.

uTP is a TCP-like implementation of [LEDBAT][ledbat] documented as a BitTorrent
extension in [BEP-29][bep29]. uTP provides provides reliable, ordered delivery
while maintaining minimum extra delay. It is implemented on top of UDP to be
cross-platform and functional today. As a result, uTP is the primary transport
for uTorrent peer-to-peer connections.

uTP is written in C++, but the external interface is strictly C (ANSI C89).

## The Interface

The uTP socket interface is a bit different from the Berkeley socket API to
avoid the need for our own select() implementation, and to make it easier to
write event-based code with minimal buffering.

When you create a uTP socket, you register a set of callbacks. Most notably, the
on_read callback is a reactive callback which occurs when bytes arrive off the
network. The write side of the socket is proactive, and you call UTP_Write to
indicate the number of bytes you wish to write. As packets are created, the
on_write callback is called for each packet, so you can fill the buffers with
data.

The libutp interface is not thread-safe. It was designed for use in a
single-threaded asyncronous context, although with proper synchronization
it may be used from a multi-threaded environment as well.

See utp.h for more details and other API documentation.

## Examples

See the utp_test and utp_file directories for examples.

## Building

uTP has been known to build on Windows with MSVC and on linux and OS X with gcc.
On Windows, use the MSVC project files (utp.sln, and friends). On other platforms,
building the shared library is as simple as:

    make

To build one of the examples, which will statically link in everything it needs
from libutp:

    cd utp_test && make

## License

libutp is released under the [MIT][lic] license.

## Related Work

Research and analysis of congestion control mechanisms can be found [here.][survey]

[ledbat]: http://datatracker.ietf.org/wg/ledbat/charter/
[bep29]: http://www.bittorrent.org/beps/bep_0029.html
[lic]: http://www.opensource.org/licenses/mit-license.php
[survey]: http://datatracker.ietf.org/doc/draft-ietf-ledbat-survey/
