/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <assert.h>
#include <ctype.h> /* isspace */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strcmp */
#include <string_view>

#include <event2/buffer.h>
#include <event2/util.h>

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
#define DEFAULT_PORT TR_DEFAULT_RPC_PORT
#define DEFAULT_URL TR_DEFAULT_RPC_URL_STR "rpc/"

#define ARGUMENTS TR_KEY_arguments

#define MEM_K 1024
#define MEM_K_STR "KiB"
#define MEM_M_STR "MiB"
#define MEM_G_STR "GiB"
#define MEM_T_STR "TiB"

#define DISK_K 1000
#define DISK_K_STR "kB"
#define DISK_M_STR "MB"
#define DISK_G_STR "GB"
#define DISK_T_STR "TB"

#define SPEED_K 1000
#define SPEED_K_STR "kB/s"
#define SPEED_M_STR "MB/s"
#define SPEED_G_STR "GB/s"
#define SPEED_T_STR "TB/s"

using namespace std::literals;

/***
****
****  Display Utilities
****
***/

static void etaToString(char* buf, size_t buflen, int64_t eta)
{
    if (eta < 0)
    {
        tr_snprintf(buf, buflen, "Unknown");
    }
    else if (eta < 60)
    {
        tr_snprintf(buf, buflen, "%" PRId64 " sec", eta);
    }
    else if (eta < (60 * 60))
    {
        tr_snprintf(buf, buflen, "%" PRId64 " min", eta / 60);
    }
    else if (eta < (60 * 60 * 24))
    {
        tr_snprintf(buf, buflen, "%" PRId64 " hrs", eta / (60 * 60));
    }
    else
    {
        tr_snprintf(buf, buflen, "%" PRId64 " days", eta / (60 * 60 * 24));
    }
}

static char* tr_strltime(char* buf, int seconds, size_t buflen)
{
    int days;
    int hours;
    int minutes;
    int total_seconds;
    char b[128];
    char d[128];
    char h[128];
    char m[128];
    char s[128];
    char t[128];

    if (seconds < 0)
    {
        seconds = 0;
    }

    total_seconds = seconds;
    days = seconds / 86400;
    hours = (seconds % 86400) / 3600;
    minutes = (seconds % 3600) / 60;
    seconds = (seconds % 3600) % 60;

    tr_snprintf(d, sizeof(d), "%d %s", days, days == 1 ? "day" : "days");
    tr_snprintf(h, sizeof(h), "%d %s", hours, hours == 1 ? "hour" : "hours");
    tr_snprintf(m, sizeof(m), "%d %s", minutes, minutes == 1 ? "minute" : "minutes");
    tr_snprintf(s, sizeof(s), "%d %s", seconds, seconds == 1 ? "second" : "seconds");
    tr_snprintf(t, sizeof(t), "%d %s", total_seconds, total_seconds == 1 ? "second" : "seconds");

    if (days != 0)
    {
        if (days >= 4 || hours == 0)
        {
            tr_strlcpy(b, d, sizeof(b));
        }
        else
        {
            tr_snprintf(b, sizeof(b), "%s, %s", d, h);
        }
    }
    else if (hours != 0)
    {
        if (hours >= 4 || minutes == 0)
        {
            tr_strlcpy(b, h, sizeof(b));
        }
        else
        {
            tr_snprintf(b, sizeof(b), "%s, %s", h, m);
        }
    }
    else if (minutes != 0)
    {
        if (minutes >= 4 || seconds == 0)
        {
            tr_strlcpy(b, m, sizeof(b));
        }
        else
        {
            tr_snprintf(b, sizeof(b), "%s, %s", m, s);
        }
    }
    else
    {
        tr_strlcpy(b, s, sizeof(b));
    }

    tr_snprintf(buf, buflen, "%s (%s)", b, t);
    return buf;
}

static char* strlpercent(char* buf, double x, size_t buflen)
{
    return tr_strpercent(buf, x, buflen);
}

static char* strlratio2(char* buf, double ratio, size_t buflen)
{
    return tr_strratio(buf, buflen, ratio, "Inf");
}

static char* strlratio(char* buf, int64_t numerator, int64_t denominator, size_t buflen)
{
    double ratio;

    if (denominator != 0)
    {
        ratio = numerator / (double)denominator;
    }
    else if (numerator != 0)
    {
        ratio = TR_RATIO_INF;
    }
    else
    {
        ratio = TR_RATIO_NA;
    }

    return strlratio2(buf, ratio, buflen);
}

static char* strlmem(char* buf, int64_t bytes, size_t buflen)
{
    if (bytes == 0)
    {
        tr_strlcpy(buf, "None", buflen);
    }
    else
    {
        tr_formatter_mem_B(buf, bytes, buflen);
    }

    return buf;
}

static char* strlsize(char* buf, int64_t bytes, size_t buflen)
{
    if (bytes < 0)
    {
        tr_strlcpy(buf, "Unknown", buflen);
    }
    else if (bytes == 0)
    {
        tr_strlcpy(buf, "None", buflen);
    }
    else
    {
        tr_formatter_size_B(buf, bytes, buflen);
    }

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

static char const* getUsage(void)
{
    // clang-format off
    return
        MY_NAME " " LONG_VERSION_STRING "\n"
        "A fast and easy BitTorrent client\n"
        "https://transmissionbt.com/\n"
        "\n"
        "Usage: " MY_NAME " [host] [options]\n"
        "       " MY_NAME " [port] [options]\n"
        "       " MY_NAME " [host:port] [options]\n"
        "       " MY_NAME " [http(s?)://host:port/transmission/] [options]\n"
        "\n"
        "See the man page for detailed explanations and many examples.";
    // clang-format on
}

/***
****
****  Command-Line Arguments
****
***/

static tr_option opts[] = {
    { 'a', "add", "Add torrent files by filename or URL", "a", false, nullptr },
    { 970, "alt-speed", "Use the alternate Limits", "as", false, nullptr },
    { 971, "no-alt-speed", "Don't use the alternate Limits", "AS", false, nullptr },
    { 972, "alt-speed-downlimit", "max alternate download speed (in " SPEED_K_STR ")", "asd", true, "<speed>" },
    { 973, "alt-speed-uplimit", "max alternate upload speed (in " SPEED_K_STR ")", "asu", true, "<speed>" },
    { 974, "alt-speed-scheduler", "Use the scheduled on/off times", "asc", false, nullptr },
    { 975, "no-alt-speed-scheduler", "Don't use the scheduled on/off times", "ASC", false, nullptr },
    { 976, "alt-speed-time-begin", "Time to start using the alt speed limits (in hhmm)", nullptr, true, "<time>" },
    { 977, "alt-speed-time-end", "Time to stop using the alt speed limits (in hhmm)", nullptr, true, "<time>" },
    { 978, "alt-speed-days", "Numbers for any/all days of the week - eg. \"1-7\"", nullptr, true, "<days>" },
    { 963, "blocklist-update", "Blocklist update", nullptr, false, nullptr },
    { 'c', "incomplete-dir", "Where to store new torrents until they're complete", "c", true, "<dir>" },
    { 'C', "no-incomplete-dir", "Don't store incomplete torrents in a different location", "C", false, nullptr },
    { 'b', "debug", "Print debugging information", "b", false, nullptr },
    { 'd',
      "downlimit",
      "Set the max download speed in " SPEED_K_STR " for the current torrent(s) or globally",
      "d",
      true,
      "<speed>" },
    { 'D', "no-downlimit", "Disable max download speed for the current torrent(s) or globally", "D", false, nullptr },
    { 'e', "cache", "Set the maximum size of the session's memory cache (in " MEM_M_STR ")", "e", true, "<size>" },
    { 910, "encryption-required", "Encrypt all peer connections", "er", false, nullptr },
    { 911, "encryption-preferred", "Prefer encrypted peer connections", "ep", false, nullptr },
    { 912, "encryption-tolerated", "Prefer unencrypted peer connections", "et", false, nullptr },
    { 850, "exit", "Tell the transmission session to shut down", nullptr, false, nullptr },
    { 940, "files", "List the current torrent(s)' files", "f", false, nullptr },
    { 'g', "get", "Mark files for download", "g", true, "<files>" },
    { 'G', "no-get", "Mark files for not downloading", "G", true, "<files>" },
    { 'i', "info", "Show the current torrent(s)' details", "i", false, nullptr },
    { 940, "info-files", "List the current torrent(s)' files", "if", false, nullptr },
    { 941, "info-peers", "List the current torrent(s)' peers", "ip", false, nullptr },
    { 942, "info-pieces", "List the current torrent(s)' pieces", "ic", false, nullptr },
    { 943, "info-trackers", "List the current torrent(s)' trackers", "it", false, nullptr },
    { 920, "session-info", "Show the session's details", "si", false, nullptr },
    { 921, "session-stats", "Show the session's statistics", "st", false, nullptr },
    { 'l', "list", "List all torrents", "l", false, nullptr },
    { 'L', "labels", "Set the current torrents' labels", "L", true, "<label[,label...]>" },
    { 960, "move", "Move current torrent's data to a new folder", nullptr, true, "<path>" },
    { 961, "find", "Tell Transmission where to find a torrent's data", nullptr, true, "<path>" },
    { 'm', "portmap", "Enable portmapping via NAT-PMP or UPnP", "m", false, nullptr },
    { 'M', "no-portmap", "Disable portmapping", "M", false, nullptr },
    { 'n', "auth", "Set username and password", "n", true, "<user:pw>" },
    { 810, "authenv", "Set authentication info from the TR_AUTH environment variable (user:pw)", "ne", false, nullptr },
    { 'N', "netrc", "Set authentication info from a .netrc file", "N", true, "<file>" },
    { 820, "ssl", "Use SSL when talking to daemon", nullptr, false, nullptr },
    { 'o', "dht", "Enable distributed hash tables (DHT)", "o", false, nullptr },
    { 'O', "no-dht", "Disable distributed hash tables (DHT)", "O", false, nullptr },
    { 'p', "port", "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "p", true, "<port>" },
    { 962, "port-test", "Port testing", "pt", false, nullptr },
    { 'P', "random-port", "Random port for incoming peers", "P", false, nullptr },
    { 900, "priority-high", "Try to download these file(s) first", "ph", true, "<files>" },
    { 901, "priority-normal", "Try to download these file(s) normally", "pn", true, "<files>" },
    { 902, "priority-low", "Try to download these file(s) last", "pl", true, "<files>" },
    { 700, "bandwidth-high", "Give this torrent first chance at available bandwidth", "Bh", false, nullptr },
    { 701, "bandwidth-normal", "Give this torrent bandwidth left over by high priority torrents", "Bn", false, nullptr },
    { 702,
      "bandwidth-low",
      "Give this torrent bandwidth left over by high and normal priority torrents",
      "Bl",
      false,
      nullptr },
    { 600, "reannounce", "Reannounce the current torrent(s)", nullptr, false, nullptr },
    { 'r', "remove", "Remove the current torrent(s)", "r", false, nullptr },
    { 930, "peers", "Set the maximum number of peers for the current torrent(s) or globally", "pr", true, "<max>" },
    { 840, "remove-and-delete", "Remove the current torrent(s) and delete local data", "rad", false, nullptr },
    { 800, "torrent-done-script", "Specify a script to run when a torrent finishes", nullptr, true, "<file>" },
    { 801, "no-torrent-done-script", "Don't run a script when torrents finish", nullptr, false, nullptr },
    { 950, "seedratio", "Let the current torrent(s) seed until a specific ratio", "sr", true, "ratio" },
    { 951, "seedratio-default", "Let the current torrent(s) use the global seedratio settings", "srd", false, nullptr },
    { 952, "no-seedratio", "Let the current torrent(s) seed regardless of ratio", "SR", false, nullptr },
    { 953,
      "global-seedratio",
      "All torrents, unless overridden by a per-torrent setting, should seed until a specific ratio",
      "gsr",
      true,
      "ratio" },
    { 954,
      "no-global-seedratio",
      "All torrents, unless overridden by a per-torrent setting, should seed regardless of ratio",
      "GSR",
      false,
      nullptr },
    { 710, "tracker-add", "Add a tracker to a torrent", "td", true, "<tracker>" },
    { 712, "tracker-remove", "Remove a tracker from a torrent", "tr", true, "<trackerId>" },
    { 's', "start", "Start the current torrent(s)", "s", false, nullptr },
    { 'S', "stop", "Stop the current torrent(s)", "S", false, nullptr },
    { 't', "torrent", "Set the current torrent(s)", "t", true, "<torrent>" },
    { 990, "start-paused", "Start added torrents paused", nullptr, false, nullptr },
    { 991, "no-start-paused", "Start added torrents unpaused", nullptr, false, nullptr },
    { 992, "trash-torrent", "Delete torrents after adding", nullptr, false, nullptr },
    { 993, "no-trash-torrent", "Do not delete torrents after adding", nullptr, false, nullptr },
    { 984, "honor-session", "Make the current torrent(s) honor the session limits", "hl", false, nullptr },
    { 985, "no-honor-session", "Make the current torrent(s) not honor the session limits", "HL", false, nullptr },
    { 'u',
      "uplimit",
      "Set the max upload speed in " SPEED_K_STR " for the current torrent(s) or globally",
      "u",
      true,
      "<speed>" },
    { 'U', "no-uplimit", "Disable max upload speed for the current torrent(s) or globally", "U", false, nullptr },
    { 830, "utp", "Enable uTP for peer connections", nullptr, false, nullptr },
    { 831, "no-utp", "Disable uTP for peer connections", nullptr, false, nullptr },
    { 'v', "verify", "Verify the current torrent(s)", "v", false, nullptr },
    { 'V', "version", "Show version number and exit", "V", false, nullptr },
    { 'w',
      "download-dir",
      "When used in conjunction with --add, set the new torrent's download folder. "
      "Otherwise, set the default download folder",
      "w",
      true,
      "<path>" },
    { 'x', "pex", "Enable peer exchange (PEX)", "x", false, nullptr },
    { 'X', "no-pex", "Disable peer exchange (PEX)", "X", false, nullptr },
    { 'y', "lpd", "Enable local peer discovery (LPD)", "y", false, nullptr },
    { 'Y', "no-lpd", "Disable local peer discovery (LPD)", "Y", false, nullptr },
    { 941, "peer-info", "List the current torrent(s)' peers", "pi", false, nullptr },
    { 0, nullptr, nullptr, nullptr, false, nullptr }
};

static void showUsage(void)
{
    tr_getopt_usage(MY_NAME, getUsage(), opts);
}

static int numarg(char const* arg)
{
    char* end = nullptr;
    long const num = strtol(arg, &end, 10);

    if (*end != '\0')
    {
        fprintf(stderr, "Not a number: \"%s\"\n", arg);
        showUsage();
        exit(EXIT_FAILURE);
    }

    return num;
}

enum
{
    MODE_TORRENT_START = (1 << 0),
    MODE_TORRENT_STOP = (1 << 1),
    MODE_TORRENT_VERIFY = (1 << 2),
    MODE_TORRENT_REANNOUNCE = (1 << 3),
    MODE_TORRENT_SET = (1 << 4),
    MODE_TORRENT_GET = (1 << 5),
    MODE_TORRENT_ADD = (1 << 6),
    MODE_TORRENT_REMOVE = (1 << 7),
    MODE_TORRENT_SET_LOCATION = (1 << 8),
    MODE_SESSION_SET = (1 << 9),
    MODE_SESSION_GET = (1 << 10),
    MODE_SESSION_STATS = (1 << 11),
    MODE_SESSION_CLOSE = (1 << 12),
    MODE_BLOCKLIST_UPDATE = (1 << 13),
    MODE_PORT_TEST = (1 << 14)
};

static int getOptMode(int val)
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

    case 'L': /* labels */
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
        fprintf(stderr, "unrecognized argument %d\n", val);
        assert("unrecognized argument" && 0);
        return 0;
    }
}

