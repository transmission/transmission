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
$ sudo apt install git build-essential cmake libcurl4-openssl-dev libssl-dev python3
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
$ sudo apt-get install build-essential automake autoconf libtool pkg-config intltool libcurl4-openssl-dev libglib2.0-dev libevent-dev libminiupnpc-dev libgtk-3-dev libappindicator3-dev libssl-dev
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

### Prerequisites
You need the following installed:

* [Visual Studio 2019 or greater](https://visualstudio.microsoft.com/downloads/) (the Community Edition is sufficient)
    * install the "Desktop Development with C++" workload
    * install the ATL and MFC components (only needed by the Qt client)
* [CMake](https://cmake.org/download/) (choose to add CMake to your path)
* [Git for Windows](https://git-scm.com/download/win)
* [Vcpkg](https://github.com/microsoft/vcpkg#quick-start-windows)
* [Python](https://python.org/downloads)


### Install dependencies through vcpkg

Vcpkg will install x86 libraries by default. To install x64 add the `--triplet=x64-windows` flag at the end of the commands below.

Common dependencies:
```
vcpkg install curl zlib openssl
```

Additional dependencies for the Qt client:
```
vcpkg install qt5-tools qt5-winextras
```

### Get Transmission source
```
git clone https://github.com/transmission/transmission
cd transmission
git submodule update --init --recursive
```

### Configure CMake and build the project

To configure which components are built use the flags below.
Each option can be set to `ON` or `OFF`, values shown below are the defaults.
* `-DENABLE_DAEMON=ON` - build transmission daemon
* `-DENABLE_QT=AUTO` - build the Qt client
* `-DENABLE_UTILS=ON` - build transmission-remote, transmission-create, transmission-edit and transmission-show cli tools
* `-DENABLE_CLI=OFF` - build the cli client

```
cmake -B build -DCMAKE_TOOLCHAIN_FILE="<path-to-vcpkg>\scripts\buildsystems\vcpkg.cmake" <flags-from-above> <other-cmake-configurations>
```

To build the project run:
```
cmake --build build
```
