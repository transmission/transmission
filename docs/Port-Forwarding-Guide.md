# Port forwarding guide
[BitTorrent](https://en.wikipedia.org/wiki/BitTorrent_protocol) is a peer-to-peer protocol which allows users to send and receive bits of files without the need of it being hosted on a centralized server.

For this to be possible, it is required to be accessible from the Internet. However, this is not always as straightforward as it may seem. Because of the nature of the Internet and security reasons, routers create a local network that makes your computer invisible to the Internet. This technology is called [NAT](https://en.wikipedia.org/wiki/Network_address_translation).

## Open your local firewall
To allow other peers to communicate with your Transmission instance, you have to forward a port through your firewall.

### On macOS
Upon opening Transmission for the first time, a macOS dialog box should appear asking if you will allow Transmission to receive incoming connections. Click Accept.

If this does not happen, you can add Transmission to Leopard's firewall manually:
 1. Open System Prefs >> Security >> Firewall. Make sure "Set access for specific services and applications" is selected.
 1. Click the "+" button and select Transmission from you applications folder.
 1. Make sure the pull down menu is set to "Allow incoming connections".

### On Unix
 * For instructions on how to use it, open a Terminal and open the man page of your firewall. (e.g. 'man ufw, man firewalld')
 * You need to ensure that Transmission's port (displayed in preferences) is forwarded in the firewall.

### Windows
 1. Navigate to the control panel.
 1. Click "Windows Defender Firewall"
 1. Choose "Advanced Settings" on the left panel. This will prompt for administrator access.
 1. On the left panel click "Inbound Rules".
 1. Once you have the Inbound Rules list showing you want to click on "New Rule" under the "Actions" panel on the right.
 1. This will bring up a new window titled "New Inbound Rule Wizard". Whilst there are a couple ways to go about things from here, here we will only cover opening just the port alone for TCP/UDP. With that being said, check the button next to "Port" and click "Next".
 1. In "Protocols and Ports" select either TCP (for BT protocol) or UDP (for μTP) for which one you want to open (to open both you must now choose one and go back later and created another rule to select the other). Underneath that you choose what port to open. 51413 is the default but you can set it to any port you like as long as it correlates with the port you have set in Transmission. Once you have your port click on "Next".
 1. Here in "Actions" you choose to either "Allow the connection", "Allow the connection if it is secure" (only allow packets using [IPsec](https://en.wikipedia.org/wiki/IPsec)), choose based on personal preference here then click "Next".
 1. Choose the profile according to the type of network you are connected to and hit "Next".
 1. The last thing is to give the rule a name. It can be whatever just make sure it's something you can read and remember exactly what it's for.

  * Note this only opens the port in Windows and has no effect on the router.

## Open ports & forwarding
To allow other peers to connect to you, you will need to forward a port from the router to your computer.

### NAT-PMP / UPnP
By default Transmission will try to forward this port for you, using [UPnP]([https://en.wikipedia.org/wiki/UPnP) or [NAT-PMP](https://en.wikipedia.org/wiki/NAT-PMP).

Most routers manufactured since 2001 have either the UPnP or NAT-PMP feature.

 * Open Transmission.
 * Go to Preferences >> Network >> Ports, and check 'Forward port from router'.
 * If Transmission reports that the 'Port is open' then you have successfully port forwarded!

### Forward manually through a router
 1. Find out what your IP address is.
  * *On macOS*- Go to System Preferences >> Network, double-clicking on your connection (for instance, Built-in Ethernet), and clicking the TCP/IP tab. The address is probably something like 192.168.1.100, or 10.0.1.2. The IP of your router is here too.
  * *On Unix*- In Ubuntu, right-click the Network Manager applet in the menu bar, and select 'Connection Information'. The address is probably something like 192.168.1.2, or 10.0.1.2.
   * If you don't have Network Manager, open a Terminal and type 'ifconfig'. It will list information for each of your network devices. Find the one you are using, and use the number after 'inet addr:'.
   * Using the command "ip a" will achieve the same results in a different format.
 2. Open Transmission, go to preferences, and enter a number for the port. It is recommended you pick a random number between 49152 and 65535. The default is 51413. Then quit Transmission.
 3. Go into your router configuration screen. Normally this is done via your web browser using the address 192.168.0.1 etc.
 4. Find the port forwarding (sometimes called port mapping) screen. While the page will be different for each router generally you will enter something similar to the following:
 5. For 'Application' type 'Trans'.
 6. For 'Start Port' and 'End port' type in the port you chose in Step 2. (e.g. 51413).
 7. For Protocol, choose Both.
 8. For IP address, type in your IP address you found in Step 1. (e.g. 192.168.1.2).
 9. Check Enable.
 10. Click save settings.

For more comprehensive instructions specific to your router, visit [portforward.com](https://www.portforward.com/english/routers/port_forwarding/routerindex.htm) and choose your router from the list.

  * Please note that port mapping changes might not take effect until the router is rebooted


#### Verify
 1. Go to [CanYouSeeMe.org](https://www.canyouseeme.org/).
 1. Enter the port Transmission uses.
 1. If Transmission reports that the 'Port is open' then you have successfully forwarded the port.

#### Common problems
Go to the [Why is my port closed?](Why-is-my-port-closed.md) page.