static bool debug = false;
static char* auth = nullptr;
static char* netrc = nullptr;
static char* sessionId = nullptr;
static bool UseSSL = false;

static char* getEncodedMetainfo(char const* filename)
{
    size_t len = 0;
    char* b64 = nullptr;
    uint8_t* buf = tr_loadFile(filename, &len, nullptr);

    if (buf != nullptr)
    {
        b64 = static_cast<char*>(tr_base64_encode(buf, len, nullptr));
        tr_free(buf);
    }

    return b64;
}

static void addIdArg(tr_variant* args, char const* id_str, char const* fallback)
{
    if (tr_str_is_empty(id_str))
    {
        id_str = fallback;

        if (tr_str_is_empty(id_str))
        {
            fprintf(stderr, "No torrent specified!  Please use the -t option first.\n");
            id_str = "-1"; /* no torrent will have this ID, so will act as a no-op */
        }
    }

    if (tr_strcmp0(id_str, "active") == 0)
    {
        tr_variantDictAddStrView(args, TR_KEY_ids, "recently-active"sv);
    }
    else if (strcmp(id_str, "all") != 0)
    {
        bool isList = strchr(id_str, ',') != nullptr || strchr(id_str, '-') != nullptr;
        bool isNum = true;

        for (char const* pch = id_str; isNum && *pch != '\0'; ++pch)
        {
            isNum = isdigit(*pch);
        }

        if (isNum || isList)
        {
            tr_rpc_parse_list_str(tr_variantDictAdd(args, TR_KEY_ids), id_str);
        }
        else
        {
            tr_variantDictAddStr(args, TR_KEY_ids, id_str); /* it's a torrent sha hash */
        }
    }
}

static void addTime(tr_variant* args, tr_quark const key, char const* arg)
{
    int time = 0;
    bool success = false;

    if (arg != nullptr && strlen(arg) == 4)
    {
        char const hh[3] = { arg[0], arg[1], '\0' };
        char const mm[3] = { arg[2], arg[3], '\0' };
        int const hour = atoi(hh);
        int const min = atoi(mm);

        if (0 <= hour && hour < 24 && 0 <= min && min < 60)
        {
            time = min + (hour * 60);
            success = true;
        }
    }

    if (success)
    {
        tr_variantDictAddInt(args, key, time);
    }
    else
    {
        fprintf(stderr, "Please specify the time of day in 'hhmm' format.\n");
    }
}

static void addDays(tr_variant* args, tr_quark const key, char const* arg)
{
    int days = 0;

    if (arg != nullptr)
    {
        for (int& day : tr_parseNumberRange(arg))
        {
            if (day < 0 || day > 7)
            {
                continue;
            }

            if (day == 7)
            {
                day = 0;
            }

            days |= 1 << day;
        }
    }

    if (days != 0)
    {
        tr_variantDictAddInt(args, key, days);
    }
    else
    {
        fprintf(stderr, "Please specify the days of the week in '1-3,4,7' format.\n");
    }
}

static void addLabels(tr_variant* args, std::string_view comma_delimited_labels)
{
    tr_variant* labels;
    if (!tr_variantDictFindList(args, TR_KEY_labels, &labels))
    {
        labels = tr_variantDictAddList(args, TR_KEY_labels, 10);
    }

    auto label = std::string_view{};
    while (tr_strvSep(&comma_delimited_labels, &label, ','))
    {
        tr_variantListAddStr(labels, label);
    }
}

static void addFiles(tr_variant* args, tr_quark const key, char const* arg)
{
    tr_variant* files = tr_variantDictAddList(args, key, 100);

    if (tr_str_is_empty(arg))
    {
        fprintf(stderr, "No files specified!\n");
        arg = "-1"; /* no file will have this index, so should be a no-op */
    }

    if (strcmp(arg, "all") != 0)
    {
        for (auto const& idx : tr_parseNumberRange(arg))
        {
            tr_variantListAddInt(files, idx);
        }
    }
}

static tr_quark const files_keys[] = {
    TR_KEY_files,
    TR_KEY_name,
    TR_KEY_priorities,
    TR_KEY_wanted,
};

static tr_quark const details_keys[] = {
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
    TR_KEY_labels,
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
    TR_KEY_source,
    TR_KEY_startDate,
    TR_KEY_status,
    TR_KEY_totalSize,
    TR_KEY_uploadedEver,
    TR_KEY_uploadLimit,
    TR_KEY_uploadLimited,
    TR_KEY_webseeds,
    TR_KEY_webseedsSendingToUs,
};

static tr_quark const list_keys[] = {
    TR_KEY_error, //
    TR_KEY_errorString, //
    TR_KEY_eta, //
    TR_KEY_id, //
    TR_KEY_isFinished, //
    TR_KEY_leftUntilDone, //
    TR_KEY_name, //
    TR_KEY_peersGettingFromUs, //
    TR_KEY_peersSendingToUs, //
    TR_KEY_rateDownload, //
    TR_KEY_rateUpload, //
    TR_KEY_sizeWhenDone, //
    TR_KEY_status, //
    TR_KEY_uploadRatio, //
};

static size_t writeFunc(void* ptr, size_t size, size_t nmemb, void* vbuf)
{
    auto* const buf = static_cast<evbuffer*>(vbuf);
    size_t const byteCount = size * nmemb;
    evbuffer_add(buf, ptr, byteCount);
    return byteCount;
}

/* look for a session id in the header in case the server gives back a 409 */
static size_t parseResponseHeader(void* ptr, size_t size, size_t nmemb, void* /*stream*/)
{
    auto const* const line = static_cast<char const*>(ptr);
    size_t const line_len = size * nmemb;
    char const* key = TR_RPC_SESSION_ID_HEADER ": ";
    size_t const key_len = strlen(key);

    if (line_len >= key_len && evutil_ascii_strncasecmp(line, key, key_len) == 0)
    {
        char const* begin = line + key_len;
        char const* end = begin;

        while (!isspace(*end))
        {
            ++end;
        }

        tr_free(sessionId);
        sessionId = tr_strndup(begin, end - begin);
    }

    return line_len;
}

static long getTimeoutSecs(char const* req)
{
    if (strstr(req, "\"method\":\"blocklist-update\"") != nullptr)
    {
        return 300L;
    }

    return 60L; /* default value */
}

static char* getStatusString(tr_variant* t, char* buf, size_t buflen)
{
    int64_t status;
    bool boolVal;

    if (!tr_variantDictFindInt(t, TR_KEY_status, &status))
    {
        *buf = '\0';
    }
    else
    {
        switch (status)
        {
        case TR_STATUS_DOWNLOAD_WAIT:
        case TR_STATUS_SEED_WAIT:
            tr_strlcpy(buf, "Queued", buflen);
            break;

        case TR_STATUS_STOPPED:
            if (tr_variantDictFindBool(t, TR_KEY_isFinished, &boolVal) && boolVal)
            {
                tr_strlcpy(buf, "Finished", buflen);
            }
            else
            {
                tr_strlcpy(buf, "Stopped", buflen);
            }

            break;

        case TR_STATUS_CHECK_WAIT:
        case TR_STATUS_CHECK:
            {
                char const* str = status == TR_STATUS_CHECK_WAIT ? "Will Verify" : "Verifying";
                double percent;

                if (tr_variantDictFindReal(t, TR_KEY_recheckProgress, &percent))
                {
                    tr_snprintf(buf, buflen, "%s (%.0f%%)", str, floor(percent * 100.0));
                }
                else
                {
                    tr_strlcpy(buf, str, buflen);
                }

                break;
            }

        case TR_STATUS_DOWNLOAD:
        case TR_STATUS_SEED:
            {
                int64_t fromUs = 0;
                int64_t toUs = 0;
                tr_variantDictFindInt(t, TR_KEY_peersGettingFromUs, &fromUs);
                tr_variantDictFindInt(t, TR_KEY_peersSendingToUs, &toUs);

                if (fromUs != 0 && toUs != 0)
                {
                    tr_strlcpy(buf, "Up & Down", buflen);
                }
                else if (toUs != 0)
                {
                    tr_strlcpy(buf, "Downloading", buflen);
                }
                else if (fromUs != 0)
                {
                    int64_t leftUntilDone = 0;
                    tr_variantDictFindInt(t, TR_KEY_leftUntilDone, &leftUntilDone);

                    if (leftUntilDone > 0)
                    {
                        tr_strlcpy(buf, "Uploading", buflen);
                    }
                    else
                    {
                        tr_strlcpy(buf, "Seeding", buflen);
                    }
                }
                else
                {
                    tr_strlcpy(buf, "Idle", buflen);
                }

                break;
            }

        default:
            tr_strlcpy(buf, "Unknown", buflen);
            break;
        }
    }

    return buf;
}

