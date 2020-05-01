## About

Transmission is a fast, easy, and free BitTorrent client. It comes in several flavors:
  * A native Mac OS X GUI application
  * GTK+ and Qt GUI applications for Linux, BSD, etc.
  * A headless daemon for servers and routers
  * A web UI for remote controlling any of the above

Visit https://transmissionbt.com/ for more information.

## Command line interface notes

Transmission is fully supported in transmission-remote, the preferred cli client.

Three standalone tools to examine, create, and edit .torrent files exist: transmission-show, transmission-create, and transmission-edit, respectively.

Prior to development of transmission-remote, the standalone client transmission-cli was created. Limited to a single torrent at a time, transmission-cli is deprecated and exists primarily to support older hardware dependent upon it. In almost all instances, transmission-remote should be used instead.

Different distributions may choose to package any or all of these tools in one or more separate packages.

## Building

Transmission has an Xcode project file (Transmission.xcodeproj) for building in Xcode.

For a more detailed description, and dependencies, visit: https://github.com/transmission/transmission/wiki

### Building a Transmission release from the command line

    $ tar xf transmission-2.92.tar.xz
    $ cd transmission-2.92
    $ mkdir build
    $ cd build
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

## Contributing

### Code Style

You would want to setup your editor to make use of uncrustify.cfg and .jsbeautifyrc configuration files located in the root of this repository.

If for some reason you are unwilling or unable to do so, there is a shell script which you could run either directly or via docker-compose:

    $ ./code_style.sh
    or
    $ docker-compose build --pull
    $ docker-compose run --rm code_style
