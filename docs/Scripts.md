# Transmission scripts
## Introduction
Thanks to the powerful [RPC](./rpc-spec.md), `transmission-remote` can talk to any client that has the RPC enabled. This means that a script written using `transmission-remote` or [RPC](./rpc-spec.md) can, without rewrite, communicate with all the Transmission clients: Mac, Linux, Windows, and headless.

Mac OS users may wonder whether there will be AppleScript scripts, the answer is ''no''. Although AppleScript is a nice technology, it's a pain to implement. However, macOS is a Unix after all, so any script you find here will also work on the Mac. Even from within AppleScript, you can run these scripts by typing: `do shell script "path/to/script"`.

## How-To
If you are interested at writing scripts for Transmission, have a look at the following pages:
 * [wiki:man Transmission man pages]
 * [wiki:ConfigFiles Configuration Files]
 * [wiki:EditConfigFiles Editing Configuration Files]
 * [wiki:EnvironmentVariables Environment Variables]
 * [wiki:rpc RPC Protocol Specification]

For those who need more information how to use the scripts, have a look at the following links:
 * [https://help.ubuntu.com/community/CronHowto Cron How-To]: Run scripts at a regular interval

## Scripts
### Start/Stop
 * [wiki:Scripts/initd init.d script] (Debian, Ubuntu and BSD derivatives)
 * [wiki:Scripts/runscript runscript] (Gentoo and other `runscript`-compatible systems)

### On torrent completion
Transmission can be set to invoke a script when downloads complete. The environment variables supported are:

 * `TR_APP_VERSION` - Transmission's short version string, e.g. `4.0.0`
 * `TR_TIME_LOCALTIME`
 * `TR_TORRENT_BYTES_DOWNLOADED` - Number of bytes that were downloaded for this torrent
 * `TR_TORRENT_DIR` - Location of the downloaded data
 * `TR_TORRENT_HASH` - The torrent's info hash
 * `TR_TORRENT_ID`
 * `TR_TORRENT_LABELS` - A comma-delimited list of the torrent's labels
 * `TR_TORRENT_NAME`
 * `TR_TORRENT_TRACKERS` - A comma-delimited list of the torrent's trackers' announce URLs

[https://trac.transmissionbt.com/browser/trunk/extras/send-email-when-torrent-done.sh Here is an example script] that sends an email when a torrent finishes.

### Obsolete
Functionality of these scripts has been implemented in libtransmission and is thus available in all clients.

 * [wiki:Scripts/EmailNotifier Email Notification Script]
 * [wiki:Scripts/BlockListUpdater Block List Updater]
 * [wiki:Scripts/Watchdog Watch Directory Script]
 * [wiki:Scripts/Scheduler Bandwidth Scheduler]

## contrib/scripts
Tomas Carnecky (aka wereHamster) is maintaining a set of scripts in his [https://github.com/wereHamster/transmission/tree/master/contrib/scripts/ GitHub repository].

Falk Husemann (aka hxgn) is maintaining scripts in his [https://falkhusemann.de/category/tcp_ip/transmission-tcp_ip/ blog].

Oguz wrote [https://oguzarduc.blogspot.com/2012/05/transmission-quit-script-in-php.html on his blog] a PHP script to stop Transmission after it finishes downloading and seeding.
Scripts which have not yet been ported and may not work with the latest version:
 * https://pastebin.com/QzVxQDtM: Bash - (cron)script to keep a maximum number of torrents running; starting and pausing torrents as necessary
 * https://github.com/jaboto/Transmission-script - (cron)script set network limits according to the number of clients in the network

## Security with systemd
`transmission-daemon`'s packaging has many permissions disabled as a standard safety measure. If your script needs more permissions than are provided by the default, users have [reported](https://github.com/transmission/transmission/issues/1951) that it can be resolved by changing to `NoNewPrivileges=false` in `/lib/systemd/system/transmission-daemon.service`.
