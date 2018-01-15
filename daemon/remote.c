/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isspace */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strcmp */

#include <event2/buffer.h>

#define CURL_DISABLE_TYPECHECK /* otherwise -Wunreachable-code goes insane */
#include <curl/curl.h>

#include <libtransmission/transmission.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/log.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>

#define MY_NAME "transmission-remote"
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT atoi (TR_DEFAULT_RPC_PORT_STR)
#define DEFAULT_URL TR_DEFAULT_RPC_URL_STR "rpc/"

#define ARGUMENTS TR_KEY_arguments

#define MEM_K 1024
#define MEM_B_STR   "B"
#define MEM_K_STR "KiB"
#define MEM_M_STR "MiB"
#define MEM_G_STR "GiB"
#define MEM_T_STR "TiB"

#define DISK_K 1000
#define DISK_B_STR  "B"
#define DISK_K_STR "kB"
#define DISK_M_STR "MB"
#define DISK_G_STR "GB"
#define DISK_T_STR "TB"

#define SPEED_K 1000
#define SPEED_B_STR  "B/s"
#define SPEED_K_STR "kB/s"
#define SPEED_M_STR "MB/s"
#define SPEED_G_STR "GB/s"
#define SPEED_T_STR "TB/s"

/***
****
****  Display Utilities
****
***/

static void
etaToString (char *  buf, size_t  buflen, int64_t eta)
{
    if (eta < 0)
        tr_snprintf (buf, buflen, "Unknown");
    else if (eta < 60)
        tr_snprintf (buf, buflen, "%" PRId64 " sec", eta);
    else if (eta < (60 * 60))
        tr_snprintf (buf, buflen, "%" PRId64 " min", eta / 60);
    else if (eta < (60 * 60 * 24))
        tr_snprintf (buf, buflen, "%" PRId64 " hrs", eta / (60 * 60));
    else
        tr_snprintf (buf, buflen, "%" PRId64 " days", eta / (60 * 60 * 24));
}

static char*
tr_strltime (char * buf, int seconds, size_t buflen)
{
    int  days, hours, minutes, total_seconds;
    char b[128], d[128], h[128], m[128], s[128], t[128];

    if (seconds < 0)
        seconds = 0;

    total_seconds = seconds;
    days = seconds / 86400;
    hours = (seconds % 86400) / 3600;
    minutes = (seconds % 3600) / 60;
    seconds = (seconds % 3600) % 60;

    tr_snprintf (d, sizeof (d), "%d %s", days, days==1?"day":"days");
    tr_snprintf (h, sizeof (h), "%d %s", hours, hours==1?"hour":"hours");
    tr_snprintf (m, sizeof (m), "%d %s", minutes, minutes==1?"minute":"minutes");
    tr_snprintf (s, sizeof (s), "%d %s", seconds, seconds==1?"second":"seconds");
    tr_snprintf (t, sizeof (t), "%d %s", total_seconds, total_seconds==1?"second":"seconds");

    if (days)
    {
        if (days >= 4 || !hours)
            tr_strlcpy (b, d, sizeof (b));
        else
            tr_snprintf (b, sizeof (b), "%s, %s", d, h);
    }
    else if (hours)
    {
        if (hours >= 4 || !minutes)
            tr_strlcpy (b, h, sizeof (b));
        else
            tr_snprintf (b, sizeof (b), "%s, %s", h, m);
    }
    else if (minutes)
    {
        if (minutes >= 4 || !seconds)
            tr_strlcpy (b, m, sizeof (b));
        else
            tr_snprintf (b, sizeof (b), "%s, %s", m, s);
    }
    else tr_strlcpy (b, s, sizeof (b));

    tr_snprintf (buf, buflen, "%s (%s)", b, t);
    return buf;
}

static char*
strlpercent (char * buf, double x, size_t buflen)
{
    return tr_strpercent (buf, x, buflen);
}

static char*
strlratio2 (char * buf, double ratio, size_t buflen)
{
    return tr_strratio (buf, buflen, ratio, "Inf");
}

static char*
strlratio (char * buf, int64_t numerator, int64_t denominator, size_t buflen)
{
    double ratio;

    if (denominator != 0)
        ratio = numerator / (double)denominator;
    else if (numerator != 0)
        ratio = TR_RATIO_INF;
    else
        ratio = TR_RATIO_NA;

    return strlratio2 (buf, ratio, buflen);
}

static char*
strlmem (char * buf, int64_t bytes, size_t buflen)
{
    if (!bytes)
        tr_strlcpy (buf, "None", buflen);
    else
        tr_formatter_mem_B (buf, bytes, buflen);

    return buf;
}

static char*
strlsize (char * buf, int64_t bytes, size_t buflen)
{
    if (bytes < 0)
        tr_strlcpy (buf, "Unknown", buflen);
    else if (bytes == 0)
        tr_strlcpy (buf, "None", buflen);
    else
        tr_formatter_size_B (buf, bytes, buflen);

    return buf;
}

enum
{
    TAG_SESSION,
    TAG_STATS,
    TAG_DETAILS,
    TAG_FILES,
    TAG_LIST,
    TAG_PEERS,
    TAG_PIECES,
    TAG_PORTTEST,
    TAG_TORRENT_ADD,
    TAG_TRACKERS
};

static const char*
getUsage (void)
{
    return
        MY_NAME" "LONG_VERSION_STRING"\n"
        "A fast and easy BitTorrent client\n"
        "https://transmissionbt.com/\n"
        "\n"
        "Usage: " MY_NAME
        " [host] [options]\n"
        "       "
        MY_NAME " [port] [options]\n"
                "       "
        MY_NAME " [host:port] [options]\n"
                "       "
        MY_NAME " [http(s?)://host:port/transmission/] [options]\n"
                "\n"
                "See the man page for detailed explanations and many examples.";
}

/***
****
****  Command-Line Arguments
****
***/

