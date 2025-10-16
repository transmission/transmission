> [!IMPORTANT]
> Since `4.1.0` (`rpc_version` 18), Transmission has switched from a bespoke RPC protocol to [JSON-RPC 2.0](https://www.jsonrpc.org/specification).
> All RPC strings has been converted to snake_case as well.
>
> For backward compatibility, the old RPC protocol will still be available until `5.0.0`.
> The old RPC strings consisting of a mix of kebab-case and camelCase are still available,
> but they are deprecated and will be removed in `5.0.0` as well.
>
> For documentation of the old RPC protocol and strings, please consult documentation from previous versions.
> https://github.com/transmission/transmission/blob/4.0.6/docs/rpc-spec.md

# Transmission's RPC specification
This document describes a protocol for interacting with Transmission sessions remotely.

### 1.1 Terminology
The [JSON](https://www.json.org/) terminology in [RFC 8259](https://datatracker.ietf.org/doc/html/rfc8259) is used.
RPC requests and responses are formatted in JSON.

### 1.2 Tools
If `transmission-remote` is called with a `--debug` argument, its RPC traffic to the Transmission server will be dumped to the terminal. This can be useful when you want to compare requests in your application to another for reference.

If `transmission-qt` is run with an environment variable `TR_RPC_VERBOSE` set, it too will dump the RPC requests and responses to the terminal for inspection.

Lastly, using the browser's developer tools in the Transmission web client is always an option.

### 1.3 Libraries of ready-made wrappers
Some people outside of the Transmission project have written libraries that wrap this RPC API. These aren't supported by the Transmission project, but are listed here in the hope that they may be useful:

| Language | Link
|:---|:---
| C# | https://www.nuget.org/packages/Transmission.API.RPC
| Go | https://github.com/hekmon/transmissionrpc
| Python | https://github.com/Trim21/transmission-rpc
| Rust | https://crates.io/crates/transmission-rpc


## 2 Message format
Transmission follows the [JSON-RPC 2.0](https://www.jsonrpc.org/specification) specification and supports the entirety of it,
except that parameters by-position is not supported, meaning the request parameters must be an Object.

Response parameters are returned in the `result` Object.

#### Example request
```json
{
   "jsonrpc": "2.0",
   "params": {
     "fields": [ "version" ]
   },
   "method": "session_get",
   "id": 912313
}
```

#### Example response
```json
{
   "jsonrpc": "2.0",
   "result": {
      "version": "4.1.0-dev (ae226418eb)"
   },
   "id": 912313
}
```

### 2.1 Error data

JSON-RPC 2.0 allows for additional information about an error be included in the `data` key of the Error object in an implementation-defined format.

In Transmission, this key is an Object that includes:

1. An optional `errorString` string that provides additional information that is not included in the `message` key of the Error object.
2. An optional `result` Object that contains additional keys defined by the method.

```json
{
   "jsonrpc": "2.0",
   "error": {
      "code": 7,
      "message": "HTTP error from backend service",
      "data": {
         "errorString": "Couldn't test port: No Response (0)",
         "result": {
            "ipProtocol": "ipv6"
         }
      }
   },
   "id": 912313
}
```

### 2.2 Transport mechanism
HTTP POSTing a JSON-encoded request is the preferred way of communicating
with a Transmission RPC server. The current Transmission implementation
has the default URL as `http://host:9091/transmission/rpc`. Clients
may use this as a default, but should allow the URL to be reconfigured,
since the port and path may be changed to allow mapping and/or multiple
daemons to run on a single server.

The RPC server will normally return HTTP 200 regardless of whether the
request succeeded. For JSON-RPC 2.0 notifications, HTTP 204 will be returned.

#### 2.2.1 CSRF protection
Most Transmission RPC servers require a `X-Transmission-Session-Id`
header to be sent with requests, to prevent CSRF attacks.

When your request has the wrong id -- such as when you send your first
request, or when the server expires the CSRF token -- the
Transmission RPC server will return an HTTP 409 error with the
right `X-Transmission-Session-Id` in its own headers.

So, the correct way to handle a 409 response is to update your
`X-Transmission-Session-Id` and to resend the previous request.

#### 2.2.2 DNS rebinding protection
Additional check is being made on each RPC request to make sure that the
client sending the request does so using one of the allowed hostnames by
which RPC server is meant to be available.

If host whitelisting is enabled (which is true by default), Transmission
inspects the `Host:` HTTP header value (with port stripped, if any) and
matches it to one of the whitelisted names. Regardless of host whitelist
content, `localhost` and `localhost.` domain names as well as all the IP
addresses are always implicitly allowed.

For more information on configuration, see settings.json documentation for
`rpc_host_whitelist_enabled` and `rpc_host_whitelist` keys.

#### 2.2.3 Authentication
Enabling authentication is an optional security feature that can be enabled
on Transmission RPC servers. Authentication occurs by method of HTTP Basic
Access Authentication.

If authentication is enabled, Transmission inspects the `Authorization:`
HTTP header value to validate the credentials of the request. The value
of this HTTP header is expected to be [`Basic <b64 credentials>`](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Authorization#basic),
where <b64 credentials> is equal to a base64 encoded string of the
username and password (respectively), separated by a colon.

## 3 Torrent requests
### 3.1 Torrent action requests
| Method name          | libtransmission function | Description
|:--|:--|:--
| `torrent_start`      | tr_torrentStart          | start torrent
| `torrent_start_now`  | tr_torrentStartNow       | start torrent disregarding queue position
| `torrent_stop`       | tr_torrentStop           | stop torrent
| `torrent_verify`     | tr_torrentVerify         | verify torrent
| `torrent_reannounce` | tr_torrentManualUpdate   | re-announce to trackers now

Request parameters: `ids`, which specifies which torrents to use.
All torrents are used if the `ids` parameter is omitted.

`ids` should be one of the following:

1. an integer referring to a torrent id
2. a list of torrent id numbers, SHA1 hash strings, or both
3. a string, `recently_active`, for recently-active torrents

Note that integer torrent ids are not stable across Transmission daemon
restarts. Use torrent hashes if you need stable ids.

Response parameters: none

### 3.2 Torrent mutator: `torrent_set`
Method name: `torrent_set`

Request parameters:

| Key | Value Type | Value Description
|:--|:--|:--
| `bandwidth_priority`    | number   | this torrent's bandwidth tr_priority_t
| `download_limit`        | number   | maximum download speed (KBps)
| `download_limited`      | boolean  | true if `download_limit` is honored
| `files_unwanted`        | array    | indices of file(s) to not download
| `files_wanted`          | array    | indices of file(s) to download
| `group`                 | string   | The name of this torrent's bandwidth group
| `honors_session_limits` | boolean  | true if session upload limits are honored
| `ids`                   | array    | torrent list, as described in 3.1
| `labels`                | array    | array of string labels
| `location`              | string   | new location of the torrent's content
| `peer_limit`            | number   | maximum number of peers
| `priority_high`         | array    | indices of high-priority file(s)
| `priority_low`          | array    | indices of low-priority file(s)
| `priority_normal`       | array    | indices of normal-priority file(s)
| `queue_position`        | number   | position of this torrent in its queue [0...n)
| `seed_idle_limit`       | number   | torrent-level number of minutes of seeding inactivity
| `seed_idle_mode`        | number   | which seeding inactivity to use. See tr_idlelimit
| `seed_ratio_limit`      | double   | torrent-level seeding ratio
| `seed_ratio_mode`       | number   | which ratio to use. See tr_ratiolimit
| `sequential_download`   | boolean  | download torrent pieces sequentially
| `tracker_add`           | array    | **DEPRECATED** use `tracker_list` instead
| `tracker_list`          | string   | string of announce URLs, one per line, and a blank line between [tiers](https://www.bittorrent.org/beps/bep_0012.html).
| `tracker_remove`        | array    | **DEPRECATED** use `tracker_list` instead
| `tracker_replace`       | array    | **DEPRECATED** use `tracker_list` instead
| `upload_limit`          | number   | maximum upload speed (KBps)
| `upload_limited`        | boolean  | true if `upload_limit` is honored

Just as an empty `ids` value is shorthand for "all ids", using an empty array
for `files_wanted`, `files_unwanted`, `priority_high`, `priority_low`, or
`priority_normal` is shorthand for saying "all files".

   Response parameters: none

### 3.3 Torrent accessor: `torrent_get`
Method name: `torrent_get`.

Request parameters:

1. An optional `ids` array as described in 3.1.
2. A required `fields` array of keys. (see list below)
3. An optional `format` string specifying how to format the
   `torrents` response field. Allowed values are `objects`
   (default) and `table`. (see "Response parameters" below)

Response parameters:

1. A `torrents` array.

   If the `format` request was `objects` (default), `torrents` will
   be an array of objects, each of which contains the key/value
   pairs matching the request's `fields` arg. This was the only
   format before Transmission 3 and has some obvious programmer
   conveniences, such as parsing directly into Javascript objects.

   If the format was `table`, then `torrents` will be an array of
   arrays. The first row holds the keys and each remaining row holds
   a torrent's values for those keys. This format is more efficient
   in terms of JSON generation and JSON parsing.

2. If the request's `ids` field was `recently_active`,
   a `removed` array of torrent-id numbers of recently-removed
   torrents.

Note: For more information on what these fields mean, see the comments
in [libtransmission/transmission.h](../libtransmission/transmission.h).
The 'source' column here corresponds to the data structure there.

| Key | Value Type | transmission.h source
|:--|:--|:--
| `activity_date` | number | tr_stat
| `added_date` | number | tr_stat
| `availability` | array (see below)| tr_torrentAvailability()
| `bandwidth_priority` | number | tr_priority_t
| `comment` | string | tr_torrent_view
| `corrupt_ever`| number | tr_stat
| `creator`| string | tr_torrent_view
| `date_created`| number| tr_torrent_view
| `desired_available`| number| tr_stat
| `done_date`| number | tr_stat
| `download_dir` | string  | tr_torrent
| `downloaded_ever` | number  | tr_stat
| `download_limit` | number  | tr_torrent
| `download_limited` | boolean | tr_torrent
| `edit_date` | number | tr_stat
| `error` | number | tr_stat
| `error_string` | string | tr_stat
| `eta` | number | tr_stat
| `eta_idle` | number | tr_stat
| `file_count` | number | tr_info
| `files`| array (see below)| n/a
| `file_stats`| array (see below)| n/a
| `group`| string| n/a
| `hash_string`| string| tr_torrent_view
| `have_unchecked`| number| tr_stat
| `have_valid`| number| tr_stat
| `honors_session_limits`| boolean| tr_torrent
| `id` | number | tr_torrent
| `is_finished` | boolean| tr_stat
| `is_private` | boolean| tr_torrent
| `is_stalled` | boolean| tr_stat
| `labels` | array of strings | tr_torrent
| `left_until_done` | number| tr_stat
| `magnet_link` | string| n/a
| `manual_announce_time` | number| tr_stat
| `max_connected_peers` | number| tr_torrent
| `metadata_percent_complete` | double| tr_stat
| `name` | string| tr_torrent_view
| `peer_limit` | number| tr_torrent
| `peers` | array (see below)| n/a
| `peers_connected` | number| tr_stat
| `peers_from` | object (see below)| n/a
| `peers_getting_from_us` | number| tr_stat
| `peers_sending_to_us` | number| tr_stat
| `percent_complete` | double | tr_stat
| `percent_done` | double | tr_stat
| `pieces` | string (see below)| tr_torrent
| `piece_count`| number| tr_torrent_view
| `piece_size`| number| tr_torrent_view
| `priorities`| array (see below)| n/a
| `primary_mime_type`| string| tr_torrent
| `queue_position`| number| tr_stat
| `rate_download` (B/s)| number| tr_stat
| `rate_upload` (B/s)| number| tr_stat
| `recheck_progress`| double| tr_stat
| `seconds_downloading`| number| tr_stat
| `seconds_seeding`| number| tr_stat
| `seed_idle_limit`| number| tr_torrent
| `seed_idle_mode`| number| tr_inactivelimit
| `seed_ratio_limit`| double| tr_torrent
| `seed_ratio_mode`| number| tr_ratiolimit
| `sequential_download`| boolean| tr_torrent
| `size_when_done`| number| tr_stat
| `start_date`| number| tr_stat
| `status`| number (see below)| tr_stat
| `torrent_file`| string| tr_info
| `total_size`| number| tr_torrent_view
| `trackers`| array (see below)| n/a
| `tracker_list` | string | string of announce URLs, one per line, with a blank line between tiers
| `tracker_stats`| array (see below)| n/a
| `uploaded_ever`| number| tr_stat
| `upload_limit`| number| tr_torrent
| `upload_limited`| boolean| tr_torrent
| `upload_ratio`| double| tr_stat
| `wanted`| array (see below)| n/a
| `webseeds`| array of strings | tr_tracker_view
| `webseeds_sending_to_us`| number| tr_stat

`availability`: An array of `piece_count` numbers representing the number of connected peers that have each piece, or -1 if we already have the piece ourselves.

`files`: array of objects, each containing:

| Key | Value Type | transmission.h source
|:--|:--|:--
| `bytes_completed` | number | tr_file_view
| `length` | number | tr_file_view
| `name` | string | tr_file_view
| `begin_piece` | number | tr_file_view
| `end_piece` | number | tr_file_view

Files are returned in the order they are laid out in the torrent. References to "file indices" throughout this specification should be interpreted as the position of the file within this ordering, with the first file bearing index 0.

`file_stats`: a file's non-constant properties. An array of `tr_info.filecount` objects, in the same order as `files`, each containing:

| Key | Value Type | transmission.h source
|:--|:--|:--
| `bytes_completed` | number | tr_file_view
| `wanted` | boolean | tr_file_view (**Note:** Not to be confused with `torrent_get.wanted`, which is an array of 0/1 instead of boolean)
| `priority` | number | tr_file_view

`peers`: an array of objects, each containing:

| Key | Value Type | transmission.h source
|:--|:--|:--
| `address`              | string     | tr_peer_stat
| `client_is_choked`     | boolean    | tr_peer_stat
| `client_is_interested` | boolean    | tr_peer_stat
| `client_name`          | string     | tr_peer_stat
| `flag_str`             | string     | tr_peer_stat
| `is_downloading_from`  | boolean    | tr_peer_stat
| `is_encrypted`         | boolean    | tr_peer_stat
| `is_incoming`          | boolean    | tr_peer_stat
| `is_uploading_to`      | boolean    | tr_peer_stat
| `is_utp`               | boolean    | tr_peer_stat
| `peer_is_choked`       | boolean    | tr_peer_stat
| `peer_is_interested`   | boolean    | tr_peer_stat
| `port`                 | number     | tr_peer_stat
| `progress`             | double     | tr_peer_stat
| `rate_to_client` (B/s) | number     | tr_peer_stat
| `rate_to_peer` (B/s)   | number     | tr_peer_stat

`peers_from`: an object containing:

| Key | Value Type | transmission.h source
|:--|:--|:--
| `from_cache`    | number     | tr_stat
| `from_dht`      | number     | tr_stat
| `from_incoming` | number     | tr_stat
| `from_lpd`      | number     | tr_stat
| `from_ltep`     | number     | tr_stat
| `from_pex`      | number     | tr_stat
| `from_tracker`  | number     | tr_stat


`pieces`: A bitfield holding `piece_count` flags which are set to 'true' if we have the piece matching that position. JSON doesn't allow raw binary data, so this is a base64-encoded string. (Source: tr_torrent)

`priorities`: An array of `tr_torrentFileCount()` numbers. Each is the `tr_priority_t` mode for the corresponding file.

`status`: A number between 0 and 6, where:

| Value | Meaning
|:--|:--
| 0 | Torrent is stopped
| 1 | Torrent is queued to verify local data
| 2 | Torrent is verifying local data
| 3 | Torrent is queued to download
| 4 | Torrent is downloading
| 5 | Torrent is queued to seed
| 6 | Torrent is seeding


`trackers`: array of objects, each containing:

| Key | Value Type | transmission.h source
|:--|:--|:--
| `announce` | string | tr_tracker_view
| `id` | number | tr_tracker_view
| `scrape` | string | tr_tracker_view
| `sitename` | string | tr_tracker_view
| `tier` | number | tr_tracker_view

`tracker_stats`: array of objects, each containing:

| Key | Value Type | transmission.h source
|:--|:--|:--
| `announce`                 | string     | tr_tracker_view
| `announce_state`           | number     | tr_tracker_view
| `download_count`           | number     | tr_tracker_view
| `has_announced`            | boolean    | tr_tracker_view
| `has_scraped`              | boolean    | tr_tracker_view
| `host`                     | string     | tr_tracker_view
| `id`                       | number     | tr_tracker_view
| `is_backup`                | boolean    | tr_tracker_view
| `last_announce_peer_count` | number     | tr_tracker_view
| `last_announce_result`     | string     | tr_tracker_view
| `last_announce_start_time` | number     | tr_tracker_view
| `last_announce_succeeded`  | boolean    | tr_tracker_view
| `last_announce_time`       | number     | tr_tracker_view
| `last_announce_timed_out`  | boolean    | tr_tracker_view
| `last_scrape_result`       | string     | tr_tracker_view
| `last_scrape_start_time`   | number     | tr_tracker_view
| `last_scrape_succeeded`    | boolean    | tr_tracker_view
| `last_scrape_time`         | number     | tr_tracker_view
| `last_scrape_timed_out`    | boolean    | tr_tracker_view
| `leecher_count`            | number     | tr_tracker_view
| `next_announce_time`       | number     | tr_tracker_view
| `next_scrape_time`         | number     | tr_tracker_view
| `scrape`                   | string     | tr_tracker_view
| `scrape_state`             | number     | tr_tracker_view
| `seeder_count`             | number     | tr_tracker_view
| `sitename`                 | string     | tr_tracker_view
| `tier`                     | number     | tr_tracker_view


`wanted`: An array of `tr_torrentFileCount()` 0/1, 1 (true) if the corresponding file is to be downloaded. (Source: `tr_file_view`)

**Note:** For backwards compatibility, in `4.x.x`, `wanted` is serialized as an array of `0` or `1` that should be treated as booleans.
This will be fixed in `5.0.0` to return an array of booleans.

Example:

Say we want to get the name and total size of torrents #7 and #10.

Request:

```json
{
   "jsonrpc": "2.0",
   "params": {
       "fields": [ "id", "name", "total_size" ],
       "ids": [ 7, 10 ]
   },
   "method": "torrent_get",
   "id": 39693
}
```

Response:

```json
{
   "jsonrpc": "2.0",
   "result": {
      "torrents": [
         {
             "id": 10,
             "name": "Fedora x86_64 DVD",
             "total_size": 34983493932
         },
         {
             "id": 7,
             "name": "Ubuntu x86_64 DVD",
             "total_size": 9923890123
         }
      ]
   },
   "id": 39693
}
```

### 3.4 Adding a torrent
Method name: `torrent_add`

Request parameters:

| Key | Value Type | Description
|:--|:--|:--
| `cookies`             | string    | pointer to a string of one or more cookies.
| `download_dir`        | string    | path to download the torrent to
| `filename`            | string    | filename or URL of the .torrent file
| `labels`              | array     | array of string labels
| `metainfo`            | string    | base64-encoded .torrent content
| `paused`              | boolean   | if true, don't start the torrent
| `peer_limit`          | number    | maximum number of peers
| `bandwidth_priority`  | number    | torrent's bandwidth tr_priority_t
| `files_wanted`        | array     | indices of file(s) to download
| `files_unwanted`      | array     | indices of file(s) to not download
| `priority_high`       | array     | indices of high-priority file(s)
| `priority_low`        | array     | indices of low-priority file(s)
| `priority_normal`     | array     | indices of normal-priority file(s)
| `sequential_download` | boolean   | download torrent pieces sequentially

Either `filename` **or** `metainfo` **must** be included. All other parameters are optional.

The format of the `cookies` should be `NAME=CONTENTS`, where `NAME` is the cookie name and `CONTENTS` is what the cookie should contain. Set multiple cookies like this: `name1=content1; name2=content2;` etc. See [libcurl documentation](http://curl.haxx.se/libcurl/c/curl_easy_setopt.html#CURLOPTCOOKIE) for more information.

Response parameters:

* On success, a `torrent_added` object in the form of one of 3.3's torrent objects with the fields for `id`, `name`, and `hash_string`.

* When attempting to add a duplicate torrent, a `torrent_duplicate` object in the same form is returned, but the response's `result` value is still `success`.

### 3.5 Removing a torrent
Method name: `torrent_remove`

| Key | Value Type | Description
|:--|:--|:--
| `ids`               | array   | torrent list, as described in 3.1
| `delete_local_data` | boolean | delete local data. (default: false)

Response parameters: none

### 3.6 Moving a torrent
Method name: `torrent_set_location`

Request parameters:

| Key | Value Type | Description
|:--|:--|:--
| `ids`      | array   | torrent list, as described in 3.1
| `location` | string  | the new torrent location
| `move`     | boolean | if true, move from previous location. otherwise, search `location` for files (default: false)

Response parameters: none

### 3.7 Renaming a torrent's path
Method name: `torrent_rename_path`

For more information on the use of this function, see the transmission.h
documentation of `tr_torrentRenamePath()`. In particular, note that if this
call succeeds you'll want to update the torrent's `files` and `name` field
with `torrent_get`.

Request parameters:

| Key | Value Type | Description
|:--|:--|:--
| `ids` | array | the torrent list, as described in 3.1 (must only be 1 torrent)
| `path` | string | the path to the file or folder that will be renamed
| `name` | string | the file or folder's new name

Response parameters: `path`, `name`, and `id`, holding the torrent ID integer

## 4  Session requests
### 4.1 Session parameters
| Key | Value Type | Description
|:--|:--|:--
| `alt_speed_down` | number | max global download speed (KBps)
| `alt_speed_enabled` | boolean | true means use the alt speeds
| `alt_speed_time_begin` | number | when to turn on alt speeds (units: minutes after midnight)
| `alt_speed_time_day` | number | what day(s) to turn on alt speeds (look at tr_sched_day)
| `alt_speed_time_enabled` | boolean | true means the scheduled on/off times are used
| `alt_speed_time_end` | number | when to turn off alt speeds (units: same)
| `alt_speed_up` | number | max global upload speed (KBps)
| `anti_brute_force_enabled` | boolean | true means to enable a basic brute force protection for RPC server
| `blocklist_enabled` | boolean | true means enabled
| `blocklist_size` | number | number of rules in the blocklist
| `blocklist_url` | string | location of the blocklist to use for `blocklist_update`
| `cache_size_mb` | number | maximum size of the disk cache (MB)
| `config_dir` | string | location of transmission's configuration directory
| `default_trackers` | string | announce URLs, one per line, and a blank line between [tiers](https://www.bittorrent.org/beps/bep_0012.html).
| `dht_enabled` | boolean | true means allow DHT in public torrents
| `download_dir` | string | default path to download torrents
| `download_dir_free_space` | number |  **DEPRECATED** Use the `free_space` method instead.
| `download_queue_enabled` | boolean | if true, limit how many torrents can be downloaded at once
| `download_queue_size` | number | max number of torrents to download at once (see `download_queue_enabled`)
| `encryption` | string | `required`, `preferred`, `tolerated`
| `idle_seeding_limit` | number | torrents we're seeding will be stopped if they're idle for this long
| `idle_seeding_limit_enabled` | boolean | true if the seeding inactivity limit is honored by default
| `incomplete_dir` | string | path for incomplete torrents, when enabled
| `incomplete_dir_enabled` | boolean | true means keep torrents in `incomplete_dir` until done
| `lpd_enabled` | boolean | true means allow Local Peer Discovery in public torrents
| `peer_limit_global` | number | maximum global number of peers
| `peer_limit_per_torrent` | number | maximum global number of peers
| `peer_port_random_on_start` | boolean | true means pick a random peer port on launch
| `peer_port` | number | port number
| `pex_enabled` | boolean | true means allow PEX in public torrents
| `port_forwarding_enabled` | boolean | true means ask upstream router to forward the configured peer port to transmission using UPnP or NAT-PMP
| `queue_stalled_enabled` | boolean | whether or not to consider idle torrents as stalled
| `queue_stalled_minutes` | number | torrents that are idle for N minuets aren't counted toward `seed_queue_size` or `download_queue_size`
| `rename_partial_files` | boolean | true means append `.part` to incomplete files
| `reqq` | number | the number of outstanding block requests a peer is allowed to queue in the client
| `rpc_version_minimum` | number | the minimum RPC API version supported
| `rpc_version_semver` | string | the current RPC API version in a [semver](https://semver.org)-compatible string
| `rpc_version` | number | the current RPC API version
| `script_torrent_added_enabled` | boolean | whether or not to call the `added` script
| `script_torrent_added_filename` | string | filename of the script to run
| `script_torrent_done_enabled` | boolean | whether or not to call the `done` script
| `script_torrent_done_filename` | string | filename of the script to run
| `script_torrent_done_seeding_enabled` | boolean | whether or not to call the `seeding_done` script
| `script_torrent_done_seeding_filename` | string | filename of the script to run
| `seed_queue_enabled` | boolean | if true, limit how many torrents can be uploaded at once
| `seed_queue_size` | number | max number of torrents to uploaded at once (see `seed_queue_enabled`)
| `seed_ratio_limit` | double | the default seed ratio for torrents to use
| `seed_ratio_limited` | boolean | true if `seed_ratio_limit` is honored by default
| `sequential_download` | boolean | true means sequential download is enabled by default for added torrents
| `session_id` | string | the current `X-Transmission-Session-Id` value
| `speed_limit_down` | number | max global download speed (KBps)
| `speed_limit_down_enabled` | boolean | true means enabled
| `speed_limit_up` | number | max global upload speed (KBps)
| `speed_limit_up_enabled` | boolean | true means enabled
| `start_added_torrents` | boolean | true means added torrents will be started right away
| `tcp_enabled` | boolean | true means allow TCP
| `trash_original_torrent_files` | boolean | true means the .torrent file of added torrents will be deleted
| `units` | object | see below
| `utp_enabled` | boolean | true means allow UTP
| `version` | string | long version string `$version ($revision)`


`units`: an object containing:

| Key | Value Type | transmission.h source
|:--|:--|:--
| `speed_units`  | array  | 4 strings: KB/s, MB/s, GB/s, TB/s
| `speed_bytes`  | number | number of bytes in a KB (1000 for kB; 1024 for KiB)
| `size_units`   | array  | 4 strings: KB/s, MB/s, GB/s, TB/s
| `size_bytes`   | number | number of bytes in a KB (1000 for kB; 1024 for KiB)
| `memory_units` | array  | 4 strings: KB/s, MB/s, GB/s, TB/s
| `memory_bytes` | number | number of bytes in a KB (1000 for kB; 1024 for KiB)

`rpc_version` indicates the RPC interface version supported by the RPC server.
It is incremented when a new version of Transmission changes the RPC interface.

`rpc_version_minimum` indicates the oldest API supported by the RPC server.
It is changes when a new version of Transmission changes the RPC interface
in a way that is not backwards compatible. There are no plans for this
to be common behavior.

#### 4.1.1 Mutators
Method name: `session_set`

Request parameters: the mutable properties from 4.1's parameters, i.e. all of them
except:

* `blocklist_size`
* `config_dir`
* `rpc_version_minimum`,
* `rpc_version_semver`
* `rpc_version`
* `session_id`
* `tcp_enabled`
* `units`
* `version`

Response parameters: none

#### 4.1.2 Accessors
Method name: `session_get`

Request parameters: an optional `fields` array of keys (see 4.1)

Response parameters: key/value pairs matching the request's `fields`
parameter if present, or all supported fields (see 4.1) otherwise.

### 4.2 Session statistics
Method name: `session_stats`

Request parameters: none

Response parameters:

| Key | Value Type | Description
|:--|:--|:--
| `active_torrent_count`     | number
| `download_speed`           | number
| `paused_torrent_count`     | number
| `torrent_count`            | number
| `upload_speed`             | number
| `cumulative_stats`         | stats object (see below)
| `current_stats`            | stats object (see below)

A stats object contains:

| Key | Value Type | transmission.h source
|:--|:--|:--
| `uploaded_bytes`   | number     | tr_session_stats
| `downloaded_bytes` | number     | tr_session_stats
| `files_added`      | number     | tr_session_stats
| `seconds_active`   | number     | tr_session_stats
| `session_count`    | number     | tr_session_stats

### 4.3 Blocklist
Method name: `blocklist_update`

Request parameters: none

Response parameters: a number `blocklist_size`

### 4.4 Port checking
This method tests to see if your incoming peer port is accessible
from the outside world.

Method name: `port_test`

Request parameters: an optional parameter `ip_protocol`.
`ip_protocol` is a string specifying the IP protocol version to be used for the port test.
Set to `ipv4` to check IPv4, or set to `ipv6` to check IPv6.
For backwards compatibility, it is allowed to omit this parameter to get the behaviour before Transmission `4.1.0`,
which is to check whichever IP protocol the OS happened to use to connect to our port test service,
frankly not very useful.

Response parameters:

| Key | Value Type | Description
| :-- | :-- | :--
| `port_is_open` | boolean | true if port is open, false if port is closed
| `ip_protocol` | string | `ipv4` if the test was carried out on IPv4, `ipv6` if the test was carried out on IPv6, unset if it cannot be determined

### 4.5 Session shutdown
This method tells the Transmission session to shut down.

Method name: `session_close`

Request parameters: none

Response parameters: none

### 4.6 Queue movement requests
| Method name | transmission.h source
|:--|:--
| `queue_move_top` | tr_torrentQueueMoveTop()
| `queue_move_up` | tr_torrentQueueMoveUp()
| `queue_move_down` | tr_torrentQueueMoveDown()
| `queue_move_bottom` | tr_torrentQueueMoveBottom()

Request parameters:

| Key | Value Type | Description
|:--|:--|:--
| `ids` | array | torrent list, as described in 3.1.

Response parameters: none

### 4.7 Free space
This method tests how much free space is available in a
client-specified folder.

Method name: `free_space`

Request parameters:

| Key | Value type | Description
|:--|:--|:--
| `path` | string | the directory to query

Response parameters:

| Key | Value type | Description
|:--|:--|:--
| `path` | string | same as the Request parameter
| `size_bytes` | number | the size, in bytes, of the free space in that directory
| `total_size` | number | the total capacity, in bytes, of that directory

### 4.8 Bandwidth groups
#### 4.8.1 Bandwidth group mutator: `group_set`
Method name: `group_set`

Request parameters:

| Key | Value type | Description
|:--|:--|:--
| `honors_session_limits` | boolean  | true if session upload limits are honored
| `name` | string | Bandwidth group name
| `speed_limit_down` | number | max global download speed (KBps)
| `speed_limit_down_enabled` | boolean | true means enabled
| `speed_limit_up` | number | max global upload speed (KBps)
| `speed_limit_up_enabled` | boolean | true means enabled

Response parameters: none

#### 4.8.2 Bandwidth group accessor: `group_get`
Method name: `group_get`

Request parameters: An optional parameter `group`.
`group` is either a string naming the bandwidth group,
or a list of such strings.
If `group` is omitted, all bandwidth groups are used.

Response parameters:

| Key | Value type | Description
|:--|:--|:--
|`group`| array | A list of bandwidth group description objects

A bandwidth group description object has:

| Key | Value type | Description
|:--|:--|:--
| `honors_session_limits` | boolean  | true if session upload limits are honored
| `name` | string | Bandwidth group name
| `speed_limit_down` | number | max global download speed (KBps)
| `speed_limit_down_enabled` | boolean | true means enabled
| `speed_limit_up` | number | max global upload speed (KBps)
| `speed_limit_up_enabled` | boolean | true means enabled

## 5 Protocol versions
This section lists the changes that have been made to the RPC protocol.

There are two ways to check for API compatibility. Since most developers know
[semver](https://semver.org/), `session_get`'s `rpc_version_semver` is the
recommended way. That value is a semver-compatible string of the RPC protocol
version number.

Since Transmission predates the semver 1.0 spec, the previous scheme was for
the RPC version to be a whole number and to increment it whenever a change was
made. That is `session_get`'s `rpc_version`. `rpc_version_minimum` lists the
oldest version that is compatible with the current version; i.e. an app coded
to use `rpc_version_minimum` would still work on a Transmission release running
`rpc_version`.

Breaking changes are denoted with a :bomb: emoji.

Transmission 1.30 (`rpc-version-semver` 1.0.0, `rpc-version`: 1)

Initial revision.

Transmission 1.40 (`rpc-version-semver` 1.1.0, `rpc-version`: 2)

| Method | Description
|:---|:---
| `torrent-get` | new `port` to `peers`

Transmission 1.41 (`rpc-version-semver` 1.2.0, `rpc-version`: 3)

| Method | Description
|:---|:---
| `session-get`      | new arg `version`
| `torrent-get`      | new arg `downloaders`
| `torrent-remove`   | new method

Transmission 1.50 (`rpc-version-semver` 1.3.0, `rpc-version`: 4)

| Method | Description
|:---|:---
|`session-get`       | new arg `rpc-version-minimum`
|`session-get`       | new arg `rpc-version`
|`session-stats`     | added `cumulative-stats`
|`session-stats`     | added `current-stats`
|`torrent-get`       | new arg `downloadDir`

Transmission 1.60 (`rpc-version-semver` 2.0.0, `rpc-version`: 5)

| Method | Description
|:---|:---
| `session-get` | :bomb: renamed `peer-limit` to `peer-limit-global`
| `session-get` | :bomb: renamed `pex-allowed` to `pex-enabled`
| `session-get` | :bomb: renamed `port` to `peer-port`
| `torrent-get` | :bomb: removed arg `downloadLimitMode`
| `torrent-get` | :bomb: removed arg `uploadLimitMode`
| `torrent-set` | :bomb: renamed `speed-limit-down-enabled` to `downloadLimited`
| `torrent-set` | :bomb: renamed `speed-limit-down` to `downloadLimit`
| `torrent-set` | :bomb: renamed `speed-limit-up-enabled` to `uploadLimited`
| `torrent-set` | :bomb: renamed `speed-limit-up` to `uploadLimit`
| `blocklist-update` | new method
| `port-test` | new method
| `session-get` | new arg `alt-speed-begin`
| `session-get` | new arg `alt-speed-down`
| `session-get` | new arg `alt-speed-enabled`
| `session-get` | new arg `alt-speed-end`
| `session-get` | new arg `alt-speed-time-enabled`
| `session-get` | new arg `alt-speed-up`
| `session-get` | new arg `blocklist-enabled`
| `session-get` | new arg `blocklist-size`
| `session-get` | new arg `peer-limit-per-torrent`
| `session-get` | new arg `seedRatioLimit`
| `session-get` | new arg `seedRatioLimited`
| `torrent-add` | new arg `files-unwanted`
| `torrent-add` | new arg `files-wanted`
| `torrent-add` | new arg `priority-high`
| `torrent-add` | new arg `priority-low`
| `torrent-add` | new arg `priority-normal`
| `torrent-get` | new arg `bandwidthPriority`
| `torrent-get` | new arg `fileStats`
| `torrent-get` | new arg `honorsSessionLimits`
| `torrent-get` | new arg `percentDone`
| `torrent-get` | new arg `pieces`
| `torrent-get` | new arg `seedRatioLimit`
| `torrent-get` | new arg `seedRatioMode`
| `torrent-get` | new arg `torrentFile`
| `torrent-get` | new ids option `recently-active`
| `torrent-reannounce` | new method
| `torrent-set` | new arg `bandwidthPriority`
| `torrent-set` | new arg `honorsSessionLimits`
| `torrent-set` | new arg `seedRatioLimit`
| `torrent-set` | new arg `seedRatioLimited`

Transmission 1.70 (`rpc-version-semver` 2.1.0, `rpc-version`: 6)

| Method | Description
|:---|:---
| method `torrent-set-location` | new method

Transmission 1.80 (`rpc-version-semver` 3.0.0, `rpc-version`: 7)

| Method | Description
|:---|:---
| `torrent-get` | :bomb: removed arg `announceResponse` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `announceURL` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `downloaders` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `lastAnnounceTime` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `lastScrapeTime` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `leechers` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `nextAnnounceTime` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `nextScrapeTime` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `scrapeResponse` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `scrapeURL` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `seeders` (use `trackerStats instead`)
| `torrent-get` | :bomb: removed arg `swarmSpeed`
| `torrent-get` | :bomb: removed arg `timesCompleted` (use `trackerStats instead`)
| `session-set` | new arg `incomplete-dir-enabled`
| `session-set` | new arg `incomplete-dir`
| `torrent-get` | new arg `magnetLink`
| `torrent-get` | new arg `metadataPercentComplete`
| `torrent-get` | new arg `trackerStats`

Transmission 1.90 (`rpc-version-semver` 3.1.0, `rpc-version`: 8)

| Method | Description
|:---|:---
| `session-set` | new arg `rename-partial-files`
| `session-get` | new arg `rename-partial-files`
| `session-get` | new arg `config-dir`
| `torrent-add` | new arg `bandwidthPriority`
| `torrent-get` | new trackerStats arg `lastAnnounceTimedOut`

Transmission 1.92 (`rpc-version-semver` 3.2.0, `rpc-version`: 8)

Note: `rpc-version` was not bumped in this release due to an oversight.

| Method | Description
|:---|:---
| `torrent-get` | new trackerStats arg `lastScrapeTimedOut`

Transmission 2.00 (`rpc-version-semver` 3.3.0, `rpc-version`: 9)

| Method | Description
|:---|:---
| `session-set` | new arg `start-added-torrents`
| `session-set` | new arg `trash-original-torrent-files`
| `session-get` | new arg `start-added-torrents`
| `session-get` | new arg `trash-original-torrent-files`
| `torrent-get` | new arg `isFinished`

Transmission 2.10 (`rpc-version-semver` 3.4.0, `rpc-version`: 10)

| Method | Description
|:---|:---
| `session-get` | new arg `cache-size-mb`
| `session-get` | new arg `units`
| `session-set` | new arg `idle-seeding-limit-enabled`
| `session-set` | new arg `idle-seeding-limit`
| `torrent-set` | new arg `seedIdleLimit`
| `torrent-set` | new arg `seedIdleMode`
| `torrent-set` | new arg `trackerAdd`
| `torrent-set` | new arg `trackerRemove`
| `torrent-set` | new arg `trackerReplace`

Transmission 2.12 (`rpc-version-semver` 3.5.0, `rpc-version`: 11)

| Method | Description
|:---|:---
| `session-get` | new arg `blocklist-url`
| `session-set` | new arg `blocklist-url`

Transmission 2.20 (`rpc-version-semver` 3.6.0, `rpc-version`: 12)

| Method | Description
|:---|:---
| `session-get` | new arg `download-dir-free-space`
| `session-close` | new method

Transmission 2.30 (`rpc-version-semver` 4.0.0, `rpc-version`: 13)

| Method | Description
|:---|:---
| `torrent-get` | :bomb: removed arg `peersKnown`
| `torrent-get` | new arg `isUTP` to the `peers` list
| `torrent-add` | new arg `cookies`

Transmission 2.40 (`rpc-version-semver` 5.0.0, `rpc-version`: 14)

| Method | Description
|:---|:---
| `torrent-get` | :bomb: values of `status` field changed
| `queue-move-bottom` | new method
| `queue-move-down` | new method
| `queue-move-top` | new method
| `session-set` | new arg `download-queue-enabled`
| `session-set` | new arg `download-queue-size`
| `session-set` | new arg `queue-stalled-enabled`
| `session-set` | new arg `queue-stalled-minutes`
| `session-set` | new arg `seed-queue-enabled`
| `session-set` | new arg `seed-queue-size`
| `torrent-get` | new arg `fromLpd` in `peersFrom`
| `torrent-get` | new arg `isStalled`
| `torrent-get` | new arg `queuePosition`
| `torrent-set` | new arg `queuePosition`
| `torrent-start-now` | new method

Transmission 2.80 (`rpc-version-semver` 5.1.0, `rpc-version`: 15)

| Method | Description
|:---|:---
| `torrent-get`         | new arg `etaIdle`
| `torrent-rename-path` | new method
| `free-space`          | new method
| `torrent-add`         | new return arg `torrent-duplicate`

Transmission 3.00 (`rpc-version-semver` 5.2.0, `rpc-version`: 16)

| Method | Description
|:---|:---
| `session-get` | new request arg `fields`
| `session-get` | new arg `session-id`
| `torrent-get` | new arg `labels`
| `torrent-set` | new arg `labels`
| `torrent-get` | new arg `editDate`
| `torrent-get` | new request arg `format`

Transmission 4.0.0 (`rpc-version-semver` 5.3.0, `rpc-version`: 17)

| Method | Description
|:---|:---
| `/upload` | :warning: undocumented `/upload` endpoint removed
| `session-get` | :warning: **DEPRECATED** `download-dir-free-space`. Use `free-space` instead.
| `free-space` | new return arg `total_size`
| `session-get` | new arg `default-trackers`
| `session-get` | new arg `rpc-version-semver`
| `session-get` | new arg `script-torrent-added-enabled`
| `session-get` | new arg `script-torrent-added-filename`
| `session-get` | new arg `script-torrent-done-seeding-enabled`
| `session-get` | new arg `script-torrent-done-seeding-filename`
| `torrent-add` | new arg `labels`
| `torrent-get` | new arg `availability`
| `torrent-get` | new arg `file-count`
| `torrent-get` | new arg `group`
| `torrent-get` | new arg `percentComplete`
| `torrent-get` | new arg `primary-mime-type`
| `torrent-get` | new arg `tracker.sitename`
| `torrent-get` | new arg `trackerStats.sitename`
| `torrent-get` | new arg `trackerList`
| `torrent-set` | new arg `group`
| `torrent-set` | new arg `trackerList`
| `torrent-set` | :warning: **DEPRECATED** `trackerAdd`. Use `trackerList` instead.
| `torrent-set` | :warning: **DEPRECATED** `trackerRemove`. Use `trackerList` instead.
| `torrent-set` | :warning: **DEPRECATED** `trackerReplace`. Use `trackerList` instead.
| `group-set` | new method
| `group-get` | new method
| `torrent-get` | :warning: old arg `wanted` was implemented as an array of `0` or `1` in Transmission 3.00 and older, despite being documented as an array of booleans. Transmission 4.0.0 and 4.0.1 "fixed" this by returning an array of booleans; but in practical terms, this change caused an unannounced breaking change for any 3rd party code that expected `0` or `1`. For this reason, 4.0.2 restored the 3.00 behavior and updated this spec to match the code.

Transmission 4.1.0 (`rpc_version_semver` 6.0.0, `rpc_version`: 18)

:bomb: switch to the JSON-RPC 2.0 protocol
:bomb: switch to snake_case for all strings

| Method | Description
|:---|:---
| `torrent_get` | new arg `sequential_download`
| `session_set` | new arg `sequential_download`
| `torrent_add` | new arg `sequential_download`
| `torrent_get` | new arg `sequential_download`
| `torrent_set` | new arg `sequential_download`
| `torrent_get` | new arg `files.begin_piece`
| `torrent_get` | new arg `files.end_piece`
| `port_test` | new arg `ip_protocol`
