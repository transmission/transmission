VOLUNTEERS WANTED

   - Qt developers and translators are needed
   - If you find a bug, please report it at http://trac.transmissionbt.com/
    
ABOUT TRANSMISSION-QT

   Transmission-qt is a GUI for Transmission loosely based on the GTK+ client.

   This is the only Transmission client that can act as its own self-contained
   session (as the GTK+ and Mac clients do), and can also connect to a remote
   session (as the web client and transmission-remote terminal client do).

   Use Case 1: If you like to run BitTorrent for awhile from your desktop,
   then the Mac, GTK+, and Qt clients are a good match.

   Use Case 2: If you like to leave BitTorrent running nonstop on your
   computer or router, and want to control it from your desktop or
   from a remote site, then transmission-remote and the web and Qt clients
   are a good match.

   To use the Qt client as a remote, in the menu go to Edit > Change Session

   The Qt client is also the most likely to wind up running on Windows,
   though that's not a high priority at the moment...

BUILDING ON WINDOWS

   rb07 has a writeup of this on the Transmission wiki:
   https://trac.transmissionbt.com/wiki/BuildingTransmissionQtWindows

BUILDING ON OS X

   nnc has a writeup of this on the Transmission wiki:
   https://trac.transmissionbt.com/wiki/BuildingTransmissionQtMac

BUILDING ON UNIX

   1. Prerequisites: Qt >= 4.6 and its development packages
   2. Build Transmission as normal
   3. In the qt/ directory, type "qmake qtr.pro" or "qmake-qt4 qtr.pro"
   4. In the qt/ directory, type "make"
   5. In the qt/ directory, as root, type "INSTALL_ROOT=/usr make install"
      (Feel free to replace /usr with /usr/local or /opt or whatever)
 