static char const* bandwidthPriorityNames[] = {
    "Low",
    "Normal",
    "High",
    "Invalid",
};

static char* format_date(char* buf, size_t buflen, time_t now)
{
    struct tm tm;
    tr_localtime_r(&now, &tm);
    strftime(buf, buflen, "%a %b %2e %T %Y%n", &tm); /* ctime equiv */
    return buf;
}

static void printDetails(tr_variant* top)
{
    tr_variant* args;
    tr_variant* torrents;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args) && tr_variantDictFindList(args, TR_KEY_torrents, &torrents))
    {
        for (int ti = 0, tCount = tr_variantListSize(torrents); ti < tCount; ++ti)
        {
            tr_variant* t = tr_variantListChild(torrents, ti);
            tr_variant* l;
            char buf[512];
            char buf2[512];
            int64_t i;
            int64_t j;
            int64_t k;
            bool boolVal;
            double d;
            auto sv = std::string_view{};

            printf("NAME\n");

            if (tr_variantDictFindInt(t, TR_KEY_id, &i))
            {
                printf("  Id: %" PRId64 "\n", i);
            }

            if (tr_variantDictFindStrView(t, TR_KEY_name, &sv))
            {
                printf("  Name: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
            }

            if (tr_variantDictFindStrView(t, TR_KEY_hashString, &sv))
            {
                printf("  Hash: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
            }

            if (tr_variantDictFindStrView(t, TR_KEY_magnetLink, &sv))
            {
                printf("  Magnet: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
            }

            if (tr_variantDictFindList(t, TR_KEY_labels, &l))
            {
                size_t child_pos = 0;
                tr_variant const* child;
                while ((child = tr_variantListChild(l, child_pos++)))
                {
                    if (tr_variantGetStrView(child, &sv))
                    {
                        printf(i == 0 ? "%" TR_PRIsv : ", %" TR_PRIsv, TR_PRIsv_ARG(sv));
                    }
                }

                printf("\n");
            }

            printf("\n");

            printf("TRANSFER\n");
            getStatusString(t, buf, sizeof(buf));
            printf("  State: %s\n", buf);

            if (tr_variantDictFindStrView(t, TR_KEY_downloadDir, &sv))
            {
                printf("  Location: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
            }

            if (tr_variantDictFindInt(t, TR_KEY_sizeWhenDone, &i) && tr_variantDictFindInt(t, TR_KEY_leftUntilDone, &j))
            {
                strlpercent(buf, 100.0 * (i - j) / i, sizeof(buf));
                printf("  Percent Done: %s%%\n", buf);
            }

            if (tr_variantDictFindInt(t, TR_KEY_eta, &i))
            {
                printf("  ETA: %s\n", tr_strltime(buf, i, sizeof(buf)));
            }

            if (tr_variantDictFindInt(t, TR_KEY_rateDownload, &i))
            {
                printf("  Download Speed: %s\n", tr_formatter_speed_KBps(buf, i / (double)tr_speed_K, sizeof(buf)));
            }

            if (tr_variantDictFindInt(t, TR_KEY_rateUpload, &i))
            {
                printf("  Upload Speed: %s\n", tr_formatter_speed_KBps(buf, i / (double)tr_speed_K, sizeof(buf)));
            }

            if (tr_variantDictFindInt(t, TR_KEY_haveUnchecked, &i) && tr_variantDictFindInt(t, TR_KEY_haveValid, &j))
            {
                strlsize(buf, i + j, sizeof(buf));
                strlsize(buf2, j, sizeof(buf2));
                printf("  Have: %s (%s verified)\n", buf, buf2);
            }

            if (tr_variantDictFindInt(t, TR_KEY_sizeWhenDone, &i))
            {
                if (i < 1)
                {
                    printf("  Availability: None\n");
                }

                if (tr_variantDictFindInt(t, TR_KEY_desiredAvailable, &j) && tr_variantDictFindInt(t, TR_KEY_leftUntilDone, &k))
                {
                    j += i - k;
                    strlpercent(buf, 100.0 * j / i, sizeof(buf));
                    printf("  Availability: %s%%\n", buf);
                }

                if (tr_variantDictFindInt(t, TR_KEY_totalSize, &j))
                {
                    strlsize(buf2, i, sizeof(buf2));
                    strlsize(buf, j, sizeof(buf));
                    printf("  Total size: %s (%s wanted)\n", buf, buf2);
                }
            }

            if (tr_variantDictFindInt(t, TR_KEY_downloadedEver, &i) && tr_variantDictFindInt(t, TR_KEY_uploadedEver, &j))
            {
                strlsize(buf, i, sizeof(buf));
                printf("  Downloaded: %s\n", buf);
                strlsize(buf, j, sizeof(buf));
                printf("  Uploaded: %s\n", buf);
                strlratio(buf, j, i, sizeof(buf));
                printf("  Ratio: %s\n", buf);
            }

            if (tr_variantDictFindInt(t, TR_KEY_corruptEver, &i))
            {
                strlsize(buf, i, sizeof(buf));
                printf("  Corrupt DL: %s\n", buf);
            }

            if (tr_variantDictFindStrView(t, TR_KEY_errorString, &sv) && !std::empty(sv) &&
                tr_variantDictFindInt(t, TR_KEY_error, &i) && i != 0)
            {
                switch (i)
                {
                case TR_STAT_TRACKER_WARNING:
                    printf("  Tracker gave a warning: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
                    break;

                case TR_STAT_TRACKER_ERROR:
                    printf("  Tracker gave an error: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
                    break;

                case TR_STAT_LOCAL_ERROR:
                    printf("  Error: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
                    break;

                default:
                    break; /* no error */
                }
            }

            if (tr_variantDictFindInt(t, TR_KEY_peersConnected, &i) &&
                tr_variantDictFindInt(t, TR_KEY_peersGettingFromUs, &j) &&
                tr_variantDictFindInt(t, TR_KEY_peersSendingToUs, &k))
            {
                printf("  Peers: connected to %" PRId64 ", uploading to %" PRId64 ", downloading from %" PRId64 "\n", i, j, k);
            }

            if (tr_variantDictFindList(t, TR_KEY_webseeds, &l) && tr_variantDictFindInt(t, TR_KEY_webseedsSendingToUs, &i))
            {
                int64_t const n = tr_variantListSize(l);

                if (n > 0)
                {
                    printf("  Web Seeds: downloading from %" PRId64 " of %" PRId64 " web seeds\n", i, n);
                }
            }

            printf("\n");

            printf("HISTORY\n");

            if (tr_variantDictFindInt(t, TR_KEY_addedDate, &i) && i != 0)
            {
                printf("  Date added:       %s", format_date(buf, sizeof(buf), i));
            }

            if (tr_variantDictFindInt(t, TR_KEY_doneDate, &i) && i != 0)
            {
                printf("  Date finished:    %s", format_date(buf, sizeof(buf), i));
            }

            if (tr_variantDictFindInt(t, TR_KEY_startDate, &i) && i != 0)
            {
                printf("  Date started:     %s", format_date(buf, sizeof(buf), i));
            }

            if (tr_variantDictFindInt(t, TR_KEY_activityDate, &i) && i != 0)
            {
                printf("  Latest activity:  %s", format_date(buf, sizeof(buf), i));
            }

            if (tr_variantDictFindInt(t, TR_KEY_secondsDownloading, &i) && i > 0)
            {
                printf("  Downloading Time: %s\n", tr_strltime(buf, i, sizeof(buf)));
            }

            if (tr_variantDictFindInt(t, TR_KEY_secondsSeeding, &i) && i > 0)
            {
                printf("  Seeding Time:     %s\n", tr_strltime(buf, i, sizeof(buf)));
            }

            printf("\n");

            printf("ORIGINS\n");

            if (tr_variantDictFindInt(t, TR_KEY_dateCreated, &i) && i != 0)
            {
                printf("  Date created: %s", format_date(buf, sizeof(buf), i));
            }

            if (tr_variantDictFindBool(t, TR_KEY_isPrivate, &boolVal))
            {
                printf("  Public torrent: %s\n", (boolVal ? "No" : "Yes"));
            }

            if (tr_variantDictFindStrView(t, TR_KEY_comment, &sv) && !std::empty(sv))
            {
                printf("  Comment: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
            }

            if (tr_variantDictFindStrView(t, TR_KEY_creator, &sv) && !std::empty(sv))
            {
                printf("  Creator: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
            }

            if (tr_variantDictFindStrView(t, TR_KEY_source, &sv) && !std::empty(sv))
            {
                printf("  Source: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
            }

            if (tr_variantDictFindInt(t, TR_KEY_pieceCount, &i))
            {
                printf("  Piece Count: %" PRId64 "\n", i);
            }

            if (tr_variantDictFindInt(t, TR_KEY_pieceSize, &i))
            {
                printf("  Piece Size: %s\n", strlmem(buf, i, sizeof(buf)));
            }

            printf("\n");

            printf("LIMITS & BANDWIDTH\n");

            if (tr_variantDictFindBool(t, TR_KEY_downloadLimited, &boolVal) &&
                tr_variantDictFindInt(t, TR_KEY_downloadLimit, &i))
            {
                printf("  Download Limit: ");

                if (boolVal)
                {
                    printf("%s\n", tr_formatter_speed_KBps(buf, i, sizeof(buf)));
                }
                else
                {
                    printf("Unlimited\n");
                }
            }

            if (tr_variantDictFindBool(t, TR_KEY_uploadLimited, &boolVal) && tr_variantDictFindInt(t, TR_KEY_uploadLimit, &i))
            {
                printf("  Upload Limit: ");

                if (boolVal)
                {
                    printf("%s\n", tr_formatter_speed_KBps(buf, i, sizeof(buf)));
                }
                else
                {
                    printf("Unlimited\n");
                }
            }

            if (tr_variantDictFindInt(t, TR_KEY_seedRatioMode, &i))
            {
                switch (i)
                {
                case TR_RATIOLIMIT_GLOBAL:
                    printf("  Ratio Limit: Default\n");
                    break;

                case TR_RATIOLIMIT_SINGLE:
                    if (tr_variantDictFindReal(t, TR_KEY_seedRatioLimit, &d))
                    {
                        printf("  Ratio Limit: %s\n", strlratio2(buf, d, sizeof(buf)));
                    }

                    break;

                case TR_RATIOLIMIT_UNLIMITED:
                    printf("  Ratio Limit: Unlimited\n");
                    break;

                default:
                    break;
                }
            }

            if (tr_variantDictFindBool(t, TR_KEY_honorsSessionLimits, &boolVal))
            {
                printf("  Honors Session Limits: %s\n", (boolVal ? "Yes" : "No"));
            }

            if (tr_variantDictFindInt(t, TR_KEY_peer_limit, &i))
            {
                printf("  Peer limit: %" PRId64 "\n", i);
            }

            if (tr_variantDictFindInt(t, TR_KEY_bandwidthPriority, &i))
            {
                printf("  Bandwidth Priority: %s\n", bandwidthPriorityNames[(i + 1) & 3]);
            }

            printf("\n");
        }
    }
}

static void printFileList(tr_variant* top)
{
    tr_variant* args;
    tr_variant* torrents;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args) && tr_variantDictFindList(args, TR_KEY_torrents, &torrents))
    {
        for (int i = 0, in = tr_variantListSize(torrents); i < in; ++i)
        {
            tr_variant* d = tr_variantListChild(torrents, i);
            tr_variant* files;
            tr_variant* priorities;
            tr_variant* wanteds;
            auto name = std::string_view{};

            if (tr_variantDictFindStrView(d, TR_KEY_name, &name) && tr_variantDictFindList(d, TR_KEY_files, &files) &&
                tr_variantDictFindList(d, TR_KEY_priorities, &priorities) && tr_variantDictFindList(d, TR_KEY_wanted, &wanteds))
            {
                int const jn = tr_variantListSize(files);
                printf("%" TR_PRIsv " (%d files):\n", TR_PRIsv_ARG(name), jn);
                printf("%3s  %4s %8s %3s %9s  %s\n", "#", "Done", "Priority", "Get", "Size", "Name");

                for (int j = 0; j < jn; ++j)
                {
                    int64_t have;
                    int64_t length;
                    int64_t priority;
                    bool wanted;
                    auto filename = std::string_view{};
                    tr_variant* file = tr_variantListChild(files, j);

                    if (tr_variantDictFindInt(file, TR_KEY_length, &length) &&
                        tr_variantDictFindStrView(file, TR_KEY_name, &filename) &&
                        tr_variantDictFindInt(file, TR_KEY_bytesCompleted, &have) &&
                        tr_variantGetInt(tr_variantListChild(priorities, j), &priority) &&
                        tr_variantGetBool(tr_variantListChild(wanteds, j), &wanted))
                    {
                        char sizestr[64];
                        double percent = (double)have / length;
                        char const* pristr;
                        strlsize(sizestr, length, sizeof(sizestr));

                        switch (priority)
                        {
                        case TR_PRI_LOW:
                            pristr = "Low";
                            break;

                        case TR_PRI_HIGH:
                            pristr = "High";
                            break;

                        default:
                            pristr = "Normal";
                            break;
                        }

                        printf(
                            "%3d: %3.0f%% %-8s %-3s %9s  %" TR_PRIsv "\n",
                            j,
                            floor(100.0 * percent),
                            pristr,
                            wanted ? "Yes" : "No",
                            sizestr,
                            TR_PRIsv_ARG(filename));
                    }
                }
            }
        }
    }
}

static void printPeersImpl(tr_variant* peers)
{
    printf("%-40s  %-12s  %-5s %-6s  %-6s  %s\n", "Address", "Flags", "Done", "Down", "Up", "Client");

    for (int i = 0, n = tr_variantListSize(peers); i < n; ++i)
    {
        auto address = std::string_view{};
        auto client = std::string_view{};
        auto flagstr = std::string_view{};
        auto progress = double{};
        auto rateToClient = int64_t{};
        auto rateToPeer = int64_t{};

        tr_variant* d = tr_variantListChild(peers, i);

        if (tr_variantDictFindStrView(d, TR_KEY_address, &address) &&
            tr_variantDictFindStrView(d, TR_KEY_clientName, &client) && tr_variantDictFindReal(d, TR_KEY_progress, &progress) &&
            tr_variantDictFindStrView(d, TR_KEY_flagStr, &flagstr) &&
            tr_variantDictFindInt(d, TR_KEY_rateToClient, &rateToClient) &&
            tr_variantDictFindInt(d, TR_KEY_rateToPeer, &rateToPeer))
        {
            printf(
                "%-40s  %-12s  %-5.1f %6.1f  %6.1f  %" TR_PRIsv "\n",
                std::string{ address }.c_str(),
                std::string{ flagstr }.c_str(),
                (progress * 100.0),
                rateToClient / (double)tr_speed_K,
                rateToPeer / (double)tr_speed_K,
                TR_PRIsv_ARG(client));
        }
    }
}

static void printPeers(tr_variant* top)
{
    tr_variant* args;
    tr_variant* torrents;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args) && tr_variantDictFindList(args, TR_KEY_torrents, &torrents))
    {
        for (int i = 0, n = tr_variantListSize(torrents); i < n; ++i)
        {
            tr_variant* peers;
            tr_variant* torrent = tr_variantListChild(torrents, i);

            if (tr_variantDictFindList(torrent, TR_KEY_peers, &peers))
            {
                printPeersImpl(peers);

                if (i + 1 < n)
                {
                    printf("\n");
                }
            }
        }
    }
}

static void printPiecesImpl(uint8_t const* raw, size_t raw_len, size_t piece_count)
{
    size_t len = 0;
    auto* const str = static_cast<char*>(tr_base64_decode(raw, raw_len, &len));
    printf("  ");

    size_t piece = 0;
    size_t const col_width = 64;
    for (char const *it = str, *end = it + len; it != end; ++it)
    {
        for (int bit = 0; piece < piece_count && bit < 8; ++bit, ++piece)
        {
            printf("%c", (*it & (1 << (7 - bit))) != 0 ? '1' : '0');
        }

        printf(" ");

        if (piece % col_width == 0)
        {
            printf("\n  ");
        }
    }

    printf("\n");
    tr_free(str);
}

static void printPieces(tr_variant* top)
{
    tr_variant* args;
    tr_variant* torrents;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args) && tr_variantDictFindList(args, TR_KEY_torrents, &torrents))
    {
        for (int i = 0, n = tr_variantListSize(torrents); i < n; ++i)
        {
            int64_t j;
            uint8_t const* raw;
            size_t rawlen;
            tr_variant* torrent = tr_variantListChild(torrents, i);

            if (tr_variantDictFindRaw(torrent, TR_KEY_pieces, &raw, &rawlen) &&
                tr_variantDictFindInt(torrent, TR_KEY_pieceCount, &j))
            {
                assert(j >= 0);
                printPiecesImpl(raw, rawlen, (size_t)j);

                if (i + 1 < n)
                {
                    printf("\n");
                }
            }
        }
    }
}

static void printPortTest(tr_variant* top)
{
    tr_variant* args;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args))
    {
        bool boolVal;

        if (tr_variantDictFindBool(args, TR_KEY_port_is_open, &boolVal))
        {
            printf("Port is open: %s\n", boolVal ? "Yes" : "No");
        }
    }
}

static void printTorrentList(tr_variant* top)
{
    tr_variant* args;
    tr_variant* list;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args) && tr_variantDictFindList(args, TR_KEY_torrents, &list))
    {
        int64_t total_size = 0;
        double total_up = 0;
        double total_down = 0;
        char haveStr[32];

        printf(
            "%6s   %-4s  %9s  %-8s  %6s  %6s  %-5s  %-11s  %s\n",
            "ID",
            "Done",
            "Have",
            "ETA",
            "Up",
            "Down",
            "Ratio",
            "Status",
            "Name");

        for (int i = 0, n = tr_variantListSize(list); i < n; ++i)
        {
            int64_t torId;
            int64_t eta;
            int64_t status;
            int64_t up;
            int64_t down;
            int64_t sizeWhenDone;
            int64_t leftUntilDone;
            double ratio;
            auto name = std::string_view{};
            tr_variant* d = tr_variantListChild(list, i);

            if (tr_variantDictFindInt(d, TR_KEY_eta, &eta) && tr_variantDictFindInt(d, TR_KEY_id, &torId) &&
                tr_variantDictFindInt(d, TR_KEY_leftUntilDone, &leftUntilDone) &&
                tr_variantDictFindStrView(d, TR_KEY_name, &name) && tr_variantDictFindInt(d, TR_KEY_rateDownload, &down) &&
                tr_variantDictFindInt(d, TR_KEY_rateUpload, &up) &&
                tr_variantDictFindInt(d, TR_KEY_sizeWhenDone, &sizeWhenDone) &&
                tr_variantDictFindInt(d, TR_KEY_status, &status) && tr_variantDictFindReal(d, TR_KEY_uploadRatio, &ratio))
            {
                char etaStr[16];
                char statusStr[64];
                char ratioStr[32];
                char doneStr[8];
                int64_t error;
                char errorMark;

                if (sizeWhenDone != 0)
                {
                    tr_snprintf(doneStr, sizeof(doneStr), "%d%%", (int)(100.0 * (sizeWhenDone - leftUntilDone) / sizeWhenDone));
                }
                else
                {
                    tr_strlcpy(doneStr, "n/a", sizeof(doneStr));
                }

                strlsize(haveStr, sizeWhenDone - leftUntilDone, sizeof(haveStr));

                if (leftUntilDone != 0 || eta != -1)
                {
                    etaToString(etaStr, sizeof(etaStr), eta);
                }
                else
                {
                    tr_snprintf(etaStr, sizeof(etaStr), "Done");
                }

                if (tr_variantDictFindInt(d, TR_KEY_error, &error) && error)
                {
                    errorMark = '*';
                }
                else
                {
                    errorMark = ' ';
                }

                printf(
                    "%6d%c  %4s  %9s  %-8s  %6.1f  %6.1f  %5s  %-11s  %" TR_PRIsv "\n",
                    (int)torId,
                    errorMark,
                    doneStr,
                    haveStr,
                    etaStr,
                    up / (double)tr_speed_K,
                    down / (double)tr_speed_K,
                    strlratio2(ratioStr, ratio, sizeof(ratioStr)),
                    getStatusString(d, statusStr, sizeof(statusStr)),
                    TR_PRIsv_ARG(name));

                total_up += up;
                total_down += down;
                total_size += sizeWhenDone - leftUntilDone;
            }
        }

        printf(
            "Sum:           %9s            %6.1f  %6.1f\n",
            strlsize(haveStr, total_size, sizeof(haveStr)),
            total_up / (double)tr_speed_K,
            total_down / (double)tr_speed_K);
    }
}

