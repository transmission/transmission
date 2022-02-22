# Port Forwarding Guide

[http://en.wikipedia.org/wiki/BitTorrent_%28protocol%29 BitTorrent] is a system that ''requires'' you to share the file you're downloading.

For this to be possible, it is required to be accessible from the internet. However, this isn't always as straight forward as it may seem. Because of the nature of the internet and security reasons, routers create a local network that makes your computer invisible to the internet. This technology is called [NAT](http://en.wikipedia.org/wiki/Network_address_translation).

## Open your local Firewall
To allow other peers to communicate with Transmission, you have to open your local firewall.
However, if you're behind a NAT (a router or DSL 'modem'), it is safe disable the firewall.

## On Mac OS X
bbLeopard ===
Upon opening Transmission for the first time, a Mac OS X dialogue box should appear asking if you will allow Transmission to receive incoming connections. Click Accept.

If this doesn't happen, you can add Transmission to Leopard's firewall manually:
 1. Open System Prefs >> Security >> Firewall. Make sure "Set access for specific services and applications" is selected.
 1. Click the "+" button and select Transmission from you Applications folder.
 1. Make sure the pull down menu is set to "Allow incoming connections".

## On Mac OS X 10.4 and Older

1. Open Transmission, go to Preferences >> Network and and note down the number for the port. Then quit Transmission.
1. Open System Prefs >> Sharing >> Firewall. Click "New." In the "Port Name" pop-up menu, select Other, and fill in the settings as follows:
  * TCP Port Number(s): the port you chose in step 1 - (default is 51413).
  * Description: Transmission
1. Click OK.

## On Unix

 * For instructions on how to use it, open a Terminal open the man page of your Firewall. (eg. 'man ufw')
 * You need to ensure that Transmission's port (displayed in Preferences) is opened in the firewall.

## Open ports & forwarding

To allow other peers to connect to you, you'll need to forward a port from the router to your computer.

### NAT-PMP / UPnP

By default Transmission will try to forward this port for you, using [UPnP]([http://en.wikipedia.org/wiki/UPnP) or [NAT-PMP](http://en.wikipedia.org/wiki/NAT-PMP).

Most routers manufactured since 2001 have either the UPnP or NAT-PMP feature.

 * Open Transmission.
 * Go to Preferences >> Network >> Ports, and check 'Forward port from router'.
 * If Transmission reports that the 'Port is open' then you have successfully port forwarded!

### Forward manually through a Router
 1. Find out what your IP address is.
  * On Mac OS X
   * Go to System Preferencess >> Network, double-clicking on your connection (for instance, Built-in Ethernet), and clicking the TCP/IP tab. The address is probably something like 192.168.1.100, or 10.0.1.2. The IP of your router is here too.
  * On Unix
   * In Ubuntu, right click the Network Manager applet in the menu bar, and select 'Connection Information'. The address is probably something like 192.168.1.2, or 10.0.1.2.
   * If you don't have Network Manager, open a Terminal and type 'ifconfig'. It will list information for each of your network devices. Find the one you are using, and use the number after 'inet addr:'.
 1. Open Transmission, go to preferences, and enter a number for the port. It is recommended you pick a random number between 49152 and 65535. The default is 51413. Then quit Transmission.
 1. Go into your router configuration screen. Normally this is done via your web browser using the address 192.168.0.1 etc.
 1. Find the port forwarding (sometimes called port mapping) screen. While the page will be different for each router generally you will enter something similar to the following:
 1. For 'Application' type 'Trans'.
 1. For 'Start Port' and 'End port' type in the port you chose in Step 2. (eg. 51.413).
 1. For Protocol, choose Both.
 1. For IP address, type in your IP address you found in Step 1. (eg. 192.168.1.2).
 1. Check Enable.
 1. Click save settings.

For more comprehensive instructions specific to your router, visit [portforward.com](http://www.portforward.com/english/routers/port_forwarding/routerindex.htm) and choose your router from the list.

== Verify ==
 1. Go to [CanYouSeeMe.org](http://www.canyouseeme.org/)
 1. Enter the Port Transmission uses
 1. If Transmission reports that the 'Port is open' then you have successfully forwarded the port!

== Common Problems ==
Go to the [Why is my port closed?](Why-is-my-port-closed.md) page.

