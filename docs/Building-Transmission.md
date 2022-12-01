## Getting the Source ##
The source code for both official and nightly releases can be found on our [download page](https://transmissionbt.com/download/).

## On macOS ##
Software prerequisites:
 * macOS 10.14.4 or newer
 * Xcode 11.3.1 or newer

Building the project on Mac requires the source to be retrieved from GitHub. Pre-packaged source code will not compile.
```console
git clone --recurse-submodules https://github.com/transmission/transmission Transmission
```

If building from source is too daunting for you, check out the [nightly builds](https://build.transmissionbt.com/job/trunk-mac/).
(Note: These are untested snapshots. Use them with care.)

### Building the native app with Xcode ###
Transmission has an Xcode project file for building in Xcode.
- Open Transmission.xcodeproj
- Run the Transmission scheme

### Building the native app with ninja ###
Build the app:
```console
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build transmission-mac
open ./build/macosx/Transmission.app
```

### Building the GTK app with ninja ###
Install GTK and build the app:
```console
brew install gtk4 gtkmm4
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_GTK=ON -DENABLE_MAC=OFF
ninja -C build transmission-gtk
./build/gtk/transmission-gtk
```

## On Unix ##
### Prerequisites ###

#### Debian 11 / Bullseye ####
On Debian, you can build transmission with a few dependencies on top of a base installation.

For building transmission-daemon you will need basic dependencies
```console
$ sudo apt install git build-essential cmake libcurl4-openssl-dev libssl-dev
```
You likely want to install transmission as a native GUI application. There are two options, GTK and QT.

For GTK 3 client, two additional packages are required
```console
$ sudo apt install libgtkmm-3.0-dev gettext
```
For QT client, one additional package is needed on top of basic dependencies
```console
$ sudo apt install qttools5-dev
```

Then you can begin [building.](#building-transmission-from-git-first-time)

#### Ubuntu ####
On Ubuntu, you can install the required development tools for GTK with this command:

```console
$ sudo apt-get install build-essential automake autoconf libtool pkg-config intltool libcurl4-openssl-dev libglib2.0-dev libevent-dev libminiupnpc-dev libgtk-3-dev libappindicator3-dev
```

Then you can begin [building.](#building-transmission-from-git-first-time)

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

### Building Transmission from Git (first time) ###
```console
$ git clone https://github.com/transmission/transmission Transmission
$ cd Transmission
$ git submodule update --init --recursive
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
$ git submodule update --recursive
$ # Use -DCMAKE_BUILD_TYPE=RelWithDebInfo to build optimized binary.
$ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
$ make
$ sudo make install
```

## On Windows ##

## Building transmission-daemon
You need the following installed:

* Visual Studio 2017 (the Community Edition is sufficient - just make sure its C[++] compiler, MSVC, is installed)
* [ActivePerl](https://www.activestate.com/products/activeperl/) or [StrawberryPerl](https://strawberryperl.com)
* [CMake](https://cmake.org/download/) (choose to add CMake to your path)
* Possibly [Git for Windows](https://git-scm.com/download/win) to have tools like `patch` present
* [jom](https://wiki.qt.io/Jom) (very recommended, esp. for OpenSSL compilation which can take ages on a single core. If you want to use `nmake`, use "NMake Makefiles" as the CMake generator & don't add `/FS` to CFLAGS)

### Set up the environment

Open a Command Prompt and run the following:

```
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
set "TPDIR=C:\3rd-party-msvc64"
set "PATH=%TPDIR%\bin;%PATH%"
set "TPCFLAGS=/nologo /MP /GS /GL /Gy /Oi /Oy /O2 /DWIN32 /D_WINDOWS"
set "TPLDFLAGS=/NOLOGO /DYNAMICBASE /NXCOMPAT /LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF /machine:x64"
set "CFLAGS=%TPCFLAGS%"
set "LDFLAGS=%TPLDFLAGS%"

mkdir "%TPDIR%" & cd "%TPDIR%"
```

### Build zlib

Run the following to build and install zlib into %TPDIR%:

```
mkdir build & cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%TPDIR%" -DCMAKE_SHARED_LINKER_FLAGS="%LDFLAGS%" -DCMAKE_C_FLAGS="%CFLAGS%" -DBUILD_SHARED_LIBS=ON -DSKIP_INSTALL_FILES=ON -DAMD64=ON
nmake
nmake install/fast
```

### Build OpenSSL

[Download OpenSSL](https://www.openssl.org/source/), extract the contents of it into %TPDIR% and **`cd` into the extracted OpenSSL folder**.

Run the following to build and install OpenSSL into %TPDIR%:

```
set "CFLAGS=%TPCFLAGS% /I"%TPDIR%\include" /FS"
set "LDFLAGS=%TPLDFLAGS% /DEBUG:FASTLINK"
perl Configure --prefix="%TPDIR%" --openssldir="C:\Program Files\Transmission\ssl" VC-WIN64A-masm zlib-dynamic
REM possibly install dmake - I did
jom
nmake install_sw
set "CFLAGS=%TPCFLAGS%"
set "LDFLAGS=%TPLDFLAGS%"
```

### Build curl

[Download curl](https://curl.haxx.se/download.html), extract the contents of it into %TPDIR% and **`cd` into the extracted curl folder**.

Run the following to build and install curl into %TPDIR%:

```
mkdir build & cd build
cmake .. -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%TPDIR%" -DCMAKE_SHARED_LINKER_FLAGS="%LDFLAGS%" -DCMAKE_EXE_LINKER_FLAGS="%LDFLAGS%" -DCMAKE_C_FLAGS="%CFLAGS%" -DBUILD_CURL_EXE=OFF -DUSE_WIN32_LDAP=OFF -DBUILD_TESTING=OFF -DCMAKE_USE_OPENSSL=ON -DCURL_WINDOWS_SSPI=OFF -DBUILD_TESTING=OFF -DCURL_DISABLE_DICT=ON -DCURL_DISABLE_GOPHER=ON -DCURL_DISABLE_IMAP=ON -DCURL_DISABLE_SMTP=ON -DCURL_DISABLE_POP3=ON -DCURL_DISABLE_RTSP=ON -DCURL_DISABLE_TFTP=ON -DCURL_DISABLE_TELNET=ON -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DENABLE_MANUAL=OFF
jom
nmake install/fast
```

### Build Transmission

Download Transmission, be this a tarball from the official website or Git master and **`cd` into the extracted transmission folder**. 

Run the following to build and install Transmission into %TPDIR%

```
set "CXXFLAGS=%TPCFLAGS% /GR /EHsc"
mkdir build & cd build
cmake .. -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%TPDIR%" -DCMAKE_SHARED_LINKER_FLAGS="%LDFLAGS%" -DCMAKE_STATIC_LINKER_FLAGS="/NOLOGO /LTCG /machine:x64" -DCMAKE_EXE_LINKER_FLAGS="%LDFLAGS%" -DCMAKE_C_FLAGS="%CFLAGS%" -DCMAKE_CXX_FLAGS="%CXXFLAGS%" -DENABLE_CLI=ON -DENABLE_GTK=OFF -DENABLE_QT=OFF -DENABLE_MAC=OFF -DENABLE_TESTS=OFF -DINSTALL_DOC=OFF
jom
nmake install/fast
```

You should now have Transmission and the DLL files required to run it in %TPDIR%\bin.

You can replace the files in your installed version of Transmission with the ones from bin. Don't forget to do the same with %TPDIR%\share\transmission\web.
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