static void printTrackersImpl(tr_variant* trackerStats)
{
    char buf[512];

    for (size_t i = 0, n = tr_variantListSize(trackerStats); i < n; ++i)
    {
        tr_variant* const t = tr_variantListChild(trackerStats, i);

        auto announceState = int64_t{};
        auto downloadCount = int64_t{};
        auto hasAnnounced = bool{};
        auto hasScraped = bool{};
        auto host = std::string_view{};
        auto isBackup = bool{};
        auto lastAnnouncePeerCount = int64_t{};
        auto lastAnnounceResult = std::string_view{};
        auto lastAnnounceStartTime = int64_t{};
        auto lastAnnounceTime = int64_t{};
        auto lastScrapeResult = std::string_view{};
        auto lastScrapeStartTime = int64_t{};
        auto lastScrapeSucceeded = bool{};
        auto lastScrapeTime = int64_t{};
        auto lastScrapeTimedOut = bool{};
        auto leecherCount = int64_t{};
        auto nextAnnounceTime = int64_t{};
        auto nextScrapeTime = int64_t{};
        auto scrapeState = int64_t{};
        auto seederCount = int64_t{};
        auto tier = int64_t{};
        auto trackerId = int64_t{};
        bool lastAnnounceSucceeded;
        bool lastAnnounceTimedOut;

        if (tr_variantDictFindInt(t, TR_KEY_downloadCount, &downloadCount) &&
            tr_variantDictFindBool(t, TR_KEY_hasAnnounced, &hasAnnounced) &&
            tr_variantDictFindBool(t, TR_KEY_hasScraped, &hasScraped) && tr_variantDictFindStrView(t, TR_KEY_host, &host) &&
            tr_variantDictFindInt(t, TR_KEY_id, &trackerId) && tr_variantDictFindBool(t, TR_KEY_isBackup, &isBackup) &&
            tr_variantDictFindInt(t, TR_KEY_announceState, &announceState) &&
            tr_variantDictFindInt(t, TR_KEY_scrapeState, &scrapeState) &&
            tr_variantDictFindInt(t, TR_KEY_lastAnnouncePeerCount, &lastAnnouncePeerCount) &&
            tr_variantDictFindStrView(t, TR_KEY_lastAnnounceResult, &lastAnnounceResult) &&
            tr_variantDictFindInt(t, TR_KEY_lastAnnounceStartTime, &lastAnnounceStartTime) &&
            tr_variantDictFindBool(t, TR_KEY_lastAnnounceSucceeded, &lastAnnounceSucceeded) &&
            tr_variantDictFindInt(t, TR_KEY_lastAnnounceTime, &lastAnnounceTime) &&
            tr_variantDictFindBool(t, TR_KEY_lastAnnounceTimedOut, &lastAnnounceTimedOut) &&
            tr_variantDictFindStrView(t, TR_KEY_lastScrapeResult, &lastScrapeResult) &&
            tr_variantDictFindInt(t, TR_KEY_lastScrapeStartTime, &lastScrapeStartTime) &&
            tr_variantDictFindBool(t, TR_KEY_lastScrapeSucceeded, &lastScrapeSucceeded) &&
            tr_variantDictFindInt(t, TR_KEY_lastScrapeTime, &lastScrapeTime) &&
            tr_variantDictFindBool(t, TR_KEY_lastScrapeTimedOut, &lastScrapeTimedOut) &&
            tr_variantDictFindInt(t, TR_KEY_leecherCount, &leecherCount) &&
            tr_variantDictFindInt(t, TR_KEY_nextAnnounceTime, &nextAnnounceTime) &&
            tr_variantDictFindInt(t, TR_KEY_nextScrapeTime, &nextScrapeTime) &&
            tr_variantDictFindInt(t, TR_KEY_seederCount, &seederCount) && tr_variantDictFindInt(t, TR_KEY_tier, &tier))
        {
            time_t const now = time(nullptr);

            printf("\n");
            printf("  Tracker %d: %" TR_PRIsv "\n", (int)trackerId, TR_PRIsv_ARG(host));

            if (isBackup)
            {
                printf("  Backup on tier %d\n", (int)tier);
            }
            else
            {
                printf("  Active in tier %d\n", (int)tier);
            }

            if (!isBackup)
            {
                if (hasAnnounced && announceState != TR_TRACKER_INACTIVE)
                {
                    tr_strltime(buf, now - lastAnnounceTime, sizeof(buf));

                    if (lastAnnounceSucceeded)
                    {
                        printf("  Got a list of %d peers %s ago\n", (int)lastAnnouncePeerCount, buf);
                    }
                    else if (lastAnnounceTimedOut)
                    {
                        printf("  Peer list request timed out; will retry\n");
                    }
                    else
                    {
                        printf("  Got an error \"%" TR_PRIsv "\" %s ago\n", TR_PRIsv_ARG(lastAnnounceResult), buf);
                    }
                }

                switch (announceState)
                {
                case TR_TRACKER_INACTIVE:
                    printf("  No updates scheduled\n");
                    break;

                case TR_TRACKER_WAITING:
                    tr_strltime(buf, nextAnnounceTime - now, sizeof(buf));
                    printf("  Asking for more peers in %s\n", buf);
                    break;

                case TR_TRACKER_QUEUED:
                    printf("  Queued to ask for more peers\n");
                    break;

                case TR_TRACKER_ACTIVE:
                    tr_strltime(buf, now - lastAnnounceStartTime, sizeof(buf));
                    printf("  Asking for more peers now... %s\n", buf);
                    break;
                }

                if (hasScraped)
                {
                    tr_strltime(buf, now - lastScrapeTime, sizeof(buf));

                    if (lastScrapeSucceeded)
                    {
                        printf("  Tracker had %d seeders and %d leechers %s ago\n", (int)seederCount, (int)leecherCount, buf);
                    }
                    else if (lastScrapeTimedOut)
                    {
                        printf("  Tracker scrape timed out; will retry\n");
                    }
                    else
                    {
                        printf("  Got a scrape error \"%" TR_PRIsv "\" %s ago\n", TR_PRIsv_ARG(lastScrapeResult), buf);
                    }
                }

                switch (scrapeState)
                {
                case TR_TRACKER_INACTIVE:
                    break;

                case TR_TRACKER_WAITING:
                    tr_strltime(buf, nextScrapeTime - now, sizeof(buf));
                    printf("  Asking for peer counts in %s\n", buf);
                    break;

                case TR_TRACKER_QUEUED:
                    printf("  Queued to ask for peer counts\n");
                    break;

                case TR_TRACKER_ACTIVE:
                    tr_strltime(buf, now - lastScrapeStartTime, sizeof(buf));
                    printf("  Asking for peer counts now... %s\n", buf);
                    break;
                }
            }
        }
    }
}

