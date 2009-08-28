STATUS

   This application is very new and is in beta.  There may be bugs!
   Also, the name "QTransmission" is a placeholder.

VOLUNTEERS WANTED

   - If you find a bug, please report it at http://trac.transmissionbt.com/
   - New translations are encouraged
   - Windows devs: it would be interesting to see if/how this works on Windows
   - Suggestions for a better name than "QTransmission" would be good ;)
    
ABOUT QTRANSMISSION

   QTransmission is a GUI for Transmission loosely based on the GTK+ client.

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

BUILDING

   This currently is a little awkward.  We're working on it...

   1. Prerequisites: Qt >= 4.x and its development packages
   2. Build Transmission as normal
   3. If you want to use the OS'es libevent, edit qtr.pro:
      - LIBS += $${TRANSMISSION_TOP}/third-party/libevent/.libs/libevent.a
      + LIBS += -levent
   4. If you built Transmission without DHT, edit qtr.pro:
      - LIBS += $${TRANSMISSION_TOP}/third-party/dht/libdht.a
   5. In the qt/ directory, type "qmake-qt4 qtr.pro"
   6. In the qt/ directory, type "make"
   7. In the qt/ directory, as root, type "INSTALL_ROOT=/usr make install"
      (Feel free to replace /usr with /usr/local or /opt or whatever)