static tr_option opts[] =
{
    { 'a', "add",                    "Add torrent files by filename or URL", "a",  0, NULL },
    { 970, "alt-speed",              "Use the alternate Limits", "as",  0, NULL },
    { 971, "no-alt-speed",           "Don't use the alternate Limits", "AS",  0, NULL },
    { 972, "alt-speed-downlimit",    "max alternate download speed (in "SPEED_K_STR")", "asd",  1, "<speed>" },
    { 973, "alt-speed-uplimit",      "max alternate upload speed (in "SPEED_K_STR")", "asu",  1, "<speed>" },
    { 974, "alt-speed-scheduler",    "Use the scheduled on/off times", "asc",  0, NULL },
    { 975, "no-alt-speed-scheduler", "Don't use the scheduled on/off times", "ASC",  0, NULL },
    { 976, "alt-speed-time-begin",   "Time to start using the alt speed limits (in hhmm)", NULL,  1, "<time>" },
    { 977, "alt-speed-time-end",     "Time to stop using the alt speed limits (in hhmm)", NULL,  1, "<time>" },
    { 978, "alt-speed-days",         "Numbers for any/all days of the week - eg. \"1-7\"", NULL,  1, "<days>" },
    { 963, "blocklist-update",       "Blocklist update", NULL, 0, NULL },
    { 'c', "incomplete-dir",         "Where to store new torrents until they're complete", "c", 1, "<dir>" },
    { 'C', "no-incomplete-dir",      "Don't store incomplete torrents in a different location", "C", 0, NULL },
    { 'b', "debug",                  "Print debugging information", "b",  0, NULL },
    { 'd', "downlimit",              "Set the max download speed in "SPEED_K_STR" for the current torrent(s) or globally", "d", 1, "<speed>" },
    { 'D', "no-downlimit",           "Disable max download speed for the current torrent(s) or globally", "D", 0, NULL },
    { 'e', "cache",                  "Set the maximum size of the session's memory cache (in " MEM_M_STR ")", "e", 1, "<size>" },
    { 910, "encryption-required",    "Encrypt all peer connections", "er", 0, NULL },
    { 911, "encryption-preferred",   "Prefer encrypted peer connections", "ep", 0, NULL },
    { 912, "encryption-tolerated",   "Prefer unencrypted peer connections", "et", 0, NULL },
    { 850, "exit",                   "Tell the transmission session to shut down", NULL, 0, NULL },
    { 940, "files",                  "List the current torrent(s)' files", "f",  0, NULL },
    { 'g', "get",                    "Mark files for download", "g",  1, "<files>" },
    { 'G', "no-get",                 "Mark files for not downloading", "G",  1, "<files>" },
    { 'i', "info",                   "Show the current torrent(s)' details", "i",  0, NULL },
    { 940, "info-files",             "List the current torrent(s)' files", "if",  0, NULL },
    { 941, "info-peers",             "List the current torrent(s)' peers", "ip",  0, NULL },
    { 942, "info-pieces",            "List the current torrent(s)' pieces", "ic",  0, NULL },
    { 943, "info-trackers",          "List the current torrent(s)' trackers", "it",  0, NULL },
    { 920, "session-info",           "Show the session's details", "si", 0, NULL },
    { 921, "session-stats",          "Show the session's statistics", "st", 0, NULL },
    { 'l', "list",                   "List all torrents", "l",  0, NULL },
    { 960, "move",                   "Move current torrent's data to a new folder", NULL, 1, "<path>" },
    { 961, "find",                   "Tell Transmission where to find a torrent's data", NULL, 1, "<path>" },
    { 'm', "portmap",                "Enable portmapping via NAT-PMP or UPnP", "m",  0, NULL },
    { 'M', "no-portmap",             "Disable portmapping", "M",  0, NULL },
    { 'n', "auth",                   "Set username and password", "n",  1, "<user:pw>" },
    { 810, "authenv",                "Set authentication info from the TR_AUTH environment variable (user:pw)", "ne", 0, NULL },
    { 'N', "netrc",                  "Set authentication info from a .netrc file", "N",  1, "<file>" },
    { 820, "ssl",                    "Use SSL when talking to daemon", NULL,  0, NULL },
    { 'o', "dht",                    "Enable distributed hash tables (DHT)", "o", 0, NULL },
    { 'O', "no-dht",                 "Disable distributed hash tables (DHT)", "O", 0, NULL },
    { 'p', "port",                   "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "p", 1, "<port>" },
    { 962, "port-test",              "Port testing", "pt", 0, NULL },
    { 'P', "random-port",            "Random port for incoming peers", "P", 0, NULL },
    { 900, "priority-high",          "Try to download these file(s) first", "ph", 1, "<files>" },
    { 901, "priority-normal",        "Try to download these file(s) normally", "pn", 1, "<files>" },
    { 902, "priority-low",           "Try to download these file(s) last", "pl", 1, "<files>" },
    { 700, "bandwidth-high",         "Give this torrent first chance at available bandwidth", "Bh", 0, NULL },
    { 701, "bandwidth-normal",       "Give this torrent bandwidth left over by high priority torrents", "Bn", 0, NULL },
    { 702, "bandwidth-low",          "Give this torrent bandwidth left over by high and normal priority torrents", "Bl", 0, NULL },
    { 600, "reannounce",             "Reannounce the current torrent(s)", NULL,  0, NULL },
    { 'r', "remove",                 "Remove the current torrent(s)", "r",  0, NULL },
    { 930, "peers",                  "Set the maximum number of peers for the current torrent(s) or globally", "pr", 1, "<max>" },
    { 840, "remove-and-delete",      "Remove the current torrent(s) and delete local data", "rad", 0, NULL },
    { 800, "torrent-done-script",    "Specify a script to run when a torrent finishes", NULL, 1, "<file>" },
    { 801, "no-torrent-done-script", "Don't run a script when torrents finish", NULL, 0, NULL },
    { 950, "seedratio",              "Let the current torrent(s) seed until a specific ratio", "sr", 1, "ratio" },
    { 951, "seedratio-default",      "Let the current torrent(s) use the global seedratio settings", "srd", 0, NULL },
    { 952, "no-seedratio",           "Let the current torrent(s) seed regardless of ratio", "SR", 0, NULL },
    { 953, "global-seedratio",       "All torrents, unless overridden by a per-torrent setting, should seed until a specific ratio", "gsr", 1, "ratio" },
    { 954, "no-global-seedratio",    "All torrents, unless overridden by a per-torrent setting, should seed regardless of ratio", "GSR", 0, NULL },
    { 710, "tracker-add",            "Add a tracker to a torrent", "td", 1, "<tracker>" },
    { 712, "tracker-remove",         "Remove a tracker from a torrent", "tr", 1, "<trackerId>" },
    { 's', "start",                  "Start the current torrent(s)", "s",  0, NULL },
    { 'S', "stop",                   "Stop the current torrent(s)", "S",  0, NULL },
    { 't', "torrent",                "Set the current torrent(s)", "t",  1, "<torrent>" },
    { 990, "start-paused",           "Start added torrents paused", NULL, 0, NULL },
    { 991, "no-start-paused",        "Start added torrents unpaused", NULL, 0, NULL },
    { 992, "trash-torrent",          "Delete torrents after adding", NULL, 0, NULL },
    { 993, "no-trash-torrent",       "Do not delete torrents after adding", NULL, 0, NULL },
    { 984, "honor-session",          "Make the current torrent(s) honor the session limits", "hl",  0, NULL },
    { 985, "no-honor-session",       "Make the current torrent(s) not honor the session limits", "HL",  0, NULL },
    { 'u', "uplimit",                "Set the max upload speed in "SPEED_K_STR" for the current torrent(s) or globally", "u", 1, "<speed>" },
    { 'U', "no-uplimit",             "Disable max upload speed for the current torrent(s) or globally", "U", 0, NULL },
    { 830, "utp",                    "Enable uTP for peer connections", NULL, 0, NULL },
    { 831, "no-utp",                 "Disable uTP for peer connections", NULL, 0, NULL },
    { 'v', "verify",                 "Verify the current torrent(s)", "v",  0, NULL },
    { 'V', "version",                "Show version number and exit", "V", 0, NULL },
    { 'w', "download-dir",           "When used in conjunction with --add, set the new torrent's download folder. Otherwise, set the default download folder", "w",  1, "<path>" },
    { 'x', "pex",                    "Enable peer exchange (PEX)", "x",  0, NULL },
    { 'X', "no-pex",                 "Disable peer exchange (PEX)", "X",  0, NULL },
    { 'y', "lpd",                    "Enable local peer discovery (LPD)", "y",  0, NULL },
    { 'Y', "no-lpd",                 "Disable local peer discovery (LPD)", "Y",  0, NULL },
    { 941, "peer-info",              "List the current torrent(s)' peers", "pi",  0, NULL },
    {   0, NULL,                     NULL, NULL, 0, NULL }
};

static void
showUsage (void)
{
    tr_getopt_usage (MY_NAME, getUsage (), opts);
}

static int
numarg (const char * arg)
{
  char * end = NULL;
  const long num = strtol (arg, &end, 10);

  if (*end)
    {
      fprintf (stderr, "Not a number: \"%s\"\n", arg);
      showUsage ();
      exit (EXIT_FAILURE);
    }

  return num;
}

enum
{
  MODE_TORRENT_START         = (1<<0),
  MODE_TORRENT_STOP          = (1<<1),
  MODE_TORRENT_VERIFY        = (1<<2),
  MODE_TORRENT_REANNOUNCE    = (1<<3),
  MODE_TORRENT_SET           = (1<<4),
  MODE_TORRENT_GET           = (1<<5),
  MODE_TORRENT_ADD           = (1<<6),
  MODE_TORRENT_REMOVE        = (1<<7),
  MODE_TORRENT_SET_LOCATION  = (1<<8),
  MODE_SESSION_SET           = (1<<9),
  MODE_SESSION_GET           = (1<<10),
  MODE_SESSION_STATS         = (1<<11),
  MODE_SESSION_CLOSE         = (1<<12),
  MODE_BLOCKLIST_UPDATE      = (1<<13),
  MODE_PORT_TEST             = (1<<14)
};

static int
getOptMode (int val)
{
  switch (val)
    {
      case TR_OPT_ERR:
      case TR_OPT_UNK:
      case 'a': /* add torrent */
      case 'b': /* debug */
      case 'n': /* auth */
      case 810: /* authenv */
      case 'N': /* netrc */
      case 820: /* UseSSL */
      case 't': /* set current torrent */
      case 'V': /* show version number */
        return 0;

      case 'c': /* incomplete-dir */
      case 'C': /* no-incomplete-dir */
      case 'e': /* cache */
      case 'm': /* portmap */
      case 'M': /* "no-portmap */
      case 'o': /* dht */
      case 'O': /* no-dht */
      case 'p': /* incoming peer port */
      case 'P': /* random incoming peer port */
      case 'x': /* pex */
      case 'X': /* no-pex */
      case 'y': /* lpd */
      case 'Y': /* no-lpd */
      case 800: /* torrent-done-script */
      case 801: /* no-torrent-done-script */
      case 830: /* utp */
      case 831: /* no-utp */
      case 970: /* alt-speed */
      case 971: /* no-alt-speed */
      case 972: /* alt-speed-downlimit */
      case 973: /* alt-speed-uplimit */
      case 974: /* alt-speed-scheduler */
      case 975: /* no-alt-speed-scheduler */
      case 976: /* alt-speed-time-begin */
      case 977: /* alt-speed-time-end */
      case 978: /* alt-speed-days */
      case 910: /* encryption-required */
      case 911: /* encryption-preferred */
      case 912: /* encryption-tolerated */
      case 953: /* global-seedratio */
      case 954: /* no-global-seedratio */
      case 990: /* start-paused */
      case 991: /* no-start-paused */
      case 992: /* trash-torrent */
      case 993: /* no-trash-torrent */
        return MODE_SESSION_SET;

      case 712: /* tracker-remove */
      case 950: /* seedratio */
      case 951: /* seedratio-default */
      case 952: /* no-seedratio */
      case 984: /* honor-session */
      case 985: /* no-honor-session */
        return MODE_TORRENT_SET;

      case 920: /* session-info */
        return MODE_SESSION_GET;

      case 'g': /* get */
      case 'G': /* no-get */
      case 700: /* torrent priority-high */
      case 701: /* torrent priority-normal */
      case 702: /* torrent priority-low */
      case 710: /* tracker-add */
      case 900: /* file priority-high */
      case 901: /* file priority-normal */
      case 902: /* file priority-low */
        return MODE_TORRENT_SET | MODE_TORRENT_ADD;

      case 961: /* find */
        return MODE_TORRENT_SET_LOCATION | MODE_TORRENT_ADD;

      case 'i': /* info */
      case 'l': /* list all torrents */
      case 940: /* info-files */
      case 941: /* info-peer */
      case 942: /* info-pieces */
      case 943: /* info-tracker */
        return MODE_TORRENT_GET;

      case 'd': /* download speed limit */
      case 'D': /* no download speed limit */
      case 'u': /* upload speed limit */
      case 'U': /* no upload speed limit */
      case 930: /* peers */
        return MODE_SESSION_SET | MODE_TORRENT_SET;

      case 's': /* start */
        return MODE_TORRENT_START | MODE_TORRENT_ADD;

      case 'S': /* stop */
        return MODE_TORRENT_STOP | MODE_TORRENT_ADD;

      case 'w': /* download-dir */
        return MODE_SESSION_SET | MODE_TORRENT_ADD;

      case 850: /* session-close */
        return MODE_SESSION_CLOSE;

      case 963: /* blocklist-update */
        return MODE_BLOCKLIST_UPDATE;

      case 921: /* session-stats */
        return MODE_SESSION_STATS;

      case 'v': /* verify */
        return MODE_TORRENT_VERIFY;

      case 600: /* reannounce */
        return MODE_TORRENT_REANNOUNCE;

      case 962: /* port-test */
        return MODE_PORT_TEST;

      case 'r': /* remove */
      case 840: /* remove and delete */
        return MODE_TORRENT_REMOVE;

      case 960: /* move */
        return MODE_TORRENT_SET_LOCATION;

      default:
        fprintf (stderr, "unrecognized argument %d\n", val);
        assert ("unrecognized argument" && 0);
        return 0;
    }
}

static bool debug = 0;
static char * auth = NULL;
static char * netrc = NULL;
static char * sessionId = NULL;
static bool UseSSL = false;

static char*
getEncodedMetainfo (const char * filename)
{
    size_t    len = 0;
    char *    b64 = NULL;
    uint8_t * buf = tr_loadFile (filename, &len, NULL);

    if (buf)
    {
        b64 = tr_base64_encode (buf, len, NULL);
        tr_free (buf);
    }
    return b64;
}

static void
addIdArg (tr_variant * args, const char * id, const char * fallback)
{
  if (!id || !*id)
    {
      id = fallback;

      if (!id || !*id)
        {
          fprintf (stderr, "No torrent specified!  Please use the -t option first.\n");
          id = "-1"; /* no torrent will have this ID, so will act as a no-op */
        }
    }

  if (!tr_strcmp0 (id, "active"))
    {
      tr_variantDictAddStr (args, TR_KEY_ids, "recently-active");
    }
  else if (strcmp (id, "all"))
    {
      const char * pch;
      bool isList = strchr (id,',') || strchr (id,'-');
      bool isNum = true;

      for (pch=id; isNum && *pch; ++pch)
        if (!isdigit (*pch))
          isNum = false;

      if (isNum || isList)
        tr_rpc_parse_list_str (tr_variantDictAdd (args, TR_KEY_ids), id, strlen (id));
      else
        tr_variantDictAddStr (args, TR_KEY_ids, id); /* it's a torrent sha hash */
    }
}

static void
addTime (tr_variant * args, const tr_quark key, const char * arg)
{
    int time;
    bool success = false;

    if (arg && (strlen (arg) == 4))
    {
        const char hh[3] = { arg[0], arg[1], '\0' };
        const char mm[3] = { arg[2], arg[3], '\0' };
        const int hour = atoi (hh);
        const int min = atoi (mm);

        if (0<=hour && hour<24 && 0<=min && min<60)
        {
            time = min + (hour * 60);
            success = true;
        }
    }

    if (success)
        tr_variantDictAddInt (args, key, time);
    else
        fprintf (stderr, "Please specify the time of day in 'hhmm' format.\n");
}

static void
addDays (tr_variant * args, const tr_quark key, const char * arg)
{
  int days = 0;

  if (arg)
    {
      int i;
      int valueCount;
      int * values;

      values = tr_parseNumberRange (arg, TR_BAD_SIZE, &valueCount);
      for (i=0; i<valueCount; ++i)
        {
          if (values[i] < 0 || values[i] > 7)
            continue;

          if (values[i] == 7)
            values[i] = 0;

          days |= 1 << values[i];
        }

      tr_free (values);
    }

  if (days)
    tr_variantDictAddInt (args, key, days);
  else
    fprintf (stderr, "Please specify the days of the week in '1-3,4,7' format.\n");
}

static void
addFiles (tr_variant      * args,
          const tr_quark    key,
          const char      * arg)
{
  tr_variant * files = tr_variantDictAddList (args, key, 100);

  if (!*arg)
    {
      fprintf (stderr, "No files specified!\n");
      arg = "-1"; /* no file will have this index, so should be a no-op */
    }

  if (strcmp (arg, "all"))
    {
      int i;
      int valueCount;
      int * values = tr_parseNumberRange (arg, TR_BAD_SIZE, &valueCount);

      for (i=0; i<valueCount; ++i)
        tr_variantListAddInt (files, values[i]);

      tr_free (values);
    }
}

#define TR_N_ELEMENTS(ary) (sizeof (ary) / sizeof (*ary))

static const tr_quark files_keys[] = {
    TR_KEY_files,
    TR_KEY_name,
    TR_KEY_priorities,
    TR_KEY_wanted
};

static const tr_quark details_keys[] = {
    TR_KEY_activityDate,
    TR_KEY_addedDate,
    TR_KEY_bandwidthPriority,
    TR_KEY_comment,
    TR_KEY_corruptEver,
    TR_KEY_creator,
    TR_KEY_dateCreated,
    TR_KEY_desiredAvailable,
    TR_KEY_doneDate,
    TR_KEY_downloadDir,
    TR_KEY_downloadedEver,
    TR_KEY_downloadLimit,
    TR_KEY_downloadLimited,
    TR_KEY_error,
    TR_KEY_errorString,
    TR_KEY_eta,
    TR_KEY_hashString,
    TR_KEY_haveUnchecked,
    TR_KEY_haveValid,
    TR_KEY_honorsSessionLimits,
    TR_KEY_id,
    TR_KEY_isFinished,
    TR_KEY_isPrivate,
    TR_KEY_leftUntilDone,
    TR_KEY_magnetLink,
    TR_KEY_name,
    TR_KEY_peersConnected,
    TR_KEY_peersGettingFromUs,
    TR_KEY_peersSendingToUs,
    TR_KEY_peer_limit,
    TR_KEY_pieceCount,
    TR_KEY_pieceSize,
    TR_KEY_rateDownload,
    TR_KEY_rateUpload,
    TR_KEY_recheckProgress,
    TR_KEY_secondsDownloading,
    TR_KEY_secondsSeeding,
    TR_KEY_seedRatioMode,
    TR_KEY_seedRatioLimit,
    TR_KEY_sizeWhenDone,
    TR_KEY_startDate,
    TR_KEY_status,
    TR_KEY_totalSize,
    TR_KEY_uploadedEver,
    TR_KEY_uploadLimit,
    TR_KEY_uploadLimited,
    TR_KEY_webseeds,
    TR_KEY_webseedsSendingToUs
};

static const tr_quark list_keys[] = {
    TR_KEY_error,
    TR_KEY_errorString,
    TR_KEY_eta,
    TR_KEY_id,
    TR_KEY_isFinished,
    TR_KEY_leftUntilDone,
    TR_KEY_name,
    TR_KEY_peersGettingFromUs,
    TR_KEY_peersSendingToUs,
    TR_KEY_rateDownload,
    TR_KEY_rateUpload,
    TR_KEY_sizeWhenDone,
    TR_KEY_status,
    TR_KEY_uploadRatio
};

static size_t
writeFunc (void * ptr, size_t size, size_t nmemb, void * buf)
{
    const size_t byteCount = size * nmemb;
    evbuffer_add (buf, ptr, byteCount);
    return byteCount;
}

/* look for a session id in the header in case the server gives back a 409 */
static size_t
parseResponseHeader (void *ptr, size_t size, size_t nmemb, void * stream UNUSED)
{
    const char * line = ptr;
    const size_t line_len = size * nmemb;
    const char * key = TR_RPC_SESSION_ID_HEADER ": ";
    const size_t key_len = strlen (key);

    if ((line_len >= key_len) && !memcmp (line, key, key_len))
    {
        const char * begin = line + key_len;
        const char * end = begin;
        while (!isspace (*end))
            ++end;
        tr_free (sessionId);
        sessionId = tr_strndup (begin, end-begin);
    }

    return line_len;
}

static long
getTimeoutSecs (const char * req)
{
  if (strstr (req, "\"method\":\"blocklist-update\"") != NULL)
    return 300L;

  return 60L; /* default value */
}

static char*
getStatusString (tr_variant * t, char * buf, size_t buflen)
{
    int64_t status;
    bool boolVal;

    if (!tr_variantDictFindInt (t, TR_KEY_status, &status))
    {
        *buf = '\0';
    }
    else switch (status)
    {
        case TR_STATUS_DOWNLOAD_WAIT:
        case TR_STATUS_SEED_WAIT:
            tr_strlcpy (buf, "Queued", buflen);
            break;

        case TR_STATUS_STOPPED:
            if (tr_variantDictFindBool (t, TR_KEY_isFinished, &boolVal) && boolVal)
                tr_strlcpy (buf, "Finished", buflen);
            else
                tr_strlcpy (buf, "Stopped", buflen);
            break;

        case TR_STATUS_CHECK_WAIT:
        case TR_STATUS_CHECK: {
            const char * str = status == TR_STATUS_CHECK_WAIT
                             ? "Will Verify"
                             : "Verifying";
            double percent;
            if (tr_variantDictFindReal (t, TR_KEY_recheckProgress, &percent))
                tr_snprintf (buf, buflen, "%s (%.0f%%)", str, floor (percent*100.0));
            else
                tr_strlcpy (buf, str, buflen);

            break;
        }

        case TR_STATUS_DOWNLOAD:
        case TR_STATUS_SEED: {
            int64_t fromUs = 0;
            int64_t toUs = 0;
            tr_variantDictFindInt (t, TR_KEY_peersGettingFromUs, &fromUs);
            tr_variantDictFindInt (t, TR_KEY_peersSendingToUs, &toUs);
            if (fromUs && toUs)
                tr_strlcpy (buf, "Up & Down", buflen);
            else if (toUs)
                tr_strlcpy (buf, "Downloading", buflen);
            else if (fromUs) {
                int64_t leftUntilDone = 0;
                tr_variantDictFindInt (t, TR_KEY_leftUntilDone, &leftUntilDone);
                if (leftUntilDone > 0)
                    tr_strlcpy (buf, "Uploading", buflen);
                else
                    tr_strlcpy (buf, "Seeding", buflen);
            } else {
                tr_strlcpy (buf, "Idle", buflen);
            }
            break;
        }

        default:
            tr_strlcpy (buf, "Unknown", buflen);
            break;
    }

    return buf;
}

static const char *bandwidthPriorityNames[] =
    { "Low", "Normal", "High", "Invalid" };

static void
printDetails (tr_variant * top)
{
    tr_variant *args, *torrents;

    if ((tr_variantDictFindDict (top, TR_KEY_arguments, &args))
      && (tr_variantDictFindList (args, TR_KEY_torrents, &torrents)))
    {
        int ti, tCount;
        for (ti = 0, tCount = tr_variantListSize (torrents); ti < tCount;
             ++ti)
        {
            tr_variant *    t = tr_variantListChild (torrents, ti);
            tr_variant *    l;
            const char * str;
            char         buf[512];
            char         buf2[512];
            int64_t      i, j, k;
            bool      boolVal;
            double       d;

            printf ("NAME\n");
            if (tr_variantDictFindInt (t, TR_KEY_id, &i))
                printf ("  Id: %" PRId64 "\n", i);
            if (tr_variantDictFindStr (t, TR_KEY_name, &str, NULL))
                printf ("  Name: %s\n", str);
            if (tr_variantDictFindStr (t, TR_KEY_hashString, &str, NULL))
                printf ("  Hash: %s\n", str);
            if (tr_variantDictFindStr (t, TR_KEY_magnetLink, &str, NULL))
                printf ("  Magnet: %s\n", str);
            printf ("\n");

            printf ("TRANSFER\n");
            getStatusString (t, buf, sizeof (buf));
            printf ("  State: %s\n", buf);

            if (tr_variantDictFindStr (t, TR_KEY_downloadDir, &str, NULL))
                printf ("  Location: %s\n", str);

            if (tr_variantDictFindInt (t, TR_KEY_sizeWhenDone, &i)
              && tr_variantDictFindInt (t, TR_KEY_leftUntilDone, &j))
            {
                strlpercent (buf, 100.0 * (i - j) / i, sizeof (buf));
                printf ("  Percent Done: %s%%\n", buf);
            }

            if (tr_variantDictFindInt (t, TR_KEY_eta, &i))
                printf ("  ETA: %s\n", tr_strltime (buf, i, sizeof (buf)));
            if (tr_variantDictFindInt (t, TR_KEY_rateDownload, &i))
                printf ("  Download Speed: %s\n", tr_formatter_speed_KBps (buf, i/ (double)tr_speed_K, sizeof (buf)));
            if (tr_variantDictFindInt (t, TR_KEY_rateUpload, &i))
                printf ("  Upload Speed: %s\n", tr_formatter_speed_KBps (buf, i/ (double)tr_speed_K, sizeof (buf)));
            if (tr_variantDictFindInt (t, TR_KEY_haveUnchecked, &i)
              && tr_variantDictFindInt (t, TR_KEY_haveValid, &j))
            {
                strlsize (buf, i + j, sizeof (buf));
                strlsize (buf2, j, sizeof (buf2));
                printf ("  Have: %s (%s verified)\n", buf, buf2);
            }

            if (tr_variantDictFindInt (t, TR_KEY_sizeWhenDone, &i))
            {
                if (i < 1)
                    printf ("  Availability: None\n");
                if (tr_variantDictFindInt (t, TR_KEY_desiredAvailable, &j)
                    && tr_variantDictFindInt (t, TR_KEY_leftUntilDone, &k))
                {
                    j += i - k;
                    strlpercent (buf, 100.0 * j / i, sizeof (buf));
                    printf ("  Availability: %s%%\n", buf);
                }
                if (tr_variantDictFindInt (t, TR_KEY_totalSize, &j))
                {
                    strlsize (buf2, i, sizeof (buf2));
                    strlsize (buf, j, sizeof (buf));
                    printf ("  Total size: %s (%s wanted)\n", buf, buf2);
                }
            }
            if (tr_variantDictFindInt (t, TR_KEY_downloadedEver, &i)
              && tr_variantDictFindInt (t, TR_KEY_uploadedEver, &j))
            {
                strlsize (buf, i, sizeof (buf));
                printf ("  Downloaded: %s\n", buf);
                strlsize (buf, j, sizeof (buf));
                printf ("  Uploaded: %s\n", buf);
                strlratio (buf, j, i, sizeof (buf));
                printf ("  Ratio: %s\n", buf);
            }
            if (tr_variantDictFindInt (t, TR_KEY_corruptEver, &i))
            {
                strlsize (buf, i, sizeof (buf));
                printf ("  Corrupt DL: %s\n", buf);
            }
            if (tr_variantDictFindStr (t, TR_KEY_errorString, &str, NULL) && str && *str &&
                tr_variantDictFindInt (t, TR_KEY_error, &i) && i)
            {
                switch (i) {
                    case TR_STAT_TRACKER_WARNING: printf ("  Tracker gave a warning: %s\n", str); break;
                    case TR_STAT_TRACKER_ERROR:   printf ("  Tracker gave an error: %s\n", str); break;
                    case TR_STAT_LOCAL_ERROR:     printf ("  Error: %s\n", str); break;
                    default: break; /* no error */
                }
            }
            if (tr_variantDictFindInt (t, TR_KEY_peersConnected, &i)
              && tr_variantDictFindInt (t, TR_KEY_peersGettingFromUs, &j)
              && tr_variantDictFindInt (t, TR_KEY_peersSendingToUs, &k))
            {
                printf (
                    "  Peers: "
                    "connected to %" PRId64 ", "
                                            "uploading to %" PRId64
                    ", "
                    "downloading from %"
                    PRId64 "\n",
                    i, j, k);
            }

            if (tr_variantDictFindList (t, TR_KEY_webseeds, &l)
              && tr_variantDictFindInt (t, TR_KEY_webseedsSendingToUs, &i))
            {
                const int64_t n = tr_variantListSize (l);
                if (n > 0)
                    printf (
                        "  Web Seeds: downloading from %" PRId64 " of %"
                        PRId64
                        " web seeds\n", i, n);
            }
            printf ("\n");

            printf ("HISTORY\n");
            if (tr_variantDictFindInt (t, TR_KEY_addedDate, &i) && i)
            {
                const time_t tt = i;
                printf ("  Date added:       %s", ctime (&tt));
            }
            if (tr_variantDictFindInt (t, TR_KEY_doneDate, &i) && i)
            {
                const time_t tt = i;
                printf ("  Date finished:    %s", ctime (&tt));
            }
            if (tr_variantDictFindInt (t, TR_KEY_startDate, &i) && i)
            {
                const time_t tt = i;
                printf ("  Date started:     %s", ctime (&tt));
            }
            if (tr_variantDictFindInt (t, TR_KEY_activityDate, &i) && i)
            {
                const time_t tt = i;
                printf ("  Latest activity:  %s", ctime (&tt));
            }
            if (tr_variantDictFindInt (t, TR_KEY_secondsDownloading, &i) && (i > 0))
                printf ("  Downloading Time: %s\n", tr_strltime (buf, i, sizeof (buf)));
            if (tr_variantDictFindInt (t, TR_KEY_secondsSeeding, &i) && (i > 0))
                printf ("  Seeding Time:     %s\n", tr_strltime (buf, i, sizeof (buf)));
            printf ("\n");

            printf ("ORIGINS\n");
            if (tr_variantDictFindInt (t, TR_KEY_dateCreated, &i) && i)
            {
                const time_t tt = i;
                printf ("  Date created: %s", ctime (&tt));
            }
            if (tr_variantDictFindBool (t, TR_KEY_isPrivate, &boolVal))
                printf ("  Public torrent: %s\n", (boolVal ? "No" : "Yes"));
            if (tr_variantDictFindStr (t, TR_KEY_comment, &str, NULL) && str && *str)
                printf ("  Comment: %s\n", str);
            if (tr_variantDictFindStr (t, TR_KEY_creator, &str, NULL) && str && *str)
                printf ("  Creator: %s\n", str);
            if (tr_variantDictFindInt (t, TR_KEY_pieceCount, &i))
                printf ("  Piece Count: %" PRId64 "\n", i);
            if (tr_variantDictFindInt (t, TR_KEY_pieceSize, &i))
                printf ("  Piece Size: %s\n", strlmem (buf, i, sizeof (buf)));
            printf ("\n");

            printf ("LIMITS & BANDWIDTH\n");
            if (tr_variantDictFindBool (t, TR_KEY_downloadLimited, &boolVal)
                && tr_variantDictFindInt (t, TR_KEY_downloadLimit, &i))
            {
                printf ("  Download Limit: ");
                if (boolVal)
                    printf ("%s\n", tr_formatter_speed_KBps (buf, i, sizeof (buf)));
                else
                    printf ("Unlimited\n");
            }
            if (tr_variantDictFindBool (t, TR_KEY_uploadLimited, &boolVal)
                && tr_variantDictFindInt (t, TR_KEY_uploadLimit, &i))
            {
                printf ("  Upload Limit: ");
                if (boolVal)
                    printf ("%s\n", tr_formatter_speed_KBps (buf, i, sizeof (buf)));
                else
                    printf ("Unlimited\n");
            }
            if (tr_variantDictFindInt (t, TR_KEY_seedRatioMode, &i))
            {
                switch (i) {
                    case TR_RATIOLIMIT_GLOBAL:
                        printf ("  Ratio Limit: Default\n");
                        break;
                    case TR_RATIOLIMIT_SINGLE:
                        if (tr_variantDictFindReal (t, TR_KEY_seedRatioLimit, &d))
                            printf ("  Ratio Limit: %s\n", strlratio2 (buf, d, sizeof(buf)));
                        break;
                    case TR_RATIOLIMIT_UNLIMITED:
                        printf ("  Ratio Limit: Unlimited\n");
                        break;
                    default: break;
                }
            }
            if (tr_variantDictFindBool (t, TR_KEY_honorsSessionLimits, &boolVal))
                printf ("  Honors Session Limits: %s\n", (boolVal ? "Yes" : "No"));
            if (tr_variantDictFindInt (t, TR_KEY_peer_limit, &i))
                printf ("  Peer limit: %" PRId64 "\n", i);
            if (tr_variantDictFindInt (t, TR_KEY_bandwidthPriority, &i))
                printf ("  Bandwidth Priority: %s\n",
                        bandwidthPriorityNames[ (i + 1) & 3]);

            printf ("\n");
        }
    }
}

static void
printFileList (tr_variant * top)
{
    tr_variant *args, *torrents;

    if ((tr_variantDictFindDict (top, TR_KEY_arguments, &args))
      && (tr_variantDictFindList (args, TR_KEY_torrents, &torrents)))
    {
        int i, in;
        for (i = 0, in = tr_variantListSize (torrents); i < in; ++i)
        {
            tr_variant *    d = tr_variantListChild (torrents, i);
            tr_variant *    files, *priorities, *wanteds;
            const char * name;
            if (tr_variantDictFindStr (d, TR_KEY_name, &name, NULL)
              && tr_variantDictFindList (d, TR_KEY_files, &files)
              && tr_variantDictFindList (d, TR_KEY_priorities, &priorities)
              && tr_variantDictFindList (d, TR_KEY_wanted, &wanteds))
            {
                int j = 0, jn = tr_variantListSize (files);
                printf ("%s (%d files):\n", name, jn);
                printf ("%3s  %4s %8s %3s %9s  %s\n", "#", "Done",
                        "Priority", "Get", "Size",
                        "Name");
                for (j = 0, jn = tr_variantListSize (files); j < jn; ++j)
                {
                    int64_t      have;
                    int64_t      length;
                    int64_t      priority;
                    int64_t      wanted;
                    const char * filename;
                    tr_variant *    file = tr_variantListChild (files, j);
                    if (tr_variantDictFindInt (file, TR_KEY_length, &length)
                      && tr_variantDictFindStr (file, TR_KEY_name, &filename, NULL)
                      && tr_variantDictFindInt (file, TR_KEY_bytesCompleted, &have)
                      && tr_variantGetInt (tr_variantListChild (priorities, j), &priority)
                      && tr_variantGetInt (tr_variantListChild (wanteds, j), &wanted))
                    {
                        char         sizestr[64];
                        double       percent = (double)have / length;
                        const char * pristr;
                        strlsize (sizestr, length, sizeof (sizestr));
                        switch (priority)
                        {
                            case TR_PRI_LOW:
                                pristr = "Low"; break;

                            case TR_PRI_HIGH:
                                pristr = "High"; break;

                            default:
                                pristr = "Normal"; break;
                        }
                        printf ("%3d: %3.0f%% %-8s %-3s %9s  %s\n",
                                j,
                                floor (100.0 * percent),
                                pristr,
                              (wanted ? "Yes" : "No"),
                                sizestr,
                                filename);
                    }
                }
            }
        }
    }
}

static void
printPeersImpl (tr_variant * peers)
{
  int i, n;
  printf ("%-20s  %-12s  %-5s %-6s  %-6s  %s\n",
          "Address", "Flags", "Done", "Down", "Up", "Client");

  for (i=0, n=tr_variantListSize(peers); i<n; ++i)
    {
      double progress;
      const char * address, * client, * flagstr;
      int64_t rateToClient, rateToPeer;
      tr_variant * d = tr_variantListChild (peers, i);

      if  (tr_variantDictFindStr  (d, TR_KEY_address, &address, NULL)
        && tr_variantDictFindStr  (d, TR_KEY_clientName, &client, NULL)
        && tr_variantDictFindReal (d, TR_KEY_progress, &progress)
        && tr_variantDictFindStr  (d, TR_KEY_flagStr, &flagstr, NULL)
        && tr_variantDictFindInt  (d, TR_KEY_rateToClient, &rateToClient)
        && tr_variantDictFindInt  (d, TR_KEY_rateToPeer, &rateToPeer))
        {
          printf ("%-20s  %-12s  %-5.1f %6.1f  %6.1f  %s\n",
                  address, flagstr, (progress*100.0),
                  rateToClient / (double)tr_speed_K,
                  rateToPeer / (double)tr_speed_K,
                  client);
        }
    }
}

static void
printPeers (tr_variant * top)
{
  tr_variant *args, *torrents;

  if (tr_variantDictFindDict (top, TR_KEY_arguments, &args)
      && tr_variantDictFindList (args, TR_KEY_torrents, &torrents))
    {
      int i, n;
      for (i=0, n=tr_variantListSize (torrents); i<n; ++i)
        {
          tr_variant * peers;
          tr_variant * torrent = tr_variantListChild (torrents, i);
          if (tr_variantDictFindList (torrent, TR_KEY_peers, &peers))
            {
              printPeersImpl (peers);
              if (i+1<n)
                printf ("\n");
            }
        }
    }
}

static void
printPiecesImpl (const uint8_t * raw, size_t rawlen, size_t j)
{
    size_t i, k, len;
    char * str = tr_base64_decode (raw, rawlen, &len);
    printf ("  ");
    for (i=k=0; k<len; ++k) {
        int e;
        for (e=0; i<j && e<8; ++e, ++i)
            printf ("%c", str[k] & (1<< (7-e)) ? '1' : '0');
        printf (" ");
        if (! (i%64))
            printf ("\n  ");
    }
    printf ("\n");
    tr_free (str);
}

static void
printPieces (tr_variant * top)
{
    tr_variant *args, *torrents;

    if (tr_variantDictFindDict (top, TR_KEY_arguments, &args)
      && tr_variantDictFindList (args, TR_KEY_torrents, &torrents))
    {
        int i, n;
        for (i=0, n=tr_variantListSize (torrents); i<n; ++i)
        {
            int64_t j;
            const uint8_t * raw;
            size_t       rawlen;
            tr_variant * torrent = tr_variantListChild (torrents, i);
            if (tr_variantDictFindRaw (torrent, TR_KEY_pieces, &raw, &rawlen) &&
                tr_variantDictFindInt (torrent, TR_KEY_pieceCount, &j)) {
                assert (j >= 0);
                printPiecesImpl (raw, rawlen, (size_t) j);
                if (i+1<n)
                    printf ("\n");
            }
        }
    }
}

static void
printPortTest (tr_variant * top)
{
    tr_variant *args;
    if ((tr_variantDictFindDict (top, TR_KEY_arguments, &args)))
    {
        bool      boolVal;

        if (tr_variantDictFindBool (args, TR_KEY_port_is_open, &boolVal))
            printf ("Port is open: %s\n", (boolVal ? "Yes" : "No"));
    }
}

static void
printTorrentList (tr_variant * top)
{
    tr_variant *args, *list;

    if ((tr_variantDictFindDict (top, TR_KEY_arguments, &args))
      && (tr_variantDictFindList (args, TR_KEY_torrents, &list)))
    {
        int i, n;
        int64_t total_size=0;
        double total_up=0, total_down=0;
        char haveStr[32];

        printf ("%-4s   %-4s  %9s  %-8s  %6s  %6s  %-5s  %-11s  %s\n",
                "ID", "Done", "Have", "ETA", "Up", "Down", "Ratio", "Status",
                "Name");

        for (i = 0, n = tr_variantListSize (list); i < n; ++i)
        {
            int64_t      id, eta, status, up, down;
            int64_t      sizeWhenDone, leftUntilDone;
            double       ratio;
            const char * name;
            tr_variant *   d = tr_variantListChild (list, i);
            if  (tr_variantDictFindInt  (d, TR_KEY_eta, &eta)
              && tr_variantDictFindInt  (d, TR_KEY_id, &id)
              && tr_variantDictFindInt  (d, TR_KEY_leftUntilDone, &leftUntilDone)
              && tr_variantDictFindStr  (d, TR_KEY_name, &name, NULL)
              && tr_variantDictFindInt  (d, TR_KEY_rateDownload, &down)
              && tr_variantDictFindInt  (d, TR_KEY_rateUpload, &up)
              && tr_variantDictFindInt  (d, TR_KEY_sizeWhenDone, &sizeWhenDone)
              && tr_variantDictFindInt  (d, TR_KEY_status, &status)
              && tr_variantDictFindReal (d, TR_KEY_uploadRatio, &ratio))
            {
                char etaStr[16];
                char statusStr[64];
                char ratioStr[32];
                char doneStr[8];
                int64_t error;
                char errorMark;

                if (sizeWhenDone)
                    tr_snprintf (doneStr, sizeof (doneStr), "%d%%", (int)(100.0 * (sizeWhenDone - leftUntilDone) / sizeWhenDone));
                else
                    tr_strlcpy (doneStr, "n/a", sizeof (doneStr));

                strlsize (haveStr, sizeWhenDone - leftUntilDone, sizeof (haveStr));

                if (leftUntilDone || eta != -1)
                    etaToString (etaStr, sizeof (etaStr), eta);
                else
                    tr_snprintf (etaStr, sizeof (etaStr), "Done");
                if (tr_variantDictFindInt (d, TR_KEY_error, &error) && error)
                    errorMark = '*';
                else
                    errorMark = ' ';
                printf (
                    "%4d%c  %4s  %9s  %-8s  %6.1f  %6.1f  %5s  %-11s  %s\n",
                  (int)id, errorMark,
                    doneStr,
                    haveStr,
                    etaStr,
                    up/ (double)tr_speed_K,
                    down/ (double)tr_speed_K,
                    strlratio2 (ratioStr, ratio, sizeof (ratioStr)),
                    getStatusString (d, statusStr, sizeof (statusStr)),
                    name);

                total_up += up;
                total_down += down;
                total_size += sizeWhenDone - leftUntilDone;
            }
        }

        printf ("Sum:         %9s            %6.1f  %6.1f\n",
                strlsize (haveStr, total_size, sizeof (haveStr)),
                total_up/ (double)tr_speed_K,
                total_down/ (double)tr_speed_K);
    }
}

static void
printTrackersImpl (tr_variant * trackerStats)
{
    int i;
    char         buf[512];
    tr_variant * t;

    for (i=0; ((t = tr_variantListChild (trackerStats, i))); ++i)
    {
        int64_t downloadCount;
        bool hasAnnounced;
        bool hasScraped;
        const char * host;
        int64_t id;
        bool isBackup;
        int64_t lastAnnouncePeerCount;
        const char * lastAnnounceResult;
        int64_t lastAnnounceStartTime;
        bool lastAnnounceSucceeded;
        int64_t lastAnnounceTime;
        bool lastAnnounceTimedOut;
        const char * lastScrapeResult;
        bool lastScrapeSucceeded;
        int64_t lastScrapeStartTime;
        int64_t lastScrapeTime;
        bool lastScrapeTimedOut;
        int64_t leecherCount;
        int64_t nextAnnounceTime;
        int64_t nextScrapeTime;
        int64_t seederCount;
        int64_t tier;
        int64_t announceState;
        int64_t scrapeState;

        if (tr_variantDictFindInt  (t, TR_KEY_downloadCount, &downloadCount) &&
            tr_variantDictFindBool (t, TR_KEY_hasAnnounced, &hasAnnounced) &&
            tr_variantDictFindBool (t, TR_KEY_hasScraped, &hasScraped) &&
            tr_variantDictFindStr  (t, TR_KEY_host, &host, NULL) &&
            tr_variantDictFindInt  (t, TR_KEY_id, &id) &&
            tr_variantDictFindBool (t, TR_KEY_isBackup, &isBackup) &&
            tr_variantDictFindInt  (t, TR_KEY_announceState, &announceState) &&
            tr_variantDictFindInt  (t, TR_KEY_scrapeState, &scrapeState) &&
            tr_variantDictFindInt  (t, TR_KEY_lastAnnouncePeerCount, &lastAnnouncePeerCount) &&
            tr_variantDictFindStr  (t, TR_KEY_lastAnnounceResult, &lastAnnounceResult, NULL) &&
            tr_variantDictFindInt  (t, TR_KEY_lastAnnounceStartTime, &lastAnnounceStartTime) &&
            tr_variantDictFindBool (t, TR_KEY_lastAnnounceSucceeded, &lastAnnounceSucceeded) &&
            tr_variantDictFindInt  (t, TR_KEY_lastAnnounceTime, &lastAnnounceTime) &&
            tr_variantDictFindBool (t, TR_KEY_lastAnnounceTimedOut, &lastAnnounceTimedOut) &&
            tr_variantDictFindStr  (t, TR_KEY_lastScrapeResult, &lastScrapeResult, NULL) &&
            tr_variantDictFindInt  (t, TR_KEY_lastScrapeStartTime, &lastScrapeStartTime) &&
            tr_variantDictFindBool (t, TR_KEY_lastScrapeSucceeded, &lastScrapeSucceeded) &&
            tr_variantDictFindInt  (t, TR_KEY_lastScrapeTime, &lastScrapeTime) &&
            tr_variantDictFindBool (t, TR_KEY_lastScrapeTimedOut, &lastScrapeTimedOut) &&
            tr_variantDictFindInt  (t, TR_KEY_leecherCount, &leecherCount) &&
            tr_variantDictFindInt  (t, TR_KEY_nextAnnounceTime, &nextAnnounceTime) &&
            tr_variantDictFindInt  (t, TR_KEY_nextScrapeTime, &nextScrapeTime) &&
            tr_variantDictFindInt  (t, TR_KEY_seederCount, &seederCount) &&
            tr_variantDictFindInt  (t, TR_KEY_tier, &tier))
        {
            const time_t now = time (NULL);

            printf ("\n");
            printf ("  Tracker %d: %s\n", (int)(id), host);
            if (isBackup)
                printf ("  Backup on tier %d\n", (int)tier);
            else
                printf ("  Active in tier %d\n", (int)tier);

            if (!isBackup)
            {
                if (hasAnnounced && announceState != TR_TRACKER_INACTIVE)
                {
                    tr_strltime (buf, now - lastAnnounceTime, sizeof (buf));
                    if (lastAnnounceSucceeded)
                        printf ("  Got a list of %d peers %s ago\n",
                              (int)lastAnnouncePeerCount, buf);
                    else if (lastAnnounceTimedOut)
                        printf ("  Peer list request timed out; will retry\n");
                    else
                        printf ("  Got an error \"%s\" %s ago\n",
                                lastAnnounceResult, buf);
                }

                switch (announceState)
                {
                    case TR_TRACKER_INACTIVE:
                        printf ("  No updates scheduled\n");
                        break;
                    case TR_TRACKER_WAITING:
                        tr_strltime (buf, nextAnnounceTime - now, sizeof (buf));
                        printf ("  Asking for more peers in %s\n", buf);
                        break;
                    case TR_TRACKER_QUEUED:
                        printf ("  Queued to ask for more peers\n");
                        break;
                    case TR_TRACKER_ACTIVE:
                        tr_strltime (buf, now - lastAnnounceStartTime, sizeof (buf));
                        printf ("  Asking for more peers now... %s\n", buf);
                        break;
                }

                if (hasScraped)
                {
                    tr_strltime (buf, now - lastScrapeTime, sizeof (buf));
                    if (lastScrapeSucceeded)
                        printf ("  Tracker had %d seeders and %d leechers %s ago\n",
                              (int)seederCount, (int)leecherCount, buf);
                    else if (lastScrapeTimedOut)
                        printf ("  Tracker scrape timed out; will retry\n");
                    else
                        printf ("  Got a scrape error \"%s\" %s ago\n",
                                lastScrapeResult, buf);
                }

                switch (scrapeState)
                {
                    case TR_TRACKER_INACTIVE:
                        break;
                    case TR_TRACKER_WAITING:
                        tr_strltime (buf, nextScrapeTime - now, sizeof (buf));
                        printf ("  Asking for peer counts in %s\n", buf);
                        break;
                    case TR_TRACKER_QUEUED:
                        printf ("  Queued to ask for peer counts\n");
                        break;
                    case TR_TRACKER_ACTIVE:
                        tr_strltime (buf, now - lastScrapeStartTime, sizeof (buf));
                        printf ("  Asking for peer counts now... %s\n", buf);
                        break;
                }
            }
        }
    }
}

static void
printTrackers (tr_variant * top)
{
  tr_variant *args, *torrents;

  if (tr_variantDictFindDict (top, TR_KEY_arguments, &args)
      && tr_variantDictFindList (args, TR_KEY_torrents, &torrents))
    {
      int i, n;
      for (i=0, n=tr_variantListSize (torrents); i<n; ++i)
        {
          tr_variant * trackerStats;
          tr_variant * torrent = tr_variantListChild (torrents, i);

          if (tr_variantDictFindList (torrent, TR_KEY_trackerStats, &trackerStats))
            {
              printTrackersImpl (trackerStats);

              if (i+1<n)
                printf ("\n");
            }
        }
    }
}

static void
printSession (tr_variant * top)
{
  tr_variant *args;
  if ((tr_variantDictFindDict (top, TR_KEY_arguments, &args)))
    {
      int64_t i;
      char buf[64];
      bool boolVal;
      const char * str;

      printf ("VERSION\n");
      if (tr_variantDictFindStr (args, TR_KEY_version, &str, NULL))
        printf ("  Daemon version: %s\n", str);
      if (tr_variantDictFindInt (args, TR_KEY_rpc_version, &i))
        printf ("  RPC version: %" PRId64 "\n", i);
      if (tr_variantDictFindInt (args, TR_KEY_rpc_version_minimum, &i))
        printf ("  RPC minimum version: %" PRId64 "\n", i);
      printf ("\n");

      printf ("CONFIG\n");
      if (tr_variantDictFindStr (args, TR_KEY_config_dir, &str, NULL))
        printf ("  Configuration directory: %s\n", str);
      if (tr_variantDictFindStr (args,  TR_KEY_download_dir, &str, NULL))
        printf ("  Download directory: %s\n", str);
      if (tr_variantDictFindInt (args, TR_KEY_peer_port, &i))
        printf ("  Listenport: %" PRId64 "\n", i);
      if (tr_variantDictFindBool (args, TR_KEY_port_forwarding_enabled, &boolVal))
        printf ("  Portforwarding enabled: %s\n", (boolVal ? "Yes" : "No"));
      if (tr_variantDictFindBool (args, TR_KEY_utp_enabled, &boolVal))
        printf ("  uTP enabled: %s\n", (boolVal ? "Yes" : "No"));
      if (tr_variantDictFindBool (args, TR_KEY_dht_enabled, &boolVal))
        printf ("  Distributed hash table enabled: %s\n", (boolVal ? "Yes" : "No"));
      if (tr_variantDictFindBool (args, TR_KEY_lpd_enabled, &boolVal))
        printf ("  Local peer discovery enabled: %s\n", (boolVal ? "Yes" : "No"));
      if (tr_variantDictFindBool (args, TR_KEY_pex_enabled, &boolVal))
        printf ("  Peer exchange allowed: %s\n", (boolVal ? "Yes" : "No"));
      if (tr_variantDictFindStr (args,  TR_KEY_encryption, &str, NULL))
        printf ("  Encryption: %s\n", str);
      if (tr_variantDictFindInt (args, TR_KEY_cache_size_mb, &i))
        printf ("  Maximum memory cache size: %s\n", tr_formatter_mem_MB (buf, i, sizeof (buf)));
      printf ("\n");

        {
            bool altEnabled, altTimeEnabled, upEnabled, downEnabled, seedRatioLimited;
            int64_t altDown, altUp, altBegin, altEnd, altDay, upLimit, downLimit, peerLimit;
            double seedRatioLimit;

            if (tr_variantDictFindInt  (args, TR_KEY_alt_speed_down, &altDown) &&
                tr_variantDictFindBool (args, TR_KEY_alt_speed_enabled, &altEnabled) &&
                tr_variantDictFindInt  (args, TR_KEY_alt_speed_time_begin, &altBegin) &&
                tr_variantDictFindBool (args, TR_KEY_alt_speed_time_enabled, &altTimeEnabled) &&
                tr_variantDictFindInt  (args, TR_KEY_alt_speed_time_end, &altEnd) &&
                tr_variantDictFindInt  (args, TR_KEY_alt_speed_time_day, &altDay) &&
                tr_variantDictFindInt  (args, TR_KEY_alt_speed_up, &altUp) &&
                tr_variantDictFindInt  (args, TR_KEY_peer_limit_global, &peerLimit) &&
                tr_variantDictFindInt  (args, TR_KEY_speed_limit_down, &downLimit) &&
                tr_variantDictFindBool (args, TR_KEY_speed_limit_down_enabled, &downEnabled) &&
                tr_variantDictFindInt  (args, TR_KEY_speed_limit_up, &upLimit) &&
                tr_variantDictFindBool (args, TR_KEY_speed_limit_up_enabled, &upEnabled) &&
                tr_variantDictFindReal (args, TR_KEY_seedRatioLimit, &seedRatioLimit) &&
                tr_variantDictFindBool (args, TR_KEY_seedRatioLimited, &seedRatioLimited))
            {
                char buf[128];
                char buf2[128];
                char buf3[128];

                printf ("LIMITS\n");
                printf ("  Peer limit: %" PRId64 "\n", peerLimit);

                if (seedRatioLimited)
                    strlratio2 (buf, seedRatioLimit, sizeof(buf));
                else
                    tr_strlcpy (buf, "Unlimited", sizeof (buf));
                printf ("  Default seed ratio limit: %s\n", buf);

                if (altEnabled)
                    tr_formatter_speed_KBps (buf, altUp, sizeof (buf));
                else if (upEnabled)
                    tr_formatter_speed_KBps (buf, upLimit, sizeof (buf));
                else
                    tr_strlcpy (buf, "Unlimited", sizeof (buf));
                printf ("  Upload speed limit: %s (%s limit: %s; %s turtle limit: %s)\n",
                        buf,
                        upEnabled ? "Enabled" : "Disabled",
                        tr_formatter_speed_KBps (buf2, upLimit, sizeof (buf2)),
                        altEnabled ? "Enabled" : "Disabled",
                        tr_formatter_speed_KBps (buf3, altUp, sizeof (buf3)));

                if (altEnabled)
                    tr_formatter_speed_KBps (buf, altDown, sizeof (buf));
                else if (downEnabled)
                    tr_formatter_speed_KBps (buf, downLimit, sizeof (buf));
                else
                    tr_strlcpy (buf, "Unlimited", sizeof (buf));
                printf ("  Download speed limit: %s (%s limit: %s; %s turtle limit: %s)\n",
                        buf,
                        downEnabled ? "Enabled" : "Disabled",
                        tr_formatter_speed_KBps (buf2, downLimit, sizeof (buf2)),
                        altEnabled ? "Enabled" : "Disabled",
                        tr_formatter_speed_KBps (buf3, altDown, sizeof (buf3)));

                if (altTimeEnabled) {
                    printf ("  Turtle schedule: %02d:%02d - %02d:%02d  ",
                          (int)(altBegin/60), (int)(altBegin%60),
                          (int)(altEnd/60), (int)(altEnd%60));
                    if (altDay & TR_SCHED_SUN)   printf ("Sun ");
                    if (altDay & TR_SCHED_MON)   printf ("Mon ");
                    if (altDay & TR_SCHED_TUES)  printf ("Tue ");
                    if (altDay & TR_SCHED_WED)   printf ("Wed ");
                    if (altDay & TR_SCHED_THURS) printf ("Thu ");
                    if (altDay & TR_SCHED_FRI)   printf ("Fri ");
                    if (altDay & TR_SCHED_SAT)   printf ("Sat ");
                    printf ("\n");
                }
            }
        }
        printf ("\n");

        printf ("MISC\n");
        if (tr_variantDictFindBool (args, TR_KEY_start_added_torrents, &boolVal))
            printf ("  Autostart added torrents: %s\n", (boolVal ? "Yes" : "No"));
        if (tr_variantDictFindBool (args, TR_KEY_trash_original_torrent_files, &boolVal))
            printf ("  Delete automatically added torrents: %s\n", (boolVal ? "Yes" : "No"));
    }
}

static void
printSessionStats (tr_variant * top)
{
  tr_variant *args, *d;
  if ((tr_variantDictFindDict (top, TR_KEY_arguments, &args)))
    {
      char buf[512];
      int64_t up, down, secs, sessions;

      if (tr_variantDictFindDict (args, TR_KEY_current_stats, &d)
          && tr_variantDictFindInt (d, TR_KEY_uploadedBytes, &up)
          && tr_variantDictFindInt (d, TR_KEY_downloadedBytes, &down)
          && tr_variantDictFindInt (d, TR_KEY_secondsActive, &secs))
        {
          printf ("\nCURRENT SESSION\n");
          printf ("  Uploaded:   %s\n", strlsize (buf, up, sizeof (buf)));
          printf ("  Downloaded: %s\n", strlsize (buf, down, sizeof (buf)));
          printf ("  Ratio:      %s\n", strlratio (buf, up, down, sizeof (buf)));
          printf ("  Duration:   %s\n", tr_strltime (buf, secs, sizeof (buf)));
        }

      if (tr_variantDictFindDict (args, TR_KEY_cumulative_stats, &d)
            && tr_variantDictFindInt (d, TR_KEY_sessionCount, &sessions)
            && tr_variantDictFindInt (d, TR_KEY_uploadedBytes, &up)
            && tr_variantDictFindInt (d, TR_KEY_downloadedBytes, &down)
            && tr_variantDictFindInt (d, TR_KEY_secondsActive, &secs))
        {
          printf ("\nTOTAL\n");
          printf ("  Started %lu times\n", (unsigned long)sessions);
          printf ("  Uploaded:   %s\n", strlsize (buf, up, sizeof (buf)));
          printf ("  Downloaded: %s\n", strlsize (buf, down, sizeof (buf)));
          printf ("  Ratio:      %s\n", strlratio (buf, up, down, sizeof (buf)));
          printf ("  Duration:   %s\n", tr_strltime (buf, secs, sizeof (buf)));
        }
    }
}

static char id[4096];

static int
processResponse (const char * rpcurl, const void * response, size_t len)
{
    tr_variant top;
    int status = EXIT_SUCCESS;

    if (debug)
        fprintf (stderr, "got response (len %d):\n--------\n%*.*s\n--------\n",
               (int)len, (int)len, (int)len, (const char*) response);

    if (tr_variantFromJson (&top, response, len))
    {
        tr_logAddNamedError (MY_NAME, "Unable to parse response \"%*.*s\"", (int)len,
               (int)len, (const char*)response);
        status |= EXIT_FAILURE;
    }
    else
    {
        int64_t      tag = -1;
        const char * str;

        if (tr_variantDictFindStr (&top, TR_KEY_result, &str, NULL))
        {
            if (strcmp (str, "success"))
            {
                printf ("Error: %s\n", str);
                status |= EXIT_FAILURE;
            }
            else
            {
        tr_variantDictFindInt (&top, TR_KEY_tag, &tag);

        switch (tag)
        {
            case TAG_SESSION:
                printSession (&top); break;

            case TAG_STATS:
                printSessionStats (&top); break;

            case TAG_DETAILS:
                printDetails (&top); break;

            case TAG_FILES:
                printFileList (&top); break;

            case TAG_LIST:
                printTorrentList (&top); break;

            case TAG_PEERS:
                printPeers (&top); break;

            case TAG_PIECES:
                printPieces (&top); break;

            case TAG_PORTTEST:
                printPortTest (&top); break;

            case TAG_TRACKERS:
                printTrackers (&top); break;

            case TAG_TORRENT_ADD: {
                int64_t i;
                tr_variant * b = &top;
                if (tr_variantDictFindDict (&top, ARGUMENTS, &b)
                        && tr_variantDictFindDict (b, TR_KEY_torrent_added, &b)
                        && tr_variantDictFindInt (b, TR_KEY_id, &i))
                    tr_snprintf (id, sizeof (id), "%"PRId64, i);
                /* fall-through to default: to give success or failure msg */
            }
            default:
                if (!tr_variantDictFindStr (&top, TR_KEY_result, &str, NULL))
                    status |= EXIT_FAILURE;
                else {
                    printf ("%s responded: \"%s\"\n", rpcurl, str);
                    if (strcmp (str, "success"))
                        status |= EXIT_FAILURE;
                }
        }

        tr_variantFree (&top);
    }
        }
        else
            status |= EXIT_FAILURE;
    }

    return status;
}

static CURL*
tr_curl_easy_init (struct evbuffer * writebuf)
{
    CURL * curl = curl_easy_init ();
    curl_easy_setopt (curl, CURLOPT_USERAGENT, MY_NAME "/" LONG_VERSION_STRING);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, writeFunc);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, writebuf);
    curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, parseResponseHeader);
    curl_easy_setopt (curl, CURLOPT_POST, 1);
    curl_easy_setopt (curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
    curl_easy_setopt (curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt (curl, CURLOPT_VERBOSE, debug);
    curl_easy_setopt (curl, CURLOPT_ENCODING, ""); /* "" tells curl to fill in the blanks with what it was compiled to support */
    if (netrc)
        curl_easy_setopt (curl, CURLOPT_NETRC_FILE, netrc);
    if (auth)
        curl_easy_setopt (curl, CURLOPT_USERPWD, auth);
    if (UseSSL)
    {
        curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0); /* do not verify subject/hostname */
        curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0); /* since most certs will be self-signed, do not verify against CA */
    }
    if (sessionId) {
        char * h = tr_strdup_printf ("%s: %s", TR_RPC_SESSION_ID_HEADER, sessionId);
        struct curl_slist * custom_headers = curl_slist_append (NULL, h);
        curl_easy_setopt (curl, CURLOPT_HTTPHEADER, custom_headers);
        /* fixme: leaks */
    }
    return curl;
}

