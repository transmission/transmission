Transmission is fundamentally a BitTorrent client and communicates with other BitTorrent peers.
In addition to this Transmission supports network-based remote control, whereby an authorised user may control the Transmission core from the same or another machine via the [Transmission JSON RPC protocol](rpc-spec.md).
To make remote control easier from an arbitrary machine, Transmission core can also serve a JavaScript web application to any browser and which in turn makes JSON RPC calls back to the Transmission core.

The core components and methods of control are shown below:

![Architecture](https://transmission.github.io/wiki-images/Transmission_Architecture.gif)

From the above diagram it can be seen that a Transmission core may be controlled by the following:
 * _Local_ directly linked GUI (macOS, GTK+)
 * _Local_ or _remote_ Qt GUI
 * _Local_ or _remote_ command line utility
 * _Local_ or _remote_ Transmission web application running in a web browser

The multiple methods of controlling Transmission and the various native GUIs available result in several different Transmission products as shown in the figure below:

![Products](https://transmission.github.io/wiki-images/Transmission_Products.gif)

The products are:
 * Transmission desktop - macOS
 * Transmission desktop - Windows, Linux/Qt
 * Transmission desktop - Linux/GTK+
 * Transmission daemon (headless)
 * Transmission command line

The Transmission packages available on various distributions may include one or more of these components.
Note. Although the diagram shows "Transmission Desktop Qt" as being a Qt GUI with Transmission core, the Qt component may be packaged on its own as a purely remote tool.
