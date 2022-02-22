Transmission is fundamentally a Bit Torrent client and communicates with other Bit Torrent peers.
In addition to this Transmission supports network-based remote control, whereby an authorised user may control the Transmission core from another machine via the [[Transmission JSON RPC protocol|rpc]].
To make remote control easier from an arbitrary machine, Transmission Core can also serve a javascript web application to any browser and which in turn makes JSON RPC calls back to the Transmission core.

The Core components and methods of control are shown below:

![Architecture](https://transmission.github.io/wiki-images/Transmission_Architecture.gif)

From the above diagram it can be seen that a Transmission Core may be controlled by the following:
 * _Local_ directly linked GUI (OS X, GTK+)
 * _Local_ or _Remote_ Qt GUI
 * _Local_ or _Remote_ Command Line Utility
 * _Local_ or _Remote_ Transmission Web Application running in a web browser

The multiple methods of controlling Transmission and the various native GUIs available result in several different Transmission products as shown in the figure below:

![Products](https://transmission.github.io/wiki-images/Transmission_Products.gif)

The products are:
 * Transmission Desktop - OS X
 * Transmission Desktop - Windows, Linux/Qt
 * Transmission Desktop - Linux/GTK+
 * Transmission Daemon (headless)
 * Transmission Command Line

The Transmission packages available on various distributions may include one or more of these components.
Note. Although the diagram shows "Transmission Desktop Qt" as being Qt GUI + Transmission Core, the Qt component may be packaged on its own as a purely remote tool.