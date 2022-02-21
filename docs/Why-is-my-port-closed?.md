Transmission needs an open port to communicate. You can open one manually or let transmission try to aquire one using NAT PMP or UPnP. If both automatic port mapping and manual forwarding failed

## Open ports & forwarding

For guide on how to open and forward your Transmission port, read the [[Port Forwarding|PortForwardingGuide]] guide. 

## Possible Problems

If you get the Message 'Port is closed'/'Port is Stealth' in the 'Ports' section of Transmission's preferences.

### Port check website is down

Transmission needs an external site to check wether the port is open. However, if that site is down, Transmission has no way to check wether the port is open or not. If you suspect this is the case, go to [CanYouSeeMe.org](http://www.canyouseeme.org/).

### UPnP / NAT-PMP

For UPnP/NAT-PMP compatible routers, make sure:
  * UPnP/NAT-PMP is enabled. Consult your router's documentation for instructions. If your router doesn't support UPnP/NAT-PMP, you will have to forward manually.
  * DMZ mode is disabled.
  * The port has not already been forwarded manually.
  * The port is not taken by another application
  * The port has been released properly by Transmission. In some rare situations Transmission won't be able to release the port so it can't be aquired by Transmission afterwards.

Note: NAT-PMP is only for Apple Airport routers.

### Double NAT

Another possible reason your port remains closed could be because your router is not the only device on the network which needs to be configured.

For example, your network might resemble the following: ADSL modem/router &rarr; Netgear Router &rarr; Laptop.

If you have multiple routers in your home network (such as in the example above), you have two options. The easiest way is to turn one of the routers into 'Bridge mode' which means you then only have to configure one device rather than all of them. So, in our above example, we would set the Netgear router to 'Bridge'. See your router's help documentation for instructions.

The second way is to map Transmission's port on all of the devices on your network. Transmission can only automatically port map the router the computer is directly connected to. Any others in between this router and your modem will have to be forwarded manually. For detailed instructions, visit [portforward.com](http://www.portforward.com/help/doublerouterportforwarding.htm).

Finally make sure your firewall is either disabled, or you have allowed Transmission's port. The firewall can cause the port to remain closed, even if it has been successfully mapped by the router(s).

### ISP Blocking Port

Though initially this was done to "combat viruses and spam", it is sometimes used to keep out "bandwidth hogs". Normally the default (51413) port is fine. However, it might be that an ISP does decide to block that port. In such a case it is recommended to pick a random number between 49152 and 65535. If you can't find a port that's open (check with [CanYouSeeMe.org](http://www.canyouseeme.org/)), you have a different issue.

### LAN ISP

eg. Universities, Wifi hotspots, RV parks and some 'true' ISPs (common in Rome, Italy).

Though these ISPs are often very interesting, offering high speeds, unlimited bandwidth (sometimes) and low prizes. This because they don't buy IP adresses for their clients and provide the (local) network themselves.

Basically the only thing you can do, is to ''politely'' ask for them to open (forward) a port for you.