static void printTrackers(tr_variant* top)
{
    tr_variant* args;
    tr_variant* torrents;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args) && tr_variantDictFindList(args, TR_KEY_torrents, &torrents))
    {
        for (int i = 0, n = tr_variantListSize(torrents); i < n; ++i)
        {
            tr_variant* trackerStats;
            tr_variant* torrent = tr_variantListChild(torrents, i);

            if (tr_variantDictFindList(torrent, TR_KEY_trackerStats, &trackerStats))
            {
                printTrackersImpl(trackerStats);

                if (i + 1 < n)
                {
                    printf("\n");
                }
            }
        }
    }
}

static void printSession(tr_variant* top)
{
    tr_variant* args;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args))
    {
        int64_t i;
        bool boolVal;
        auto sv = std::string_view{};
        char buf[128];

        printf("VERSION\n");

        if (tr_variantDictFindStrView(args, TR_KEY_version, &sv))
        {
            printf("  Daemon version: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
        }

        if (tr_variantDictFindInt(args, TR_KEY_rpc_version, &i))
        {
            printf("  RPC version: %" PRId64 "\n", i);
        }

        if (tr_variantDictFindInt(args, TR_KEY_rpc_version_minimum, &i))
        {
            printf("  RPC minimum version: %" PRId64 "\n", i);
        }

        printf("\n");

        printf("CONFIG\n");

        if (tr_variantDictFindStrView(args, TR_KEY_config_dir, &sv))
        {
            printf("  Configuration directory: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
        }

        if (tr_variantDictFindStrView(args, TR_KEY_download_dir, &sv))
        {
            printf("  Download directory: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
        }

        if (tr_variantDictFindInt(args, TR_KEY_peer_port, &i))
        {
            printf("  Listenport: %" PRId64 "\n", i);
        }

        if (tr_variantDictFindBool(args, TR_KEY_port_forwarding_enabled, &boolVal))
        {
            printf("  Portforwarding enabled: %s\n", boolVal ? "Yes" : "No");
        }

        if (tr_variantDictFindBool(args, TR_KEY_utp_enabled, &boolVal))
        {
            printf("  uTP enabled: %s\n", (boolVal ? "Yes" : "No"));
        }

        if (tr_variantDictFindBool(args, TR_KEY_dht_enabled, &boolVal))
        {
            printf("  Distributed hash table enabled: %s\n", boolVal ? "Yes" : "No");
        }

        if (tr_variantDictFindBool(args, TR_KEY_lpd_enabled, &boolVal))
        {
            printf("  Local peer discovery enabled: %s\n", boolVal ? "Yes" : "No");
        }

        if (tr_variantDictFindBool(args, TR_KEY_pex_enabled, &boolVal))
        {
            printf("  Peer exchange allowed: %s\n", boolVal ? "Yes" : "No");
        }

        if (tr_variantDictFindStrView(args, TR_KEY_encryption, &sv))
        {
            printf("  Encryption: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
        }

        if (tr_variantDictFindInt(args, TR_KEY_cache_size_mb, &i))
        {
            printf("  Maximum memory cache size: %s\n", tr_formatter_mem_MB(buf, i, sizeof(buf)));
        }

        printf("\n");

        {
            bool altEnabled;
            bool altTimeEnabled;
            bool upEnabled;
            bool downEnabled;
            bool seedRatioLimited;
            int64_t altDown;
            int64_t altUp;
            int64_t altBegin;
            int64_t altEnd;
            int64_t altDay;
            int64_t upLimit;
            int64_t downLimit;
            int64_t peerLimit;
            double seedRatioLimit;

            if (tr_variantDictFindInt(args, TR_KEY_alt_speed_down, &altDown) &&
                tr_variantDictFindBool(args, TR_KEY_alt_speed_enabled, &altEnabled) &&
                tr_variantDictFindInt(args, TR_KEY_alt_speed_time_begin, &altBegin) &&
                tr_variantDictFindBool(args, TR_KEY_alt_speed_time_enabled, &altTimeEnabled) &&
                tr_variantDictFindInt(args, TR_KEY_alt_speed_time_end, &altEnd) &&
                tr_variantDictFindInt(args, TR_KEY_alt_speed_time_day, &altDay) &&
                tr_variantDictFindInt(args, TR_KEY_alt_speed_up, &altUp) &&
                tr_variantDictFindInt(args, TR_KEY_peer_limit_global, &peerLimit) &&
                tr_variantDictFindInt(args, TR_KEY_speed_limit_down, &downLimit) &&
                tr_variantDictFindBool(args, TR_KEY_speed_limit_down_enabled, &downEnabled) &&
                tr_variantDictFindInt(args, TR_KEY_speed_limit_up, &upLimit) &&
                tr_variantDictFindBool(args, TR_KEY_speed_limit_up_enabled, &upEnabled) &&
                tr_variantDictFindReal(args, TR_KEY_seedRatioLimit, &seedRatioLimit) &&
                tr_variantDictFindBool(args, TR_KEY_seedRatioLimited, &seedRatioLimited))
            {
                char buf2[128];
                char buf3[128];

                printf("LIMITS\n");
                printf("  Peer limit: %" PRId64 "\n", peerLimit);

                if (seedRatioLimited)
                {
                    strlratio2(buf, seedRatioLimit, sizeof(buf));
                }
                else
                {
                    tr_strlcpy(buf, "Unlimited", sizeof(buf));
                }

                printf("  Default seed ratio limit: %s\n", buf);

                if (altEnabled)
                {
                    tr_formatter_speed_KBps(buf, altUp, sizeof(buf));
                }
                else if (upEnabled)
                {
                    tr_formatter_speed_KBps(buf, upLimit, sizeof(buf));
                }
                else
                {
                    tr_strlcpy(buf, "Unlimited", sizeof(buf));
                }

                printf(
                    "  Upload speed limit: %s (%s limit: %s; %s turtle limit: %s)\n",
                    buf,
                    upEnabled ? "Enabled" : "Disabled",
                    tr_formatter_speed_KBps(buf2, upLimit, sizeof(buf2)),
                    altEnabled ? "Enabled" : "Disabled",
                    tr_formatter_speed_KBps(buf3, altUp, sizeof(buf3)));

                if (altEnabled)
                {
                    tr_formatter_speed_KBps(buf, altDown, sizeof(buf));
                }
                else if (downEnabled)
                {
                    tr_formatter_speed_KBps(buf, downLimit, sizeof(buf));
                }
                else
                {
                    tr_strlcpy(buf, "Unlimited", sizeof(buf));
                }

                printf(
                    "  Download speed limit: %s (%s limit: %s; %s turtle limit: %s)\n",
                    buf,
                    downEnabled ? "Enabled" : "Disabled",
                    tr_formatter_speed_KBps(buf2, downLimit, sizeof(buf2)),
                    altEnabled ? "Enabled" : "Disabled",
                    tr_formatter_speed_KBps(buf3, altDown, sizeof(buf3)));

                if (altTimeEnabled)
                {
                    printf(
                        "  Turtle schedule: %02d:%02d - %02d:%02d  ",
                        (int)(altBegin / 60),
                        (int)(altBegin % 60),
                        (int)(altEnd / 60),
                        (int)(altEnd % 60));

                    if ((altDay & TR_SCHED_SUN) != 0)
                    {
                        printf("Sun ");
                    }

                    if ((altDay & TR_SCHED_MON) != 0)
                    {
                        printf("Mon ");
                    }

                    if ((altDay & TR_SCHED_TUES) != 0)
                    {
                        printf("Tue ");
                    }

                    if ((altDay & TR_SCHED_WED) != 0)
                    {
                        printf("Wed ");
                    }

                    if ((altDay & TR_SCHED_THURS) != 0)
                    {
                        printf("Thu ");
                    }

                    if ((altDay & TR_SCHED_FRI) != 0)
                    {
                        printf("Fri ");
                    }

                    if ((altDay & TR_SCHED_SAT) != 0)
                    {
                        printf("Sat ");
                    }

                    printf("\n");
                }
            }
        }

        printf("\n");

        printf("MISC\n");

        if (tr_variantDictFindBool(args, TR_KEY_start_added_torrents, &boolVal))
        {
            printf("  Autostart added torrents: %s\n", boolVal ? "Yes" : "No");
        }

        if (tr_variantDictFindBool(args, TR_KEY_trash_original_torrent_files, &boolVal))
        {
            printf("  Delete automatically added torrents: %s\n", boolVal ? "Yes" : "No");
        }
    }
}

static void printSessionStats(tr_variant* top)
{
    tr_variant* args;
    tr_variant* d;

    if (tr_variantDictFindDict(top, TR_KEY_arguments, &args))
    {
        char buf[512];
        int64_t up;
        int64_t down;
        int64_t secs;
        int64_t sessions;

        if (tr_variantDictFindDict(args, TR_KEY_current_stats, &d) && tr_variantDictFindInt(d, TR_KEY_uploadedBytes, &up) &&
            tr_variantDictFindInt(d, TR_KEY_downloadedBytes, &down) && tr_variantDictFindInt(d, TR_KEY_secondsActive, &secs))
        {
            printf("\nCURRENT SESSION\n");
            printf("  Uploaded:   %s\n", strlsize(buf, up, sizeof(buf)));
            printf("  Downloaded: %s\n", strlsize(buf, down, sizeof(buf)));
            printf("  Ratio:      %s\n", strlratio(buf, up, down, sizeof(buf)));
            printf("  Duration:   %s\n", tr_strltime(buf, secs, sizeof(buf)));
        }

        if (tr_variantDictFindDict(args, TR_KEY_cumulative_stats, &d) &&
            tr_variantDictFindInt(d, TR_KEY_sessionCount, &sessions) && tr_variantDictFindInt(d, TR_KEY_uploadedBytes, &up) &&
            tr_variantDictFindInt(d, TR_KEY_downloadedBytes, &down) && tr_variantDictFindInt(d, TR_KEY_secondsActive, &secs))
        {
            printf("\nTOTAL\n");
            printf("  Started %lu times\n", (unsigned long)sessions);
            printf("  Uploaded:   %s\n", strlsize(buf, up, sizeof(buf)));
            printf("  Downloaded: %s\n", strlsize(buf, down, sizeof(buf)));
            printf("  Ratio:      %s\n", strlratio(buf, up, down, sizeof(buf)));
            printf("  Duration:   %s\n", tr_strltime(buf, secs, sizeof(buf)));
        }
    }
}

static char id[4096];

static int processResponse(char const* rpcurl, std::string_view response)
{
    tr_variant top;
    int status = EXIT_SUCCESS;

    if (debug)
    {
        fprintf(
            stderr,
            "got response (len %d):\n--------\n%" TR_PRIsv "\n--------\n",
            int(std::size(response)),
            TR_PRIsv_ARG(response));
    }

    if (!tr_variantFromBuf(&top, TR_VARIANT_PARSE_JSON | TR_VARIANT_PARSE_INPLACE, response))
    {
        tr_logAddNamedError(MY_NAME, "Unable to parse response \"%" TR_PRIsv "\"", TR_PRIsv_ARG(response));
        status |= EXIT_FAILURE;
    }
    else
    {
        int64_t tag = -1;
        auto sv = std::string_view{};

        if (tr_variantDictFindStrView(&top, TR_KEY_result, &sv))
        {
            if (sv != "success"sv)
            {
                printf("Error: %" TR_PRIsv "\n", TR_PRIsv_ARG(sv));
                status |= EXIT_FAILURE;
            }
            else
            {
                tr_variantDictFindInt(&top, TR_KEY_tag, &tag);

                switch (tag)
                {
                case TAG_SESSION:
                    printSession(&top);
                    break;

                case TAG_STATS:
                    printSessionStats(&top);
                    break;

                case TAG_DETAILS:
                    printDetails(&top);
                    break;

                case TAG_FILES:
                    printFileList(&top);
                    break;

                case TAG_LIST:
                    printTorrentList(&top);
                    break;

                case TAG_PEERS:
                    printPeers(&top);
                    break;

                case TAG_PIECES:
                    printPieces(&top);
                    break;

                case TAG_PORTTEST:
                    printPortTest(&top);
                    break;

                case TAG_TRACKERS:
                    printTrackers(&top);
                    break;

                case TAG_TORRENT_ADD:
                    {
                        int64_t i;
                        tr_variant* b = &top;

                        if (tr_variantDictFindDict(&top, ARGUMENTS, &b) &&
                            tr_variantDictFindDict(b, TR_KEY_torrent_added, &b) && tr_variantDictFindInt(b, TR_KEY_id, &i))
                        {
                            tr_snprintf(id, sizeof(id), "%" PRId64, i);
                        }
                        [[fallthrough]];
                    }

                default:
                    if (!tr_variantDictFindStrView(&top, TR_KEY_result, &sv))
                    {
                        status |= EXIT_FAILURE;
                    }
                    else
                    {
                        printf("%s responded: \"%" TR_PRIsv "\"\n", rpcurl, TR_PRIsv_ARG(sv));

                        if (sv != "success"sv)
                        {
                            status |= EXIT_FAILURE;
                        }
                    }
                }

                tr_variantFree(&top);
            }
        }
        else
        {
            status |= EXIT_FAILURE;
        }
    }

    return status;
}

static CURL* tr_curl_easy_init(struct evbuffer* writebuf)
{
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, MY_NAME "/" LONG_VERSION_STRING);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, writebuf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, parseResponseHeader);
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    curl_easy_setopt(curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, debug);
    curl_easy_setopt(curl, CURLOPT_ENCODING, ""); /* "" tells curl to fill in the blanks with what it was compiled to support */

    if (netrc != nullptr)
    {
        curl_easy_setopt(curl, CURLOPT_NETRC_FILE, netrc);
    }

    if (auth != nullptr)
    {
        curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
    }

    if (UseSSL)
    {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0); /* do not verify subject/hostname */
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0); /* since most certs will be self-signed, do not verify against CA */
    }

    if (sessionId != nullptr)
    {
        char* h = tr_strdup_printf("%s: %s", TR_RPC_SESSION_ID_HEADER, sessionId);
        struct curl_slist* custom_headers = curl_slist_append(nullptr, h);
        tr_free(h);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, custom_headers);
        curl_easy_setopt(curl, CURLOPT_PRIVATE, custom_headers);
    }

    return curl;
}

