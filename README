ABOUT

  Transmission is a fast, easy, and free BitTorrent client.
  It comes in several flavors:

    * A native Mac OS X GUI application
    * GTK+ and Qt GUI applications for Linux, BSD, etc.
    * A headless daemon for servers and routers
    * A web UI for remote controlling any of the above

  Visit http://www.transmissionbt.com/ for more information.

BUILDING

  Transmission has an Xcode project file (Transmission.xcodeproj)
  for building in Xcode.

  For a more detailed description, and dependancies, visit:
  http://trac.transmissionbt.com/wiki/

  Building a Transmission release from the command line:

    $ xz -d -c transmission-2.11.tar.xz | tar xf -
    $ cd transmission-2.11
    $ ./configure
    $ make
    $ sudo make install

  Building Transmission from the nightly builds:

    Download a tarball from http://build.transmissionbt.com/job/trunk-linux-inc/
    and follow the steps from the previous section.

    If you're new to building programs from source code, this is typically 
    easier than building from SVN.

  Building Transmission from SVN (First Time):

    $ svn co svn://svn.transmissionbt.com/Transmission/trunk Transmission
    $ cd Transmission
    $ ./autogen.sh
    $ make
    $ sudo make install

  Building Transmission from SVN (Updating):

    $ cd Transmission
    $ make clean
    $ svn up
    $ ./update-version-h.sh 
    $ make
    $ sudo make install

  Notes for building on Solaris' C compiler:  User av reports success with
  this invocation: ./configure CC=c99 CXX=CC CFLAGS='-D__EXTENSIONS__ -mt'