static int
flush (const char * rpcurl, tr_variant ** benc)
{
    CURLcode res;
    CURL * curl;
    int status = EXIT_SUCCESS;
    struct evbuffer * buf = evbuffer_new ();
    char * json = tr_variantToStr (*benc, TR_VARIANT_FMT_JSON_LEAN, NULL);
    char *rpcurl_http =  tr_strdup_printf (UseSSL? "https://%s" : "http://%s", rpcurl);

    curl = tr_curl_easy_init (buf);
    curl_easy_setopt (curl, CURLOPT_URL, rpcurl_http);
    curl_easy_setopt (curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, getTimeoutSecs (json));

    if (debug)
        fprintf (stderr, "posting:\n--------\n%s\n--------\n", json);

    if ((res = curl_easy_perform (curl)))
    {
        tr_logAddNamedError (MY_NAME, " (%s) %s", rpcurl_http, curl_easy_strerror (res));
        status |= EXIT_FAILURE;
    }
    else
    {
        long response;
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &response);
        switch (response) {
            case 200:
                status |= processResponse (rpcurl, (const char*) evbuffer_pullup (buf, -1), evbuffer_get_length (buf));
                break;
            case 409:
                /* Session id failed. Our curl header func has already
                 * pulled the new session id from this response's headers,
                 * build a new CURL* and try again */
                curl_easy_cleanup (curl);
                curl = NULL;
                status |= flush (rpcurl, benc);
                benc = NULL;
                break;
            default:
                fprintf (stderr, "Unexpected response: %s\n", evbuffer_pullup (buf, -1));
                status |= EXIT_FAILURE;
                break;
        }
    }

    /* cleanup */
    tr_free (rpcurl_http);
    tr_free (json);
    evbuffer_free (buf);
    if (curl != 0)
        curl_easy_cleanup (curl);
    if (benc != NULL) {
        tr_variantFree (*benc);
        *benc = 0;
    }
    return status;
}