static void tr_curl_easy_cleanup(CURL* curl)
{
    struct curl_slist* custom_headers = nullptr;
    curl_easy_getinfo(curl, CURLINFO_PRIVATE, &custom_headers);

    curl_easy_cleanup(curl);

    if (custom_headers != nullptr)
    {
        curl_slist_free_all(custom_headers);
    }
}

static int flush(char const* rpcurl, tr_variant** benc)
{
    CURLcode res;
    CURL* curl;
    int status = EXIT_SUCCESS;
    struct evbuffer* buf = evbuffer_new();
    char* json = tr_variantToStr(*benc, TR_VARIANT_FMT_JSON_LEAN, nullptr);
    char* rpcurl_http = tr_strdup_printf(UseSSL ? "https://%s" : "http://%s", rpcurl);

    curl = tr_curl_easy_init(buf);
    curl_easy_setopt(curl, CURLOPT_URL, rpcurl_http);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, getTimeoutSecs(json));

    if (debug)
    {
        fprintf(stderr, "posting:\n--------\n%s\n--------\n", json);
    }

    if ((res = curl_easy_perform(curl)) != CURLE_OK)
    {
        tr_logAddNamedError(MY_NAME, " (%s) %s", rpcurl_http, curl_easy_strerror(res));
        status |= EXIT_FAILURE;
    }
    else
    {
        long response;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);

        switch (response)
        {
        case 200:
            status |= processResponse(
                rpcurl,
                std::string_view{ reinterpret_cast<char const*>(evbuffer_pullup(buf, -1)), evbuffer_get_length(buf) });
            break;

        case 409:
            /* Session id failed. Our curl header func has already
             * pulled the new session id from this response's headers,
             * build a new CURL* and try again */
            tr_curl_easy_cleanup(curl);
            curl = nullptr;
            status |= flush(rpcurl, benc);
            benc = nullptr;
            break;

        default:
            evbuffer_add(buf, "", 1);
            fprintf(stderr, "Unexpected response: %s\n", evbuffer_pullup(buf, -1));
            status |= EXIT_FAILURE;
            break;
        }
    }

    /* cleanup */
    tr_free(rpcurl_http);
    tr_free(json);
    evbuffer_free(buf);

    if (curl != nullptr)
    {
        tr_curl_easy_cleanup(curl);
    }

    if (benc != nullptr)
    {
        tr_variantFree(*benc);
        tr_free(*benc);
        *benc = nullptr;
    }

    return status;
}

static tr_variant* ensure_sset(tr_variant** sset)
{
    tr_variant* args;

    if (*sset != nullptr)
    {
        args = tr_variantDictFind(*sset, ARGUMENTS);
    }
    else
    {
        *sset = tr_new0(tr_variant, 1);
        tr_variantInitDict(*sset, 3);
        tr_variantDictAddStrView(*sset, TR_KEY_method, "session-set"sv);
        args = tr_variantDictAddDict(*sset, ARGUMENTS, 0);
    }

    return args;
}

static tr_variant* ensure_tset(tr_variant** tset)
{
    tr_variant* args;

    if (*tset != nullptr)
    {
        args = tr_variantDictFind(*tset, ARGUMENTS);
    }
    else
    {
        *tset = tr_new0(tr_variant, 1);
        tr_variantInitDict(*tset, 3);
        tr_variantDictAddStrView(*tset, TR_KEY_method, "torrent-set"sv);
        args = tr_variantDictAddDict(*tset, ARGUMENTS, 1);
    }

    return args;
}

