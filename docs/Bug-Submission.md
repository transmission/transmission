# Submitting a Bug Report #

If you're having trouble with Transmission then the things you should do in order are:
 1. Make sure you are running the current release
 1. Search the documentation
 1. For a bug, ideally we'd like you to check if it still exists in the [nightly build](https://build.transmissionbt.com/).
 1. Search the Issue Tracker to see if your concern has already been reported:
     * [All open tickets](http://trac.transmissionbt.com/report/2)
     * [Search](http://trac.transmissionbt.com/search)
 1. If you _don't_ see a ticket matching your problem or feature
     * read the section below on **Submitting a Bug Report**
     * [add a new ticket](http://trac.transmissionbt.com/newticket).

   If you _do_ see an existing ticket, please add a comment there again adding as much information as possible.
   The more users interested in a ticket, the higher priority it's given.

## Information Required in a Bug Report ##

 * State the version of Transmission you're using (eg Linux/GTK+ 1.80).
   If you're using a GUI version of Transmission, you can find its version in the `About` dialog in the Help menu.
   **Don't** say _the latest version_ it's ambiguous.
 * State what operating system and version (eg Mac OS X 10.5.8, Ubuntu 8.04, ...)
 * Describe the symptoms in a short precise manner.
 * If the problem is reproducible and you explain how to reproduce it, then it stands a high chance of being addressed.
   If the problem is intermittent then we still want to know, but if you can't tell us how to reproduce it we can't easily work on it.
 * See if the problem only occurs under certain conditions.
   Eg: If you're seeing the bug with 50 torrents running and speed limits turned on, see if it persists with speed limits turned off.
   See if it persists with 1 torrent running. See if it persists after pausing and restarting the torrent. etc.

**The more work you do to narrow down the bug, the more chance we have of finding and fixing it.**

## Reporting specific types of problems ##

Additional information or steps are required for certain categories of problem:

### Slow Speeds ###

If you're experiencing slow speeds and you've been through the wiki, then please provide the following information **in addition** to that above:
 * Do you have any per-torrent or global speed limits set?
 * What are your global and per-torrent peer limits?
 * How many seeds and peers does the "Peers" tab say there are in the swarm?
 * How long ago does the "Tracker" tab say the last announce and scrape results were?
 * In the "Tracker" tab's list of trackers, how many Tiers are there, and how many trackers are listed for each tier?
 * How many peers are you connected to?
 * Has your torrent finished downloading?
 * If you're still downloading, do any of the connected peers have a higher "completed" percentage than you?
 * Is your incoming peer port open or closed?
   The "Peers" and "Tracker" tabs are in the dialog named "Inspector" on the Mac GUI and "Torrent Properties" on the GTK+ GUI.

### Crash on the Mac ###

If you have problems on the Mac version then please do these extra steps:
  * Make sure your system is updated to the latest version of your operating sytem. Note As of 1.60 Transmission requires Mac OS X 10.5 or later.
  * If you're running a nightly build, set the language to English. The localizations will sometimes crash the nightly builds until they are updated right before an official release.
  * OS X collects two pieces of crash information that can help us fix the crash:
     1. In Console.app, look under LOG FILES > ~/Library/Logs/ > CrashReporter > for Transmission. If you find one, include it in your forum post.
     2. In Console.app, select LOG DATABASE QUERIES > Console Messages, and search for Transmission. If you find a message that mentions an assertion failure, include it in your forum post.

If these two pieces of information above are too large for your forum post, Paste them [here](http://transmission.pastebin.com/), click the "one month" and "send" buttons, and include the pastebin's URL in your forum post.

### Port Mapping Error ###

Read the [Port Forwarding Guide](Port-Forwarding-Guide.md) first.
You then need to include:
  * what router you're using
  * that you've confirmed that either UPnP or NAT-PMP is enabled on it
  * If you're using a custom firmware, tell us which one
  * Most importantly, post the lines from the Message Log that contain the phrase "Port Mapping". They will look something like this:

    ```
    02:12:27 Port Mapping (NAT-PMP): initnatpmp returned success (0)
    02:12:27 Port Mapping (NAT-PMP): sendpublicaddressrequest returned success (2)
    02:12:27 Port Mapping: mapping state changed from 'not mapped' to 'mapping'
    02:12:27 Port Mapping: opened port 55555 to listen for incoming peer connections
    02:12:35 Port Mapping (NAT-PMP): readnatpmpresponseorretry returned error -7, errno is 111 (Connection refused)
    02:12:35 Port Mapping (NAT-PMP): If your router supports NAT-PMP, please make sure NAT-PMP is enabled!
    02:12:35 Port Mapping (NAT-PMP): NAT-PMP port forwarding unsuccessful, trying UPnP next
    02:12:37 Port Mapping (UPNP): Found Internet Gateway Device 'http://192.168.1.1:5431/uuid:0012-17c3-4e400200b4b4/WANIPConnection:1'
    02:12:37 Port Mapping (UPNP): Local LAN IP Address is '192.168.1.99'
    02:12:37 Port Mapping (UPNP): Port forwarding via 'http://192.168.1.1:5431/uuid:0012-17c3-4e400200b4b4/WANIPConnection:1', service 'urn:schemas-upnp-org:service:WANIPConnection:1'. (local address: 192.168.1.99:55555)
    02:12:37 Port Mapping (UPNP): Port forwarding successful!
    02:12:37 Port Mapping: mapping state changed from 'mapping' to 'mapped'
    ```