static tr_variant*
ensure_sset (tr_variant ** sset)
{
    tr_variant * args;

    if (*sset)
        args = tr_variantDictFind (*sset, ARGUMENTS);
    else {
        *sset = tr_new0 (tr_variant, 1);
        tr_variantInitDict (*sset, 3);
        tr_variantDictAddStr (*sset, TR_KEY_method, "session-set");
        args = tr_variantDictAddDict (*sset, ARGUMENTS, 0);
    }

    return args;
}

static tr_variant*
ensure_tset (tr_variant ** tset)
{
    tr_variant * args;

    if (*tset)
        args = tr_variantDictFind (*tset, ARGUMENTS);
    else {
        *tset = tr_new0 (tr_variant, 1);
        tr_variantInitDict (*tset, 3);
        tr_variantDictAddStr (*tset, TR_KEY_method, "torrent-set");
        args = tr_variantDictAddDict (*tset, ARGUMENTS, 1);
    }

    return args;
}

static int
processArgs (const char * rpcurl, int argc, const char * const * argv)
{
    int c;
    int status = EXIT_SUCCESS;
    const char * optarg;
    tr_variant *sset = 0;
    tr_variant *tset = 0;
    tr_variant *tadd = 0;

    *id = '\0';

    while ((c = tr_getopt (getUsage (), argc, argv, opts, &optarg)))
    {
        const int stepMode = getOptMode (c);

        if (!stepMode) /* meta commands */
        {
            switch (c)
            {
                case 'a': /* add torrent */
                    if (sset != 0) status |= flush (rpcurl, &sset);
                    if (tadd != 0) status |= flush (rpcurl, &tadd);
                    if (tset != 0) { addIdArg (tr_variantDictFind (tset, ARGUMENTS), id, NULL); status |= flush (rpcurl, &tset); }
                    tadd = tr_new0 (tr_variant, 1);
                    tr_variantInitDict (tadd, 3);
                    tr_variantDictAddStr (tadd, TR_KEY_method, "torrent-add");
                    tr_variantDictAddInt (tadd, TR_KEY_tag, TAG_TORRENT_ADD);
                    tr_variantDictAddDict (tadd, ARGUMENTS, 0);
                    break;

                case 'b': /* debug */
                    debug = true;
                    break;

                case 'n': /* auth */
                    auth = tr_strdup (optarg);
                    break;

                case 810: /* authenv */
                    auth = tr_env_get_string ("TR_AUTH", NULL);
                    if (auth == NULL)
                    {
                        fprintf (stderr, "The TR_AUTH environment variable is not set\n");
                        exit (0);
                    }
                    break;

                case 'N': /* netrc */
                    netrc = tr_strdup (optarg);
                    break;

                case 820: /* UseSSL */
                    UseSSL = true;
                    break;

                case 't': /* set current torrent */
                    if (tadd != 0) status |= flush (rpcurl, &tadd);
                    if (tset != 0) { addIdArg (tr_variantDictFind (tset, ARGUMENTS), id, NULL); status |= flush (rpcurl, &tset); }
                    tr_strlcpy (id, optarg, sizeof (id));
                    break;

                case 'V': /* show version number */
                    fprintf (stderr, "%s %s\n", MY_NAME, LONG_VERSION_STRING);
                    exit (0);
                    break;

                case TR_OPT_ERR:
                    fprintf (stderr, "invalid option\n");
                    showUsage ();
                    status |= EXIT_FAILURE;
                    break;

                case TR_OPT_UNK:
                    if (tadd) {
                        tr_variant * args = tr_variantDictFind (tadd, ARGUMENTS);
                        char * tmp = getEncodedMetainfo (optarg);
                        if (tmp)
                            tr_variantDictAddStr (args, TR_KEY_metainfo, tmp);
                        else
                            tr_variantDictAddStr (args, TR_KEY_filename, optarg);
                        tr_free (tmp);
                    } else {
                        fprintf (stderr, "Unknown option: %s\n", optarg);
                        status |= EXIT_FAILURE;
                    }
                    break;
            }
        }
        else if (stepMode == MODE_TORRENT_GET)
        {
            size_t i, n;
            tr_variant * top = tr_new0 (tr_variant, 1);
            tr_variant * args;
            tr_variant * fields;
            tr_variantInitDict (top, 3);
            tr_variantDictAddStr (top, TR_KEY_method, "torrent-get");
            args = tr_variantDictAddDict (top, ARGUMENTS, 0);
            fields = tr_variantDictAddList (args, TR_KEY_fields, 0);

            if (tset != 0) { addIdArg (tr_variantDictFind (tset, ARGUMENTS), id, NULL); status |= flush (rpcurl, &tset); }

            switch (c)
            {
                case 'i': tr_variantDictAddInt (top, TR_KEY_tag, TAG_DETAILS);
                          n = TR_N_ELEMENTS (details_keys);
                          for (i=0; i<n; ++i) tr_variantListAddQuark (fields, details_keys[i]);
                          addIdArg (args, id, NULL);
                          break;
                case 'l': tr_variantDictAddInt (top, TR_KEY_tag, TAG_LIST);
                          n = TR_N_ELEMENTS (list_keys);
                          for (i=0; i<n; ++i) tr_variantListAddQuark (fields, list_keys[i]);
                          addIdArg (args, id, "all");
                          break;
                case 940: tr_variantDictAddInt (top, TR_KEY_tag, TAG_FILES);
                          n = TR_N_ELEMENTS (files_keys);
                          for (i=0; i<n; ++i) tr_variantListAddQuark (fields, files_keys[i]);
                          addIdArg (args, id, NULL);
                          break;
                case 941: tr_variantDictAddInt (top, TR_KEY_tag, TAG_PEERS);
                          tr_variantListAddStr (fields, "peers");
                          addIdArg (args, id, NULL);
                          break;
                case 942: tr_variantDictAddInt (top, TR_KEY_tag, TAG_PIECES);
                          tr_variantListAddStr (fields, "pieces");
                          tr_variantListAddStr (fields, "pieceCount");
                          addIdArg (args, id, NULL);
                          break;
                case 943: tr_variantDictAddInt (top, TR_KEY_tag, TAG_TRACKERS);
                          tr_variantListAddStr (fields, "trackerStats");
                          addIdArg (args, id, NULL);
                          break;
                default:  assert ("unhandled value" && 0);
            }

            status |= flush (rpcurl, &top);
        }
        else if (stepMode == MODE_SESSION_SET)
        {
            tr_variant * args = ensure_sset (&sset);

            switch (c)
            {
                case 800: tr_variantDictAddStr (args, TR_KEY_script_torrent_done_filename, optarg);
                          tr_variantDictAddBool (args, TR_KEY_script_torrent_done_enabled, true);
                          break;
                case 801: tr_variantDictAddBool (args, TR_KEY_script_torrent_done_enabled, false);
                          break;
                case 970: tr_variantDictAddBool (args, TR_KEY_alt_speed_enabled, true);
                          break;
                case 971: tr_variantDictAddBool (args, TR_KEY_alt_speed_enabled, false);
                          break;
                case 972: tr_variantDictAddInt (args, TR_KEY_alt_speed_down, numarg (optarg));
                          break;
                case 973: tr_variantDictAddInt (args, TR_KEY_alt_speed_up, numarg (optarg));
                          break;
                case 974: tr_variantDictAddBool (args, TR_KEY_alt_speed_time_enabled, true);
                          break;
                case 975: tr_variantDictAddBool (args, TR_KEY_alt_speed_time_enabled, false);
                          break;
                case 976: addTime (args, TR_KEY_alt_speed_time_begin, optarg);
                          break;
                case 977: addTime (args, TR_KEY_alt_speed_time_end, optarg);
                          break;
                case 978: addDays (args, TR_KEY_alt_speed_time_day, optarg);
                          break;
                case 'c': tr_variantDictAddStr (args, TR_KEY_incomplete_dir, optarg);
                          tr_variantDictAddBool (args, TR_KEY_incomplete_dir_enabled, true);
                          break;
                case 'C': tr_variantDictAddBool (args, TR_KEY_incomplete_dir_enabled, false);
                          break;
                case 'e': tr_variantDictAddInt (args, TR_KEY_cache_size_mb, atoi (optarg));
                          break;
                case 910: tr_variantDictAddStr (args, TR_KEY_encryption, "required");
                          break;
                case 911: tr_variantDictAddStr (args, TR_KEY_encryption, "preferred");
                          break;
                case 912: tr_variantDictAddStr (args, TR_KEY_encryption, "tolerated");
                          break;
                case 'm': tr_variantDictAddBool (args, TR_KEY_port_forwarding_enabled, true);
                          break;
                case 'M': tr_variantDictAddBool (args, TR_KEY_port_forwarding_enabled, false);
                          break;
                case 'o': tr_variantDictAddBool (args, TR_KEY_dht_enabled, true);
                          break;
                case 'O': tr_variantDictAddBool (args, TR_KEY_dht_enabled, false);
                          break;
                case 830: tr_variantDictAddBool (args, TR_KEY_utp_enabled, true);
                          break;
                case 831: tr_variantDictAddBool (args, TR_KEY_utp_enabled, false);
                          break;
                case 'p': tr_variantDictAddInt (args, TR_KEY_peer_port, numarg (optarg));
                          break;
                case 'P': tr_variantDictAddBool (args, TR_KEY_peer_port_random_on_start, true);
                          break;
                case 'x': tr_variantDictAddBool (args, TR_KEY_pex_enabled, true);
                          break;
                case 'X': tr_variantDictAddBool (args, TR_KEY_pex_enabled, false);
                          break;
                case 'y': tr_variantDictAddBool (args, TR_KEY_lpd_enabled, true);
                          break;
                case 'Y': tr_variantDictAddBool (args, TR_KEY_lpd_enabled, false);
                          break;
                case 953: tr_variantDictAddReal (args, TR_KEY_seedRatioLimit, atof (optarg));
                          tr_variantDictAddBool (args, TR_KEY_seedRatioLimited, true);
                          break;
                case 954: tr_variantDictAddBool (args, TR_KEY_seedRatioLimited, false);
                          break;
                case 990: tr_variantDictAddBool (args, TR_KEY_start_added_torrents, false);
                          break;
                case 991: tr_variantDictAddBool (args, TR_KEY_start_added_torrents, true);
                          break;
                case 992: tr_variantDictAddBool (args, TR_KEY_trash_original_torrent_files, true);
                          break;
                case 993: tr_variantDictAddBool (args, TR_KEY_trash_original_torrent_files, false);
                          break;
                default:  assert ("unhandled value" && 0);
                          break;
            }
        }
        else if (stepMode == (MODE_SESSION_SET | MODE_TORRENT_SET))
        {
            tr_variant * targs = 0;
            tr_variant * sargs = 0;

            if (*id)
                targs = ensure_tset (&tset);
            else
                sargs = ensure_sset (&sset);

            switch (c)
            {
                case 'd': if (targs) {
                              tr_variantDictAddInt (targs, TR_KEY_downloadLimit, numarg (optarg));
                              tr_variantDictAddBool (targs, TR_KEY_downloadLimited, true);
                          } else {
                              tr_variantDictAddInt (sargs, TR_KEY_speed_limit_down, numarg (optarg));
                              tr_variantDictAddBool (sargs, TR_KEY_speed_limit_down_enabled, true);
                          }
                          break;
                case 'D': if (targs)
                              tr_variantDictAddBool (targs, TR_KEY_downloadLimited, false);
                          else
                              tr_variantDictAddBool (sargs, TR_KEY_speed_limit_down_enabled, false);
                          break;
                case 'u': if (targs) {
                              tr_variantDictAddInt (targs, TR_KEY_uploadLimit, numarg (optarg));
                              tr_variantDictAddBool (targs, TR_KEY_uploadLimited, true);
                          } else {
                              tr_variantDictAddInt (sargs, TR_KEY_speed_limit_up, numarg (optarg));
                              tr_variantDictAddBool (sargs, TR_KEY_speed_limit_up_enabled, true);
                          }
                          break;
                case 'U': if (targs)
                              tr_variantDictAddBool (targs, TR_KEY_uploadLimited, false);
                          else
                              tr_variantDictAddBool (sargs, TR_KEY_speed_limit_up_enabled, false);
                          break;
                case 930: if (targs)
                              tr_variantDictAddInt (targs, TR_KEY_peer_limit, atoi (optarg));
                          else
                              tr_variantDictAddInt (sargs, TR_KEY_peer_limit_global, atoi (optarg));
                          break;
                default:  assert ("unhandled value" && 0);
                          break;
            }
        }
        else if (stepMode == MODE_TORRENT_SET)
        {
            tr_variant * args = ensure_tset (&tset);

            switch (c)
            {
                case 712: tr_variantListAddInt (tr_variantDictAddList (args, TR_KEY_trackerRemove, 1), atoi (optarg));
                          break;
                case 950: tr_variantDictAddReal (args, TR_KEY_seedRatioLimit, atof (optarg));
                          tr_variantDictAddInt (args, TR_KEY_seedRatioMode, TR_RATIOLIMIT_SINGLE);
                          break;
                case 951: tr_variantDictAddInt (args, TR_KEY_seedRatioMode, TR_RATIOLIMIT_GLOBAL);
                          break;
                case 952: tr_variantDictAddInt (args, TR_KEY_seedRatioMode, TR_RATIOLIMIT_UNLIMITED);
                          break;
                case 984: tr_variantDictAddBool (args, TR_KEY_honorsSessionLimits, true);
                          break;
                case 985: tr_variantDictAddBool (args, TR_KEY_honorsSessionLimits, false);
                          break;
                default:  assert ("unhandled value" && 0);
                          break;
            }
        }
        else if (stepMode == (MODE_TORRENT_SET | MODE_TORRENT_ADD))
        {
            tr_variant * args;

            if (tadd)
                args = tr_variantDictFind (tadd, ARGUMENTS);
            else
                args = ensure_tset (&tset);

            switch (c)
            {
                case 'g': addFiles (args, TR_KEY_files_wanted, optarg);
                          break;
                case 'G': addFiles (args, TR_KEY_files_unwanted, optarg);
                          break;
                case 900: addFiles (args, TR_KEY_priority_high, optarg);
                          break;
                case 901: addFiles (args, TR_KEY_priority_normal, optarg);
                          break;
                case 902: addFiles (args, TR_KEY_priority_low, optarg);
                          break;
                case 700: tr_variantDictAddInt (args, TR_KEY_bandwidthPriority,  1);
                          break;
                case 701: tr_variantDictAddInt (args, TR_KEY_bandwidthPriority,  0);
                          break;
                case 702: tr_variantDictAddInt (args, TR_KEY_bandwidthPriority, -1);
                          break;
                case 710: tr_variantListAddStr (tr_variantDictAddList (args, TR_KEY_trackerAdd, 1), optarg);
                          break;
                default:  assert ("unhandled value" && 0);
                          break;
            }
        }
        else if (c == 961) /* set location */
        {
            if (tadd)
            {
                tr_variant * args = tr_variantDictFind (tadd, ARGUMENTS);
                tr_variantDictAddStr (args, TR_KEY_download_dir, optarg);
            }
            else
            {
                tr_variant * args;
                tr_variant * top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 2);
                tr_variantDictAddStr (top, TR_KEY_method, "torrent-set-location");
                args = tr_variantDictAddDict (top, ARGUMENTS, 3);
                tr_variantDictAddStr (args, TR_KEY_location, optarg);
                tr_variantDictAddBool (args, TR_KEY_move, false);
                addIdArg (args, id, NULL);
                status |= flush (rpcurl, &top);
                break;
            }
        }
        else switch (c)
        {
            case 920: /* session-info */
            {
                tr_variant * top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 2);
                tr_variantDictAddStr (top, TR_KEY_method, "session-get");
                tr_variantDictAddInt (top, TR_KEY_tag, TAG_SESSION);
                status |= flush (rpcurl, &top);
                break;
            }
            case 's': /* start */
            {
                if (tadd)
                    tr_variantDictAddBool (tr_variantDictFind (tadd, TR_KEY_arguments), TR_KEY_paused, false);
                else {
                    tr_variant * top = tr_new0 (tr_variant, 1);
                    tr_variantInitDict (top, 2);
                    tr_variantDictAddStr (top, TR_KEY_method, "torrent-start");
                    addIdArg (tr_variantDictAddDict (top, ARGUMENTS, 1), id, NULL);
                    status |= flush (rpcurl, &top);
                }
                break;
            }
            case 'S': /* stop */
            {
                if (tadd)
                    tr_variantDictAddBool (tr_variantDictFind (tadd, TR_KEY_arguments), TR_KEY_paused, true);
                else {
                    tr_variant * top = tr_new0 (tr_variant, 1);
                    tr_variantInitDict (top, 2);
                    tr_variantDictAddStr (top, TR_KEY_method, "torrent-stop");
                    addIdArg (tr_variantDictAddDict (top, ARGUMENTS, 1), id, NULL);
                    status |= flush (rpcurl, &top);
                }
                break;
            }
            case 'w':
            {
                tr_variant * args = tadd ? tr_variantDictFind (tadd, TR_KEY_arguments) : ensure_sset (&sset);
                tr_variantDictAddStr (args, TR_KEY_download_dir, optarg);
                break;
            }
            case 850:
            {
                tr_variant * top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 1);
                tr_variantDictAddStr (top, TR_KEY_method, "session-close");
                status |= flush (rpcurl, &top);
                break;
            }
            case 963:
            {
                tr_variant * top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 1);
                tr_variantDictAddStr (top, TR_KEY_method, "blocklist-update");
                status |= flush (rpcurl, &top);
                break;
            }
            case 921:
            {
                tr_variant * top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 2);
                tr_variantDictAddStr (top, TR_KEY_method, "session-stats");
                tr_variantDictAddInt (top, TR_KEY_tag, TAG_STATS);
                status |= flush (rpcurl, &top);
                break;
            }
            case 962:
            {
                tr_variant * top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 2);
                tr_variantDictAddStr (top, TR_KEY_method, "port-test");
                tr_variantDictAddInt (top, TR_KEY_tag, TAG_PORTTEST);
                status |= flush (rpcurl, &top);
                break;
            }
            case 600:
            {
                tr_variant * top;
                if (tset != 0) { addIdArg (tr_variantDictFind (tset, ARGUMENTS), id, NULL); status |= flush (rpcurl, &tset); }
                top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 2);
                tr_variantDictAddStr (top, TR_KEY_method, "torrent-reannounce");
                addIdArg (tr_variantDictAddDict (top, ARGUMENTS, 1), id, NULL);
                status |= flush (rpcurl, &top);
                break;
            }
            case 'v':
            {
                tr_variant * top;
                if (tset != 0) { addIdArg (tr_variantDictFind (tset, ARGUMENTS), id, NULL); status |= flush (rpcurl, &tset); }
                top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 2);
                tr_variantDictAddStr (top, TR_KEY_method, "torrent-verify");
                addIdArg (tr_variantDictAddDict (top, ARGUMENTS, 1), id, NULL);
                status |= flush (rpcurl, &top);
                break;
            }
            case 'r':
            case 840:
            {
                tr_variant * args;
                tr_variant * top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 2);
                tr_variantDictAddStr (top, TR_KEY_method, "torrent-remove");
                args = tr_variantDictAddDict (top, ARGUMENTS, 2);
                tr_variantDictAddBool (args, TR_KEY_delete_local_data, c == 840);
                addIdArg (args, id, NULL);
                status |= flush (rpcurl, &top);
                break;
            }
            case 960:
            {
                tr_variant * args;
                tr_variant * top = tr_new0 (tr_variant, 1);
                tr_variantInitDict (top, 2);
                tr_variantDictAddStr (top, TR_KEY_method, "torrent-set-location");
                args = tr_variantDictAddDict (top, ARGUMENTS, 3);
                tr_variantDictAddStr (args, TR_KEY_location, optarg);
                tr_variantDictAddBool (args, TR_KEY_move, true);
                addIdArg (args, id, NULL);
                status |= flush (rpcurl, &top);
                break;
            }
            default:
            {
                fprintf (stderr, "got opt [%d]\n", c);
                showUsage ();
                break;
            }
        }
    }

    if (tadd != 0) status |= flush (rpcurl, &tadd);
    if (tset != 0) { addIdArg (tr_variantDictFind (tset, ARGUMENTS), id, NULL); status |= flush (rpcurl, &tset); }
    if (sset != 0) status |= flush (rpcurl, &sset);
    return status;
}