static int processArgs(char const* rpcurl, int argc, char const* const* argv)
{
    int c;
    int status = EXIT_SUCCESS;
    char const* optarg;
    tr_variant* sset = nullptr;
    tr_variant* tset = nullptr;
    tr_variant* tadd = nullptr;

    *id = '\0';

    while ((c = tr_getopt(getUsage(), argc, argv, opts, &optarg)) != TR_OPT_DONE)
    {
        int const stepMode = getOptMode(c);

        if (stepMode == 0) /* meta commands */
        {
            switch (c)
            {
            case 'a': /* add torrent */
                if (sset != nullptr)
                {
                    status |= flush(rpcurl, &sset);
                }

                if (tadd != nullptr)
                {
                    status |= flush(rpcurl, &tadd);
                }

                if (tset != nullptr)
                {
                    addIdArg(tr_variantDictFind(tset, ARGUMENTS), id, nullptr);
                    status |= flush(rpcurl, &tset);
                }

                tadd = tr_new0(tr_variant, 1);
                tr_variantInitDict(tadd, 3);
                tr_variantDictAddStrView(tadd, TR_KEY_method, "torrent-add"sv);
                tr_variantDictAddInt(tadd, TR_KEY_tag, TAG_TORRENT_ADD);
                tr_variantDictAddDict(tadd, ARGUMENTS, 0);
                break;

            case 'b': /* debug */
                debug = true;
                break;

            case 'n': /* auth */
                auth = tr_strdup(optarg);
                break;

            case 810: /* authenv */
                auth = tr_env_get_string("TR_AUTH", nullptr);

                if (auth == nullptr)
                {
                    fprintf(stderr, "The TR_AUTH environment variable is not set\n");
                    exit(0);
                }

                break;

            case 'N': /* netrc */
                netrc = tr_strdup(optarg);
                break;

            case 820: /* UseSSL */
                UseSSL = true;
                break;

            case 't': /* set current torrent */
                if (tadd != nullptr)
                {
                    status |= flush(rpcurl, &tadd);
                }

                if (tset != nullptr)
                {
                    addIdArg(tr_variantDictFind(tset, ARGUMENTS), id, nullptr);
                    status |= flush(rpcurl, &tset);
                }

                tr_strlcpy(id, optarg, sizeof(id));
                break;

            case 'V': /* show version number */
                fprintf(stderr, "%s %s\n", MY_NAME, LONG_VERSION_STRING);
                exit(0);

            case TR_OPT_ERR:
                fprintf(stderr, "invalid option\n");
                showUsage();
                status |= EXIT_FAILURE;
                break;

            case TR_OPT_UNK:
                if (tadd != nullptr)
                {
                    tr_variant* args = tr_variantDictFind(tadd, ARGUMENTS);
                    char* tmp = getEncodedMetainfo(optarg);

                    if (tmp != nullptr)
                    {
                        tr_variantDictAddStr(args, TR_KEY_metainfo, tmp);
                    }
                    else
                    {
                        tr_variantDictAddStr(args, TR_KEY_filename, optarg);
                    }

                    tr_free(tmp);
                }
                else
                {
                    fprintf(stderr, "Unknown option: %s\n", optarg);
                    status |= EXIT_FAILURE;
                }

                break;
            }
        }
        else if (stepMode == MODE_TORRENT_GET)
        {
            tr_variant* top = tr_new0(tr_variant, 1);
            tr_variant* args;
            tr_variant* fields;
            tr_variantInitDict(top, 3);
            tr_variantDictAddStrView(top, TR_KEY_method, "torrent-get"sv);
            args = tr_variantDictAddDict(top, ARGUMENTS, 0);
            fields = tr_variantDictAddList(args, TR_KEY_fields, 0);

            if (tset != nullptr)
            {
                addIdArg(tr_variantDictFind(tset, ARGUMENTS), id, nullptr);
                status |= flush(rpcurl, &tset);
            }

            switch (c)
            {
            case 'i':
                tr_variantDictAddInt(top, TR_KEY_tag, TAG_DETAILS);

                for (size_t i = 0; i < TR_N_ELEMENTS(details_keys); ++i)
                {
                    tr_variantListAddQuark(fields, details_keys[i]);
                }

                addIdArg(args, id, nullptr);
                break;

            case 'l':
                tr_variantDictAddInt(top, TR_KEY_tag, TAG_LIST);

                for (size_t i = 0; i < TR_N_ELEMENTS(list_keys); ++i)
                {
                    tr_variantListAddQuark(fields, list_keys[i]);
                }

                addIdArg(args, id, "all");
                break;

            case 940:
                tr_variantDictAddInt(top, TR_KEY_tag, TAG_FILES);

                for (size_t i = 0; i < TR_N_ELEMENTS(files_keys); ++i)
                {
                    tr_variantListAddQuark(fields, files_keys[i]);
                }

                addIdArg(args, id, nullptr);
                break;

            case 941:
                tr_variantDictAddInt(top, TR_KEY_tag, TAG_PEERS);
                tr_variantListAddStrView(fields, "peers"sv);
                addIdArg(args, id, nullptr);
                break;

            case 942:
                tr_variantDictAddInt(top, TR_KEY_tag, TAG_PIECES);
                tr_variantListAddStrView(fields, "pieces"sv);
                tr_variantListAddStrView(fields, "pieceCount"sv);
                addIdArg(args, id, nullptr);
                break;

            case 943:
                tr_variantDictAddInt(top, TR_KEY_tag, TAG_TRACKERS);
                tr_variantListAddStrView(fields, "trackerStats"sv);
                addIdArg(args, id, nullptr);
                break;

            default:
                assert("unhandled value" && 0);
            }

            status |= flush(rpcurl, &top);
        }
        else if (stepMode == MODE_SESSION_SET)
        {
            tr_variant* args = ensure_sset(&sset);

            switch (c)
            {
            case 800:
                tr_variantDictAddStr(args, TR_KEY_script_torrent_done_filename, optarg);
                tr_variantDictAddBool(args, TR_KEY_script_torrent_done_enabled, true);
                break;

            case 801:
                tr_variantDictAddBool(args, TR_KEY_script_torrent_done_enabled, false);
                break;

            case 970:
                tr_variantDictAddBool(args, TR_KEY_alt_speed_enabled, true);
                break;

            case 971:
                tr_variantDictAddBool(args, TR_KEY_alt_speed_enabled, false);
                break;

            case 972:
                tr_variantDictAddInt(args, TR_KEY_alt_speed_down, numarg(optarg));
                break;

            case 973:
                tr_variantDictAddInt(args, TR_KEY_alt_speed_up, numarg(optarg));
                break;

            case 974:
                tr_variantDictAddBool(args, TR_KEY_alt_speed_time_enabled, true);
                break;

            case 975:
                tr_variantDictAddBool(args, TR_KEY_alt_speed_time_enabled, false);
                break;

            case 976:
                addTime(args, TR_KEY_alt_speed_time_begin, optarg);
                break;

            case 977:
                addTime(args, TR_KEY_alt_speed_time_end, optarg);
                break;

            case 978:
                addDays(args, TR_KEY_alt_speed_time_day, optarg);
                break;

            case 'c':
                tr_variantDictAddStr(args, TR_KEY_incomplete_dir, optarg);
                tr_variantDictAddBool(args, TR_KEY_incomplete_dir_enabled, true);
                break;

            case 'C':
                tr_variantDictAddBool(args, TR_KEY_incomplete_dir_enabled, false);
                break;

            case 'e':
                tr_variantDictAddInt(args, TR_KEY_cache_size_mb, atoi(optarg));
                break;

            case 910:
                tr_variantDictAddStrView(args, TR_KEY_encryption, "required"sv);
                break;

            case 911:
                tr_variantDictAddStrView(args, TR_KEY_encryption, "preferred"sv);
                break;

            case 912:
                tr_variantDictAddStrView(args, TR_KEY_encryption, "tolerated"sv);
                break;

            case 'm':
                tr_variantDictAddBool(args, TR_KEY_port_forwarding_enabled, true);
                break;

            case 'M':
                tr_variantDictAddBool(args, TR_KEY_port_forwarding_enabled, false);
                break;

            case 'o':
                tr_variantDictAddBool(args, TR_KEY_dht_enabled, true);
                break;

            case 'O':
                tr_variantDictAddBool(args, TR_KEY_dht_enabled, false);
                break;

            case 830:
                tr_variantDictAddBool(args, TR_KEY_utp_enabled, true);
                break;

            case 831:
                tr_variantDictAddBool(args, TR_KEY_utp_enabled, false);
                break;

            case 'p':
                tr_variantDictAddInt(args, TR_KEY_peer_port, numarg(optarg));
                break;

            case 'P':
                tr_variantDictAddBool(args, TR_KEY_peer_port_random_on_start, true);
                break;

            case 'x':
                tr_variantDictAddBool(args, TR_KEY_pex_enabled, true);
                break;

            case 'X':
                tr_variantDictAddBool(args, TR_KEY_pex_enabled, false);
                break;

            case 'y':
                tr_variantDictAddBool(args, TR_KEY_lpd_enabled, true);
                break;

            case 'Y':
                tr_variantDictAddBool(args, TR_KEY_lpd_enabled, false);
                break;

            case 953:
                tr_variantDictAddReal(args, TR_KEY_seedRatioLimit, atof(optarg));
                tr_variantDictAddBool(args, TR_KEY_seedRatioLimited, true);
                break;

            case 954:
                tr_variantDictAddBool(args, TR_KEY_seedRatioLimited, false);
                break;

            case 990:
                tr_variantDictAddBool(args, TR_KEY_start_added_torrents, false);
                break;

            case 991:
                tr_variantDictAddBool(args, TR_KEY_start_added_torrents, true);
                break;

            case 992:
                tr_variantDictAddBool(args, TR_KEY_trash_original_torrent_files, true);
                break;

            case 993:
                tr_variantDictAddBool(args, TR_KEY_trash_original_torrent_files, false);
                break;

            default:
                assert("unhandled value" && 0);
                break;
            }
        }
        else if (stepMode == (MODE_SESSION_SET | MODE_TORRENT_SET))
        {
            tr_variant* targs = nullptr;
            tr_variant* sargs = nullptr;

            if (!tr_str_is_empty(id))
            {
                targs = ensure_tset(&tset);
            }
            else
            {
                sargs = ensure_sset(&sset);
            }

            switch (c)
            {
            case 'd':
                if (targs != nullptr)
                {
                    tr_variantDictAddInt(targs, TR_KEY_downloadLimit, numarg(optarg));
                    tr_variantDictAddBool(targs, TR_KEY_downloadLimited, true);
                }
                else
                {
                    tr_variantDictAddInt(sargs, TR_KEY_speed_limit_down, numarg(optarg));
                    tr_variantDictAddBool(sargs, TR_KEY_speed_limit_down_enabled, true);
                }

                break;

            case 'D':
                if (targs != nullptr)
                {
                    tr_variantDictAddBool(targs, TR_KEY_downloadLimited, false);
                }
                else
                {
                    tr_variantDictAddBool(sargs, TR_KEY_speed_limit_down_enabled, false);
                }

                break;

            case 'u':
                if (targs != nullptr)
                {
                    tr_variantDictAddInt(targs, TR_KEY_uploadLimit, numarg(optarg));
                    tr_variantDictAddBool(targs, TR_KEY_uploadLimited, true);
                }
                else
                {
                    tr_variantDictAddInt(sargs, TR_KEY_speed_limit_up, numarg(optarg));
                    tr_variantDictAddBool(sargs, TR_KEY_speed_limit_up_enabled, true);
                }

                break;

            case 'U':
                if (targs != nullptr)
                {
                    tr_variantDictAddBool(targs, TR_KEY_uploadLimited, false);
                }
                else
                {
                    tr_variantDictAddBool(sargs, TR_KEY_speed_limit_up_enabled, false);
                }

                break;

            case 930:
                if (targs != nullptr)
                {
                    tr_variantDictAddInt(targs, TR_KEY_peer_limit, atoi(optarg));
                }
                else
                {
                    tr_variantDictAddInt(sargs, TR_KEY_peer_limit_global, atoi(optarg));
                }

                break;

            default:
                assert("unhandled value" && 0);
                break;
            }
        }
        else if (stepMode == MODE_TORRENT_SET)
        {
            tr_variant* args = ensure_tset(&tset);

            switch (c)
            {
            case 'L':
                addLabels(args, optarg ? optarg : "");
                break;

            case 712:
                tr_variantListAddInt(tr_variantDictAddList(args, TR_KEY_trackerRemove, 1), atoi(optarg));
                break;

            case 950:
                tr_variantDictAddReal(args, TR_KEY_seedRatioLimit, atof(optarg));
                tr_variantDictAddInt(args, TR_KEY_seedRatioMode, TR_RATIOLIMIT_SINGLE);
                break;

            case 951:
                tr_variantDictAddInt(args, TR_KEY_seedRatioMode, TR_RATIOLIMIT_GLOBAL);
                break;

            case 952:
                tr_variantDictAddInt(args, TR_KEY_seedRatioMode, TR_RATIOLIMIT_UNLIMITED);
                break;

            case 984:
                tr_variantDictAddBool(args, TR_KEY_honorsSessionLimits, true);
                break;

            case 985:
                tr_variantDictAddBool(args, TR_KEY_honorsSessionLimits, false);
                break;

            default:
                assert("unhandled value" && 0);
                break;
            }
        }
        else if (stepMode == (MODE_TORRENT_SET | MODE_TORRENT_ADD))
        {
            tr_variant* args;

            if (tadd != nullptr)
            {
                args = tr_variantDictFind(tadd, ARGUMENTS);
            }
            else
            {
                args = ensure_tset(&tset);
            }

            switch (c)
            {
            case 'g':
                addFiles(args, TR_KEY_files_wanted, optarg);
                break;

            case 'G':
                addFiles(args, TR_KEY_files_unwanted, optarg);
                break;

            case 900:
                addFiles(args, TR_KEY_priority_high, optarg);
                break;

            case 901:
                addFiles(args, TR_KEY_priority_normal, optarg);
                break;

            case 902:
                addFiles(args, TR_KEY_priority_low, optarg);
                break;

            case 700:
                tr_variantDictAddInt(args, TR_KEY_bandwidthPriority, 1);
                break;

            case 701:
                tr_variantDictAddInt(args, TR_KEY_bandwidthPriority, 0);
                break;

            case 702:
                tr_variantDictAddInt(args, TR_KEY_bandwidthPriority, -1);
                break;

            case 710:
                tr_variantListAddStr(tr_variantDictAddList(args, TR_KEY_trackerAdd, 1), optarg);
                break;

            default:
                assert("unhandled value" && 0);
                break;
            }
        }
        else if (c == 961) /* set location */
        {
            if (tadd != nullptr)
            {
                tr_variant* args = tr_variantDictFind(tadd, ARGUMENTS);
                tr_variantDictAddStr(args, TR_KEY_download_dir, optarg);
            }
            else
            {
                tr_variant* args;
                tr_variant* top = tr_new0(tr_variant, 1);
                tr_variantInitDict(top, 2);
                tr_variantDictAddStrView(top, TR_KEY_method, "torrent-set-location"sv);
                args = tr_variantDictAddDict(top, ARGUMENTS, 3);
                tr_variantDictAddStr(args, TR_KEY_location, optarg);
                tr_variantDictAddBool(args, TR_KEY_move, false);
                addIdArg(args, id, nullptr);
                status |= flush(rpcurl, &top);
                break;
            }
        }
        else
        {
            switch (c)
            {
            case 920: /* session-info */
                {
                    tr_variant* top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 2);
                    tr_variantDictAddStrView(top, TR_KEY_method, "session-get"sv);
                    tr_variantDictAddInt(top, TR_KEY_tag, TAG_SESSION);
                    status |= flush(rpcurl, &top);
                    break;
                }

            case 's': /* start */
                {
                    if (tadd != nullptr)
                    {
                        tr_variantDictAddBool(tr_variantDictFind(tadd, TR_KEY_arguments), TR_KEY_paused, false);
                    }
                    else
                    {
                        tr_variant* top = tr_new0(tr_variant, 1);
                        tr_variantInitDict(top, 2);
                        tr_variantDictAddStrView(top, TR_KEY_method, "torrent-start"sv);
                        addIdArg(tr_variantDictAddDict(top, ARGUMENTS, 1), id, nullptr);
                        status |= flush(rpcurl, &top);
                    }

                    break;
                }

            case 'S': /* stop */
                {
                    if (tadd != nullptr)
                    {
                        tr_variantDictAddBool(tr_variantDictFind(tadd, TR_KEY_arguments), TR_KEY_paused, true);
                    }
                    else
                    {
                        tr_variant* top = tr_new0(tr_variant, 1);
                        tr_variantInitDict(top, 2);
                        tr_variantDictAddStrView(top, TR_KEY_method, "torrent-stop"sv);
                        addIdArg(tr_variantDictAddDict(top, ARGUMENTS, 1), id, nullptr);
                        status |= flush(rpcurl, &top);
                    }

                    break;
                }

            case 'w':
                {
                    tr_variant* args = tadd != nullptr ? tr_variantDictFind(tadd, TR_KEY_arguments) : ensure_sset(&sset);
                    tr_variantDictAddStr(args, TR_KEY_download_dir, optarg);
                    break;
                }

            case 850:
                {
                    tr_variant* top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 1);
                    tr_variantDictAddStrView(top, TR_KEY_method, "session-close"sv);
                    status |= flush(rpcurl, &top);
                    break;
                }

            case 963:
                {
                    tr_variant* top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 1);
                    tr_variantDictAddStrView(top, TR_KEY_method, "blocklist-update"sv);
                    status |= flush(rpcurl, &top);
                    break;
                }

            case 921:
                {
                    tr_variant* top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 2);
                    tr_variantDictAddStrView(top, TR_KEY_method, "session-stats"sv);
                    tr_variantDictAddInt(top, TR_KEY_tag, TAG_STATS);
                    status |= flush(rpcurl, &top);
                    break;
                }

            case 962:
                {
                    tr_variant* top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 2);
                    tr_variantDictAddStrView(top, TR_KEY_method, "port-test"sv);
                    tr_variantDictAddInt(top, TR_KEY_tag, TAG_PORTTEST);
                    status |= flush(rpcurl, &top);
                    break;
                }

            case 600:
                {
                    tr_variant* top;

                    if (tset != nullptr)
                    {
                        addIdArg(tr_variantDictFind(tset, ARGUMENTS), id, nullptr);
                        status |= flush(rpcurl, &tset);
                    }

                    top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 2);
                    tr_variantDictAddStrView(top, TR_KEY_method, "torrent-reannounce"sv);
                    addIdArg(tr_variantDictAddDict(top, ARGUMENTS, 1), id, nullptr);
                    status |= flush(rpcurl, &top);
                    break;
                }

            case 'v':
                {
                    tr_variant* top;

                    if (tset != nullptr)
                    {
                        addIdArg(tr_variantDictFind(tset, ARGUMENTS), id, nullptr);
                        status |= flush(rpcurl, &tset);
                    }

                    top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 2);
                    tr_variantDictAddStrView(top, TR_KEY_method, "torrent-verify"sv);
                    addIdArg(tr_variantDictAddDict(top, ARGUMENTS, 1), id, nullptr);
                    status |= flush(rpcurl, &top);
                    break;
                }

            case 'r':
            case 840:
                {
                    tr_variant* args;
                    tr_variant* top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 2);
                    tr_variantDictAddStrView(top, TR_KEY_method, "torrent-remove"sv);
                    args = tr_variantDictAddDict(top, ARGUMENTS, 2);
                    tr_variantDictAddBool(args, TR_KEY_delete_local_data, c == 840);
                    addIdArg(args, id, nullptr);
                    status |= flush(rpcurl, &top);
                    break;
                }

            case 960:
                {
                    tr_variant* args;
                    tr_variant* top = tr_new0(tr_variant, 1);
                    tr_variantInitDict(top, 2);
                    tr_variantDictAddStrView(top, TR_KEY_method, "torrent-set-location"sv);
                    args = tr_variantDictAddDict(top, ARGUMENTS, 3);
                    tr_variantDictAddStr(args, TR_KEY_location, optarg);
                    tr_variantDictAddBool(args, TR_KEY_move, true);
                    addIdArg(args, id, nullptr);
                    status |= flush(rpcurl, &top);
                    break;
                }

            default:
                {
                    fprintf(stderr, "got opt [%d]\n", c);
                    showUsage();
                    break;
                }
            }
        }
    }

    if (tadd != nullptr)
    {
        status |= flush(rpcurl, &tadd);
    }

    if (tset != nullptr)
    {
        addIdArg(tr_variantDictFind(tset, ARGUMENTS), id, nullptr);
        status |= flush(rpcurl, &tset);
    }

    if (sset != nullptr)
    {
        status |= flush(rpcurl, &sset);
    }

    return status;
}

