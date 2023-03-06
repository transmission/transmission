## About

Transmission is a fast, easy, and free BitTorrent client. It comes in several flavors:
  * A native macOS GUI application
  * GTK+ and Qt GUI applications for Linux, BSD, etc.
  * A Qt-based Windows-compatible GUI application
  * A headless daemon for servers and routers
  * A web UI for remote controlling any of the above
  
Visit https://transmissionbt.com/ for more information.

## Documentation

[Transmission's documentation](docs/README.md) is currently out-of-date, but the team has recently begun a new project to update it and is looking for volunteers. If you're interested, please feel free to submit pull requests!

## Command line interface notes

Transmission is fully supported in transmission-remote, the preferred cli client.

Three standalone tools to examine, create, and edit .torrent files exist: transmission-show, transmission-create, and transmission-edit, respectively.

Prior to development of transmission-remote, the standalone client transmission-cli was created. Limited to a single torrent at a time, transmission-cli is deprecated and exists primarily to support older hardware dependent upon it. In almost all instances, transmission-remote should be used instead.

Different distributions may choose to package any or all of these tools in one or more separate packages.

## Building

Transmission has an Xcode project file (Transmission.xcodeproj) for building in Xcode.

For a more detailed description, and dependencies, visit [How to Build Transmission](docs/Building-Transmission.md) in docs

### Building a Transmission release from the command line

    $ tar xf transmission-3.00.tar.xz
    $ cd transmission-3.00
    $ mkdir build
    $ cd build
    # Use -DCMAKE_BUILD_TYPE=RelWithDebInfo to build optimzed binary with debug information. (preferred)
    # Use -DCMAKE_BUILD_TYPE=Release to build full optimized binary.
    $ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
    $ make
    $ sudo make install

### Building Transmission from the nightly builds

Download a tarball from https://build.transmissionbt.com/job/trunk-linux/ and follow the steps from the previous section.

If you're new to building programs from source code, this is typically easier than building from Git.

### Building Transmission from Git (first time)

    $ git clone https://github.com/transmission/transmission Transmission
    $ cd Transmission
    $ git submodule update --init --recursive
    $ mkdir build
    $ cd build
    # Use -DCMAKE_BUILD_TYPE=RelWithDebInfo to build optimzed binary with debug information. (preferred)
    # Use -DCMAKE_BUILD_TYPE=Release to build full optimized binary.
    $ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
    $ make
    $ sudo make install

### Building Transmission from Git (updating)

    $ cd Transmission/build
    $ make clean
    $ git submodule foreach --recursive git clean -xfd
    $ git pull --rebase --prune
    $ git submodule update --recursive
    # Use -DCMAKE_BUILD_TYPE=RelWithDebInfo to build optimzed binary with debug information. (preferred)
    # Use -DCMAKE_BUILD_TYPE=Release to build full optimized binary.
    $ cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
    $ make
    $ sudo make install

## Contributing

### Code Style

You would want to setup your editor to make use of the .clang-format file located in the root of this repository and the eslint/prettier rules in web/package.json.

If for some reason you are unwilling or unable to do so, there is a shell script which you can use: `./code_style.sh`

### Translations

See [language translations](docs/Translating.md).

## Sponsors

<table>
 <tbody>
  <tr>
   <td align="center"><img alt="[MacStadium]" src="https://uploads-ssl.webflow.com/5ac3c046c82724970fc60918/5c019d917bba312af7553b49_MacStadium-developerlogo.png" height="30"/></td>
   <td>macOS CI builds are running on a M1 Mac Mini provided by <a href="https://www.macstadium.com/opensource">MacStadium</a></td>
  </tr>
  <tr>
   <td align="center"><img alt="[SignPath]" src="https://avatars.githubusercontent.com/u/34448643" height="30"/></td>
   <td>Free code signing on Windows provided by <a href="https://signpath.io/">SignPath.io</a>, certificate by <a href="https://signpath.org/">SignPath Foundation</a></td>
  </tr>
 </tbody>
</table>
