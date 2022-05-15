## About

Transmission Ocular is a fork of the free BitTorrent client. Transmission comes in several flavors, but Transmission Ocular is purely for MacOS users.

Visit https://transmissionbt.com/ for more information about Transmission.

## Building

Transmission Ocular has an Xcode project file (Transmission.xcodeproj) for building in Xcode.

### Building Transmission Ocular from Git (first time)

    $ git clone https://github.com/GaryElshaw/transmission-ocular.git Transmission Ocular
    $ cd transmission-ocular
    $ git submodule update --init --recursive
    $ mkdir build
    $ cd build
    $ # Use -DCMAKE_BUILD_TYPE=RelWithDebInfo to build optimized binary.
    $ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
    $ make
    $ sudo make install

### Building Transmission Ocular from Git (updating)

    $ cd transmission-ocular/build
    $ make clean
    $ git submodule foreach --recursive git clean -xfd
    $ git pull --rebase --prune
    $ git submodule update --recursive
    $ # Use -DCMAKE_BUILD_TYPE=RelWithDebInfo to build optimized binary.
    $ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
    $ make
    $ sudo make install

