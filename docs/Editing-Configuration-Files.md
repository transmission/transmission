It is not always possible to set all configurations from the GUI, especially for the daemon or the web interface. This guide will try to give an overview of how and what you can change. For the location of these files, look at the [Configuration Files](Configuration-Files.md) page.

Note: The client _should_ be closed before making changes, otherwise settings will be reverted to their previous state.

Some of Transmission's behavior can also be customized via environment variables.

# GTK / Daemon / CLI
### Overview
GTK, CLI and daemon (both on a Mac and Linux) use a [JSON](https://www.json.org/) formatted file, mainly because of its human readability.
(Consult the [JSON website](https://www.json.org/) for detailed information)

### Reload Settings
You can make the daemon reload the settings file by sending it the `SIGHUP` signal.
Or simply run either of the following commands:
```console
$ killall -HUP transmission-daemon
```
Or:
```console
$ pkill -HUP transmission-daemon
```

### Formatting
Here is a sample of the three basic types: respectively Boolean, Number and String:

```json
{
    "rpc-enabled": true,
    "peer-port": 51413,
    "rpc-whitelist": "127.0.0.1,192.168.*.*"
}
```

### Options
#### IP Announce

 * **announce-ip:** String (default = "") Alternative IP address to announce to the tracker.
 * **announce-ip-enabled:** Boolean (default = false) When enabled **announce-ip** value is used instead of the client's address visible to the tracker for announcement requests.

#### Bandwidth

 * **alt-speed-enabled:** Boolean (default = false, aka 'Turtle Mode')
   _Note: Clicking the "Turtle" in the GUI when the [scheduler](#Scheduling) is enabled, will only temporarily remove the scheduled limit until the next cycle._
 * **alt-speed-up:** Number (KB/s, default = 50)
 * **alt-speed-down:** Number (KB/s, default = 50)
 * **speed-limit-down:** Number (KB/s, default = 100)
 * **speed-limit-down-enabled:** Boolean (default = false)
 * **speed-limit-up:** Number (KB/s, default = 100)
 * **speed-limit-up-enabled:** Boolean (default = false)
 * **upload-slots-per-torrent:** Number (default = 14)

#### [Blocklists](./Blocklists.md)

 * **blocklist-url:** String (default = https://www.example.com/blocklist)
 * **blocklist-enabled:** Boolean (default = false)

#### [Files and Locations](./ConfigFiles.md)

 * **download-dir:** String (default = [default locations](Configuration-Files.md#Locations))
 * **incomplete-dir:** String (default = [default locations](Configuration-Files.md#Locations)) Directory to keep files in until torrent is complete.
 * **incomplete-dir-enabled:** Boolean (default = false) When enabled, new torrents will download the files to **incomplete-dir**. When complete, the files will be moved to **download-dir**.
 * **preallocation:** Number (0 = Off, 1 = Fast, 2 = Full (slower but reduces disk fragmentation), default = 1)
 * **rename-partial-files:** Boolean (default = true) Postfix partially downloaded files with ".part".
 * **start-added-torrents:** Boolean (default = true) Start torrents as soon as they are added.
 * **trash-can-enabled:** Boolean (default = true) Whether to move the torrents to the system's trashcan or unlink them right away upon deletion from Transmission.
 * **trash-original-torrent-files:** Boolean (default = false) Delete torrents added from the watch directory.
 * **umask:** String (default = "022") Sets Transmission's file mode creation mask. See [the umask(2) manpage](https://man7.org/linux/man-pages/man2/umask.2.html) for more information.
 * **watch-dir:** String
 * **watch-dir-enabled:** Boolean (default = false) Watch a directory for torrent files and add them to Transmission.
   _Note: When **watch-dir-enabled** is true, only the transmission-daemon, transmission-gtk, and transmission-qt applications will monitor **watch-dir** for new .torrent files and automatically load them._

#### Misc
 * **cache-size-mb:** Number (default = 4), in megabytes, to allocate for Transmission's memory cache. The cache is used to help batch disk IO together, so increasing the cache size can be used to reduce the number of disk reads and writes. The value is the total available to the Transmission instance.
 * **default-trackers:** String (default = "") A list of double-newline separated tracker announce URLs. These are used for all torrents in addition to the per torrent trackers specified in the torrent file. If a tracker is only meant to be a backup, it should be separated from its main tracker by a single newline character. If a tracker should be used additionally to another tracker it should be separated by two newlines. (e.g. "udp://tracker.example.invalid:1337/announce\n\nudp://tracker.another-example.invalid:6969/announce\nhttps://backup-tracker.another-example.invalid:443/announce\n\nudp://tracker.yet-another-example.invalid:1337/announce", in this case tracker.example.invalid, tracker.another-example.invalid and tracker.yet-another-example.invalid would be used as trackers and backup-tracker.another-example.invalid as backup in case tracker.another-example.invalid is unreachable.
 * **dht-enabled:** Boolean (default = true) Enable [Distributed Hash Table (DHT)](https://wiki.theory.org/BitTorrentSpecification#Distributed_Hash_Table).
 * **encryption:** Number (0 = Prefer unencrypted connections, 1 = Prefer encrypted connections, 2 = Require encrypted connections; default = 1) [Encryption](https://wiki.vuze.com/w/Message_Stream_Encryption) preference. Encryption may help get around some ISP filtering, but at the cost of slightly higher CPU use.
 * **lazy-bitfield-enabled:** Boolean (default = true) May help get around some ISP filtering. [Vuze specification](https://wiki.vuze.com/w/Commandline_options#Network_Options).
 * **lpd-enabled:** Boolean (default = false) Enable [Local Peer Discovery (LPD)](https://en.wikipedia.org/wiki/Local_Peer_Discovery).
 * **message-level:** Number (0 = None, 1 = Critical, 2 = Error, 3 = Warn, 4 = Info, 5 = Debug, 6 = Trace, default = 2) Set verbosity of Transmission's log messages.
 * **pex-enabled:** Boolean (default =  true) Enable [https://en.wikipedia.org/wiki/Peer_exchange Peer Exchange (PEX)].
 * **pidfile:** String Path to file in which daemon PID will be stored (transmission-daemon only)
 * **prefetch-enabled:** Boolean (default = true). When enabled, Transmission will hint to the OS which piece data it's about to read from disk in order to satisfy requests from peers. On Linux, this is done by passing `POSIX_FADV_WILLNEED` to [posix_fadvise()](https://www.kernel.org/doc/man-pages/online/pages/man2/posix_fadvise.2.html). On macOS, this is done by passing `F_RDADVISE` to [fcntl()](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fcntl.2.html).
 * **scrape-paused-torrents-enabled:** Boolean (default = true)
 * **script-torrent-added-enabled:** Boolean (default = false) Run a script when a torrent is added to Transmission. Environmental variables are passed in as detailed on the [Scripts](./Scripts.md) page
 * **script-torrent-added-filename:** String (default = "") Path to script.
 * **script-torrent-done-enabled:** Boolean (default = false) Run a script when a torrent is done downloading. Environmental variables are passed in as detailed on the [Scripts](./Scripts.md) page
 * **script-torrent-done-filename:** String (default = "") Path to script.
 * **script-torrent-done-seeding-enabled:** Boolean (default = false) Run a script when a torrent is done seeding. Environmental variables are passed in as detailed on the [Scripts](./Scripts.md) page
 * **script-torrent-done-seeding-filename:** String (default = "") Path to script.
 * **tcp-enabled:** Boolean (default = true) Optionally disable TCP connection to other peers. Never disable TCP when you also disable UTP, because then your client would not be able to communicate. Disabling TCP might also break webseeds. Unless you have a good reason, you should not set this to false.
 * **torrent-added-verify-mode:** String ("fast", "full", default: "fast") Whether newly-added torrents' local data should be fully verified when added, or wait and verify them on-demand later. See [#2626](https://github.com/transmission/transmission/pull/2626) for more discussion.
 * **utp-enabled:** Boolean (default = true) Enable [Micro Transport Protocol (µTP)](https://en.wikipedia.org/wiki/Micro_Transport_Protocol)

#### Peers
 * **bind-address-ipv4:** String (default = "0.0.0.0") Where to listen for peer connections. When no valid IPv4 address is provided, Transmission will default to "0.0.0.0".
 * **bind-address-ipv6:** String (default = "::") Where to listen for peer connections. When no valid IPv6 address is provided, Transmission will determine your default public IPv6 address and use it.
 * **peer-congestion-algorithm:** String. This is documented on https://www.pps.jussieu.fr/~jch/software/bittorrent/tcp-congestion-control.html.
 * **peer-limit-global:** Number (default = 240)
 * **peer-limit-per-torrent:** Number (default =  60)
 * **peer-socket-tos:** String (default = "default") Set the [Type-Of-Service (TOS)](https://en.wikipedia.org/wiki/Type_of_Service) parameter for outgoing TCP packets. Possible values are "default", "lowcost", "throughput", "lowdelay" and "reliability". The value "lowcost" is recommended if you're using a smart router, and shouldn't harm in any case.

#### Peer Port
 * **peer-port:** Number (default = 51413)
 * **peer-port-random-high:** Number (default = 65535)
 * **peer-port-random-low:** Number (default = 1024)
 * **peer-port-random-on-start:** Boolean (default = false)
 * **port-forwarding-enabled:** Boolean (default = true) Enable [UPnP](https://en.wikipedia.org/wiki/Universal_Plug_and_Play) or [NAT-PMP](https://en.wikipedia.org/wiki/NAT_Port_Mapping_Protocol).

#### Queuing
 * **download-queue-enabled:** Boolean (default = true) When true, Transmission will only download `download-queue-size` non-stalled torrents at once.
 * **download-queue-size:** Number (default = 5) See download-queue-enabled.
 * **queue-stalled-enabled:** Boolean (default = true) When true, torrents that have not shared data for `queue-stalled-minutes` are treated as 'stalled' and are not counted against the `download-queue-size` and `seed-queue-size` limits.
 * **queue-stalled-minutes:** Number (default = 30) See queue-stalled-enabled.
 * **seed-queue-enabled:** Boolean (default = false) When true. Transmission will only seed `seed-queue-size` non-stalled torrents at once.
 * **seed-queue-size:** Number (default = 10) See seed-queue-enabled.

#### [RPC](rpc-spec.md)
 * **anti-brute-force-enabled:**: Boolean (default = false) Enable a very basic brute force protection for the RPC server. See "anti-brute-force-threshold" below.
 * **anti-brute-force-threshold:**: Number (default = 100) After this amount of failed authentication attempts is surpassed, the RPC server will deny any further authentication attempts until it is restarted. This is not tracked per IP but in total.
 * **rpc-authentication-required:** Boolean (default = false)
 * **rpc-bind-address:** String (default = "0.0.0.0") Where to listen for RPC connections
 * **rpc-enabled:** Boolean (default = true)
 * **rpc-host-whitelist:** String (Comma-delimited list of domain names. Wildcards allowed using '\*'. Example: "*.foo.org,example.com", Default: "", Always allowed: "localhost", "localhost.", all the IP addresses. Added in v2.93)
 * **rpc-host-whitelist-enabled:** Boolean (default = true. Added in v2.93)
 * **rpc-password:** String. You can enter this in as plaintext when Transmission is not running, and then Transmission will salt the value on startup and re-save the salted version as a security measure. **Note:** Transmission treats passwords starting with the character `{` as salted, so when you first create your password, the plaintext password you enter must not begin with `{`.
 * **rpc-port:** Number (default = 9091)
 * **rpc-socket-mode:** String UNIX filesystem mode for the RPC UNIX socket (default: 0750; used when `rpc-bind-address` is a UNIX socket)
 * **rpc-url:** String (default = /transmission/. Added in v2.2)
 * **rpc-username:** String
 * **rpc-whitelist:** String (Comma-delimited list of IP addresses. Wildcards allowed using '\*'. Example: "127.0.0.\*,192.168.\*.\*", Default:  "127.0.0.1")
 * **rpc-whitelist-enabled:** Boolean (default = true)

#### Scheduling
 * **alt-speed-time-enabled:** Boolean (default = false)
   _Note: When enabled, this will toggle the **alt-speed-enabled** setting._
 * **alt-speed-time-begin:** Number (default = 540, in minutes from midnight, 9am)
 * **alt-speed-time-end:** Number (default = 1020, in minutes from midnight, 5pm)
 * **alt-speed-time-day:** Number/bitfield (default = 127, all days)
   * Start with 0, then for each day you want the scheduler enabled, add:
     * **Sunday**: 1 (binary: `0000001`)
     * **Monday**: 2 (binary: `0000010`)
     * **Tuesday**: 4 (binary: `0000100`)
     * **Wednesday**: 8 (binary: `0001000`)
     * **Thursday**: 16 (binary: `0010000`)
     * **Friday**: 32 (binary: `0100000`)
     * **Saturday**: 64 (binary: `1000000`)
   * Examples:
     * **Weekdays**: 62 (binary: `0111110`)
     * **Weekends**: 65 (binary: `1000001`)
     * **All Days**: 127 (binary: `1111111`)
 * **idle-seeding-limit:** Number (default = 30) Stop seeding after being idle for _N_ minutes.
 * **idle-seeding-limit-enabled:** Boolean (default = false)
 * **ratio-limit:** Number (default = 2.0)
 * **ratio-limit-enabled:**  Boolean (default = false)

### Legacy Options
Only keys that differ from above are listed here. These options have been replaced in newer versions of Transmission.

#### 2.31 (and older)
 * **open-file-limit:** Number (default = 32)

#### 1.5x (and older)
##### Bandwidth
 * **download-limit:** Number (KB/s, default = 100)
 * **download-limit-enabled:** Boolean (default = false)
 * **upload-limit:** Number (KB/s, default = 100)
 * **upload-limit-enabled:** Boolean (default = false)

##### Peer Port
 * **peer-port-random-enabled:** Boolean (default = false)

#### 1.4x (and older)
##### Proxy
 * **proxy-authentication** String
 * **proxy-authentication-required:** Boolean (default = 0)
 * **proxy-port:** Number (default = 80)
 * **proxy-server:** String
 * **proxy-server-enabled:** Boolean (default = 0)
 * **proxy-type:** Number (0 = HTTP, 1 = SOCKS4, 2 = SOCKS5, default = 0)
 * **proxy-username:** String

##### Peers
 * **max-peers-global:** Number (default = 240)
 * **max-peers-per-torrent:** Number (default =  60)

#### 1.3x (and older)
##### [RPC](rpc-spec.md)
 * **rpc-access-control-list:** String (Comma-delimited list of IP addresses prefixed with "+" or "-". Wildcards allowed using '\*'. Example: "+127.0.0.\*,-192.168.\*.\*", Default:  "+127.0.0.1")

## macOS
### Overview
macOS has a standardized way of saving user preferences files using [XML](https://en.wikipedia.org/wiki/XML) format. These files are called [plist](https://en.wikipedia.org/wiki/Plist) (short for property list) files. Usually there is no need to modify these files directly, since Apple provided a [command-line tool](https://developer.apple.com/DOCUMENTATION/Darwin/Reference/ManPages/man1/defaults.1.html) to reliably change settings. You do need to restart Transmission before these have effect.

In short:
 * To set a key: `defaults write org.m0k.transmission <key> <value>`
 * To reset a key: `defaults delete org.m0k.transmission <key>`

### Options
 * **PeerSocketTOS:** Number (Default = 0)
 * **RPCHostWhitelist:** String, see "rpc-host-whitelist" above.
 * **RPCUseHostWhitelist:** Boolean, see "rpc-host-whitelist-enabled" above.