/* [host:port] or [host] or [port] or [http (s?)://host:port/transmission/] */
static void
getHostAndPortAndRpcUrl (int * argc, char ** argv,
                         char ** host, int * port, char ** rpcurl)
{
    if (*argv[1] != '-')
    {
        int          i;
        const char * s = argv[1];
        const char * delim = strchr (s, ':');
        if (!strncmp (s, "http://", 7))   /* user passed in http rpc url */
        {
            *rpcurl = tr_strdup_printf ("%s/rpc/", s + 7);
        }
        else if (!strncmp (s, "https://", 8)) /* user passed in https rpc url */
        {
            UseSSL = true;
            *rpcurl = tr_strdup_printf ("%s/rpc/", s + 8);
        }
        else if (delim)   /* user passed in both host and port */
        {
            *host = tr_strndup (s, delim - s);
            *port = atoi (delim + 1);
        }
        else
        {
            char *    end;
            const int i = strtol (s, &end, 10);
            if (!*end) /* user passed in a port */
                *port = i;
            else /* user passed in a host */
                *host = tr_strdup (s);
        }

        *argc -= 1;
        for (i = 1; i < *argc; ++i)
            argv[i] = argv[i + 1];
    }
}

int
tr_main (int    argc,
         char * argv[])
{
    int port = DEFAULT_PORT;
    char * host = NULL;
    char * rpcurl = NULL;
    int exit_status = EXIT_SUCCESS;

    if (argc < 2) {
        showUsage ();
        return EXIT_FAILURE;
    }

    tr_formatter_mem_init (MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
    tr_formatter_size_init (DISK_K,DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
    tr_formatter_speed_init (SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);

    getHostAndPortAndRpcUrl (&argc, argv, &host, &port, &rpcurl);
    if (host == NULL)
        host = tr_strdup (DEFAULT_HOST);
    if (rpcurl == NULL)
        rpcurl = tr_strdup_printf ("%s:%d%s", host, port, DEFAULT_URL);

    exit_status = processArgs (rpcurl, argc, (const char* const *)argv);

    tr_free (host);
    tr_free (rpcurl);
    return exit_status;
}
