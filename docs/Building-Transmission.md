## Getting the Source ##
The source code for both official and nightly releases can be found on our [download page](https://transmissionbt.com/download/).

## On macOS ##
Transmission has an Xcode project file (Transmission.xcodeproj) for building in Xcode. Make sure you have this software:
 * macOS 10.14.4 or newer
 * Xcode 11.3.1 or newer

Building the project on Mac requires the source to be retrieved from GitHub. Pre-packaged source code will not compile.

If building from source is too daunting for you, check out the [nightly builds](https://build.transmissionbt.com/job/trunk-mac/).
(Note: These are untested snapshots. Use them with care.)

## On Unix ##
### Prerequisites ###
#### Ubuntu ####
On Ubuntu, you can install the required development tools with this command:

```console
$ sudo apt-get install build-essential automake autoconf libtool pkg-config intltool libcurl4-openssl-dev libglib2.0-dev libevent-dev libminiupnpc-dev libgtk-3-dev libappindicator3-dev
```

_After you install those you can skip [to this section](#building-from-a-tarball)._

#### Debian Squeeze ####
Sometimes you have a need to stay current with upstream releases, even though you would like to rely on the stability of your base distribution. Here is how this can be accomplished in a "quick and dirty" fashion. Lines starting with a # have to be executed as root, lines starting with $ should be run as a regular user.

1. Dependencies
   First let us install every dependency Transmission needs and for which there is a usable version in the Debian repository.
   ```console
   # apt-get install ca-certificates libcurl4-openssl-dev libssl-dev pkg-config build-essential checkinstall
   ```

2. libevent
   Traditionally libevent is also needed, but Transmission depends on version numbers only rarely found in Debian. So let us start by compiling libevent in a directory of your choice. Browse to https://libevent.org/ and get the latest version.
   ```console
   $ cd /var/tmp
   $ wget https://github.com/downloads/libevent/libevent/libevent-2.0.18-stable.tar.gz
   $ tar xf libevent-2.0.18-stable.tar.gz
   $ cd libevent-2.0.18-stable
   $ CFLAGS="-Os -march=native" ./configure && make
   ```

   Now, we would really like to be able to upgrade to a new version in the future, so there should be a mechanism other than the classic "make install" which keeps count of what went where (and ideally this is not a piece of paper). So we build a very simple Debian package from the compiled files and install it. Basically you just enter the following command and hit return until a nice text message tells you that all is done.

   ```console
   # checkinstall
   ```

3. Transmission
   Now we need to prepare Transmission for compilation by configuring the source, just as with libevent.

   ```console
   $ cd /var/tmp
   $ wget https://download-origin.transmissionbt.com/files/transmission-2.51.tar.bz2
   $ tar xf transmission-2.51.tar.bz2
   $ cd transmission-2.51
   $ CFLAGS="-Os -march=native" ./configure && make
   # checkinstall
   ```

_Thanks to josen at https://falkhusemann.de/blog/2012/05/compiling-transmission-bittorrent-for-debiand/ for the original Debian Squeeze HowTo section._

#### CentOS 5.4 ####
The packages you need are:
 * gcc
 * gcc-c++
 * m4
 * make
 * automake
 * libtool
 * gettext
 * openssl-devel

Or simply run the following command:
```console
$ yum install gcc gcc-c++ m4 make automake libtool gettext openssl-devel
```

However, Transmission needs other packages unavailable in `yum`:
 * [pkg-config](https://pkg-config.freedesktop.org/wiki/)
 * [libcurl](https://curl.haxx.se/)
 * [intltool](https://ftp.gnome.org/pub/gnome/sources/intltool/)

Before building Transmission, you need to set the pkgconfig environment setting:
```console
$ export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
```

_After you install those you can skip [to this section](#building-from-a-tarball)._

#### Normal ####
If this is your first time compiling on Unix, you will need a few basic tools:
 * gcc
 * libtool
 * gettext 0.14.1 or newer
 * intltool 0.40 or newer

If you are planning to build from source:
 * automake 1.9 or newer
 * autoconf 2.54 or newer

Once you have got the basics out of the way, here are the libraries that Transmission needs to have in order to build:
 * OpenSSL 0.9.8 or newer, preferably SSL or GnuTLS support.
 * libcurl 7.16.3 or newer
 * GTK+ 2.6 or newer (only needed by the GTK+ GUI)
 * libnotify 0.4.4 (optional and only needed by the GTK+ GUI)
 * DBUS 0.70 (optional and only needed by the GTK+ GUI)

#### RPM users ####

_You will also have to install the corresponding `-devel` packages._

### Building from a tarball ###
```console
$ tar xf transmission-1.76.tar.bz2
$ cd transmission-1.76
$ ./configure -q && make -s
# make install
```

### Building Transmission from Git (first time) ###
```console
$ git clone https://github.com/transmission/transmission Transmission
$ cd Transmission
$ git submodule update --init
$ mkdir build
$ cd build
$ # Use -DCMAKE_BUILD_TYPE=RelWithDebInfo to build optimized binary.
$ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
$ make
$ sudo make install
```

### Building Transmission from Git (updating) ###
```console
$ cd Transmission/build
$ make clean
$ git pull --rebase --prune
$ git submodule update
$ # Use -DCMAKE_BUILD_TYPE=RelWithDebInfo to build optimized binary.
$ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
$ make
$ sudo make install
```

## On Windows ##
For Windows XP and above there are several choices:

### Cygwin environment ###
With Cygwin https://cygwin.com/ installed, the CLI tools (transmission-remote, transmission-cli, etc.) and the daemon can be built easily.

No patches needed(\*), all the recent versions of Transmission build almost out-of-the-box (you need to install the prerequisites), and the CLI tools work better under Cygwin than those built with MinGW.

(\*) At the release time of version 2.0, **libevent** is not bundled and it is also not in the Cygwin distribution (but was added later)... so you need to build it (which is as easy as ./configure, make install). To build Transmission you may need to add LDFLAGS="-L/usr/local/lib" to the configure script (LIBEVENT_LIBS does not seem to work when it comes to build all the test programs).  Additionally **libutp** needs deleting -ansi on the Makefile.

With version 2.51 miniupnpc fails to build, see https://miniupnp.tuxfamily.org/forum/viewtopic.php?t#1130.

Version 2.80 breaks building on Cygwin, adding this https://github.com/adaptivecomputing/torque/blob/master/src/resmom/cygwin/quota.h file to Cygwin's /usr/include/sys solves the problem.  This is no longer needed after version 2.82 (Cygwin added the header).

Version 2.81 with the above workaround needs a one line patch, see ticket #5692.

Version 2.82, same as 2.81.

Version 2.83, no need to add quota.h, Cygwin added it.

### Native Windows ###
With a MinGW https://mingw.org/ development environment, the GTK and the Qt GUI applications can be built.  The CLI tools can also be built and in general work fine, but may fail if you use foreign characters as parameters (MinGW uses latin1 for parameters).

The procedure is documented at [Building Transmission Qt on Windows](https://trac.transmissionbt.com/wiki/BuildingTransmissionQtWindows).

## Switches ##
The Transmission `./configure` (or `./autogen.sh`) script allows you to switch on/off certain parts. To use these, you will either use `--enable-*` or `--disable-*`, e.g. to disable the GTK client: `--disable-gtk`.

The switches that are available are:
 * **gtk** = enables GTK+ client (default)
 * **daemon** = enables transmission-daemon and *-remote client (default)
 * **cli** = enables cli client (default. deprecated, consider using the daemon)
 * **libnotify** = enables lib notify (default)
 * **nls** = enables native language support (default)
 * **mac** = enables Mac client (default, if possible)
 * **wx** = enables wxWidgets client (unsupported)
 * **beos** = enables BeOS client (unsupported)

Note: _`--disable-nls` removes the dependency on gettext and intltool. It is designed for and should only be used on [HeadlessUsage embedded devices]. If you do have GTK+ installed on your box, you must also specify `--disable-gtk`._