static bool parsePortString(char const* s, int* port)
{
    int const errno_stack = errno;
    errno = 0;

    char* end = nullptr;
    int const i = (int)strtol(s, &end, 10);
    bool const ok = (end != nullptr) && (*end == '\0') && (errno == 0);
    if (ok)
    {
        *port = i;
    }

    errno = errno_stack;
    return ok;
}

/* [host:port] or [host] or [port] or [http(s?)://host:port/transmission/] */
static void getHostAndPortAndRpcUrl(int* argc, char** argv, char** host, int* port, char** rpcurl)
{
    if (*argv[1] == '-')
    {
        return;
    }

    char const* const s = argv[1];
    char const* const last_colon = strrchr(s, ':');

    if (strncmp(s, "http://", 7) == 0) /* user passed in http rpc url */
    {
        *rpcurl = tr_strdup_printf("%s/rpc/", s + 7);
    }
    else if (strncmp(s, "https://", 8) == 0) /* user passed in https rpc url */
    {
        UseSSL = true;
        *rpcurl = tr_strdup_printf("%s/rpc/", s + 8);
    }
    else if (parsePortString(s, port))
    {
        // it was just a port
    }
    else if (last_colon == nullptr)
    {
        // it was a non-ipv6 host with no port
        *host = tr_strdup(s);
    }
    else
    {
        char const* hend;

        // if only one colon, it's probably "$host:$port"
        if ((strchr(s, ':') == last_colon) && parsePortString(last_colon + 1, port))
        {
            hend = last_colon;
        }
        else
        {
            hend = s + strlen(s);
        }

        bool const is_unbracketed_ipv6 = (*s != '[') && (memchr(s, ':', hend - s) != nullptr);

        *host = is_unbracketed_ipv6 ? tr_strdup_printf("[%*s]", TR_ARG_TUPLE((int)(hend - s), s)) : tr_strndup(s, hend - s);
    }

    *argc -= 1;

    for (int i = 1; i < *argc; ++i)
    {
        argv[i] = argv[i + 1];
    }
}

int tr_main(int argc, char* argv[])
{
    int port = DEFAULT_PORT;
    char* host = nullptr;
    char* rpcurl = nullptr;
    int exit_status = EXIT_SUCCESS;

    if (argc < 2)
    {
        showUsage();
        return EXIT_FAILURE;
    }

    tr_formatter_mem_init(MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
    tr_formatter_size_init(DISK_K, DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
    tr_formatter_speed_init(SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);

    getHostAndPortAndRpcUrl(&argc, argv, &host, &port, &rpcurl);

    if (host == nullptr)
    {
        host = tr_strdup(DEFAULT_HOST);
    }

    if (rpcurl == nullptr)
    {
        rpcurl = tr_strdup_printf("%s:%d%s", host, port, DEFAULT_URL);
    }

    exit_status = processArgs(rpcurl, argc, (char const* const*)argv);

    tr_free(host);
    tr_free(rpcurl);
    return exit_status;
}
