Users can set environmental variables to override Transmission's default behavior and for debugging.

## Transmission-Specific Variables
 * If `TRANSMISSION_HOME` is set, Transmission will look there for its settings instead of in the [default location](Configuration-Files.md#Locations).
 * If `TRANSMISSION_WEB_HOME` is set, Transmission will look there for the [Web Interface](Web-Interface.md) files, such as the JavaScript, HTML, and graphics files.
 * If `TR_CURL_SSL_NO_VERIFY` is set, Transmission will not validate SSL certificate for HTTPS connections when talking to trackers. See CURL's documentation ([CURLOPT_SSL_VERIFYHOST](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html) and [CURLOPT_SSL_VERIFYPEER](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html)) for more details.
 * If `TR_CURL_VERBOSE` is set, debugging information for libcurl will be enabled.  More information about libcurl's debugging mode [is available here](https://curl.haxx.se/libcurl/c/curl_easy_setopt.html#CURLOPTVERBOSE).
 * If `TR_DEBUG_FD` is set to an integer, that integer is treated as a [file descriptor](https://en.wikipedia.org/wiki/File_descriptor) and very verbose debugging information is written to it.  For example, here is how to turn on debugging and save it to a file named "runlog" when running Transmission from a bash shell:
   ```console
   $ export TR_DEBUG_FD=2
   $ transmission 2>runlog
   ```
 * If `TR_DHT_VERBOSE` is set, Transmission will log all of the DHT's activities in excruciating detail to standard error.

## Standard Variables Used by Transmission
 * If `TRANSMISSION_WEB_HOME` is _not_ set, non-Mac platforms will look for the [Web Interface](Web-Interface.md) files in `XDG_DATA_HOME` and in `XDG_DATA_DIRS` as described in [the XDG Base Directory Specification](https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html#variables). `XDG_DATA_HOME` has a default value of `$HOME/.local/share/`.
 * If `TRANSMISSION_HOME` is _not_ set, Unix-based versions of Transmission will look for their settings in `$XDG_CONFIG_HOME/transmission/`. `XDG_CONFIG_HOME` has a default value of `$HOME/.config/`.
 * If `HOME` is set, it is used in three ways:
   1. By the `XDG` variables, as described above.
   2. If `TRANSMISSION_HOME` is _not_ set, Mac-based versions of Transmission will look for their settings in `$HOME/Library/Application Support/Transmission`.
   3. `$HOME/Downloads` is the default download directory.

## Standard Variables Used by Other Tools
 * Transmission uses the [libcurl](https://curl.haxx.se/libcurl/) library for HTTP- and HTTPS-based tracker announces and scrapes. Transmission does not support proxies, but libcurl itself honors [a handful of environment variables](https://curl.haxx.se/libcurl/c/curl_easy_setopt.html#CURLOPTPROXY) to customize _its_ proxy behavior.
