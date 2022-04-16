Transmission needs an open port to communicate. You can open one manually or let Transmission try to acquire one using NAT PMP or UPnP, if both automatic port mapping and manual forwarding failed.

## Open ports and port forwarding
For guide on how to open and forward your Transmission port, read the [Port Forwarding](Port-Forwarding-Guide.md) guide.

## Possible Problems
If you get the message "Port is closed"/"Port is Stealth" in the "Ports" section of Transmission's preferences.

### Port check website is down
Transmission needs an external site to check whether the port is open. However, if that site is down, Transmission has no way to check whether the port is open or not. If you suspect this is the case, go to [CanYouSeeMe.org](https://www.canyouseeme.org/).

### UPnP / NAT-PMP
For UPnP/NAT-PMP compatible routers, make sure:
  * UPnP/NAT-PMP is enabled. Consult your router's documentation for instructions. If your router does not support UPnP/NAT-PMP, you will have to forward manually.
  * DMZ mode is disabled.
  * The port has not already been forwarded manually.
  * The port is not taken by another application
  * The port has been released properly by Transmission. In some rare situations Transmission will not be able to release the port so it cannot be acquired by another instance of Transmission afterwards.

Note: NAT-PMP is only for Apple Airport routers.

### Double NAT
Another possible reason your port remains closed could be because your router is not the only device on the network which needs to be configured.

For example, your network might resemble the following: ADSL modem/router &rarr; Netgear router &rarr; laptop.

If you have multiple routers in your home network (such as in the example above), you have two options. The easiest way is to turn one of the routers into 'Bridge mode' which means you then only have to configure one device rather than all of them. So, in our above example, we would set the Netgear router to 'Bridge'. See your router's help documentation for instructions.

The second way is to map Transmission's port on all of the devices on your network. Transmission can only automatically port map the router the computer is directly connected to. Any others in between this router and your modem will have to be forwarded manually. For detailed instructions, visit [portforward.com](https://www.portforward.com/help/doublerouterportforwarding.htm).

Finally make sure your firewall is either disabled or you have allowed Transmission's port. The firewall can cause the port to remain closed, even if it has been successfully mapped by the router(s).

### ISP blocking ports
Though initially this was done to "combat viruses and spam", it is sometimes used to keep out "bandwidth hogs". Normally the default (51413) port is fine. However, it might be that an ISP does decide to block that port. In such a case it is recommended to pick a random number between 49152 and 65535. If you cannot find a port that is open (check with [CanYouSeeMe.org](https://www.canyouseeme.org/)), you have a different issue.

### LAN ISP
E.g. universities, Wi-Fi hotspots, RV parks and some "true" ISPs (common in Rome, Italy).

Although these ISPs are often very interesting, offering high speeds, unlimited bandwidth (sometimes) and low prizes, this is because they do not buy IP addresses for their clients and provide the (local) network themselves.

Basically the only thing you can do, is to ''politely'' ask for them to forward a port for you.
