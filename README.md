## About

Transmission is a fast, easy, and free BitTorrent client. It comes in several flavors:
  * A native Mac OS X GUI application
  * GTK+ and Qt GUI applications for Linux, BSD, etc.
  * A headless daemon for servers and routers
  * A web UI for remote controlling any of the above

Visit https://transmissionbt.com/ for more information.

## Building

Transmission has an Xcode project file (Transmission.xcodeproj) for building in Xcode.

For a more detailed description, and dependencies, visit: https://github.com/transmission/transmission/wiki

### Building a Transmission release from the command line

    $ tar xf transmission-2.92.tar.xz
    $ cd transmission-2.92
    $ mkdir build
    $ cmake ..
    $ make
    $ sudo make install

### Building Transmission from the nightly builds

Download a tarball from https://build.transmissionbt.com/job/trunk-linux/ and follow the steps from the previous section.

If you're new to building programs from source code, this is typically easier than building from Git.

### Building Transmission from Git (first time)

    $ git clone https://github.com/transmission/transmission Transmission
    $ cd Transmission
    $ git submodule update --init
    $ mkdir build
    $ cd build
    $ cmake ..
    $ make
    $ sudo make install

### Building Transmission from Git (updating)

    $ cd Transmission/build
    $ make clean
    $ git pull --rebase --prune
    $ git submodule update
    $ cmake ..
    $ make
    $ sudo make install
