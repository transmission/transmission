/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <stdio.h> /* printf */
#include <stdlib.h> /* atoi */

#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif

#ifdef _WIN32
#include <process.h> /* getpid */
#else
#include <unistd.h> /* getpid */
#endif

#include <event2/event.h>

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/log.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>
#include <libtransmission/watchdir.h>

#ifdef USE_SYSTEMD

#include <systemd/sd-daemon.h>

#else

static void sd_notify(int status UNUSED, char const* str UNUSED)
{
}

static void sd_notifyf(int status UNUSED, char const* fmt UNUSED, ...)
{
}

#endif

#include "daemon.h"

#define MY_NAME "transmission-daemon"

#define MEM_K 1024
#define MEM_K_STR "KiB"
#define MEM_M_STR "MiB"
#define MEM_G_STR "GiB"
#define MEM_T_STR "TiB"

#define DISK_K 1000
#define DISK_B_STR "B"
#define DISK_K_STR "kB"
#define DISK_M_STR "MB"
#define DISK_G_STR "GB"
#define DISK_T_STR "TB"

#define SPEED_K 1000
#define SPEED_B_STR "B/s"
#define SPEED_K_STR "kB/s"
#define SPEED_M_STR "MB/s"
#define SPEED_G_STR "GB/s"
#define SPEED_T_STR "TB/s"

static bool seenHUP = false;
static char const* logfileName = NULL;
static tr_sys_file_t logfile = TR_BAD_SYS_FILE;
static tr_session* mySession = NULL;
static tr_quark key_pidfile = 0;
static tr_quark key_watch_dir_force_generic = 0;
static struct event_base* ev_base = NULL;

/***
****  Config File
***/

static char const* getUsage(void)
{
    return "Transmission " LONG_VERSION_STRING "  https://transmissionbt.com/\n"
        "A fast and easy BitTorrent client\n"
        "\n"
        MY_NAME " is a headless Transmission session\n"
        "that can be controlled via transmission-remote\n"
        "or the web interface.\n"
        "\n"
        "Usage: " MY_NAME " [options]";
}

static struct tr_option const options[] =
{
    { 'a', "allowed", "Allowed IP addresses. (Default: " TR_DEFAULT_RPC_WHITELIST ")", "a", true, "<list>" },
    { 'b', "blocklist", "Enable peer blocklists", "b", false, NULL },
    { 'B', "no-blocklist", "Disable peer blocklists", "B", false, NULL },
    { 'c', "watch-dir", "Where to watch for new .torrent files", "c", true, "<directory>" },
    { 'C', "no-watch-dir", "Disable the watch-dir", "C", false, NULL },
    { 941, "incomplete-dir", "Where to store new torrents until they're complete", NULL, true, "<directory>" },
    { 942, "no-incomplete-dir", "Don't store incomplete torrents in a different location", NULL, false, NULL },
    { 'd', "dump-settings", "Dump the settings and exit", "d", false, NULL },
    { 'e', "logfile", "Dump the log messages to this filename", "e", true, "<filename>" },
    { 'f', "foreground", "Run in the foreground instead of daemonizing", "f", false, NULL },
    { 'g', "config-dir", "Where to look for configuration files", "g", true, "<path>" },
    { 'p', "port", "RPC port (Default: " TR_DEFAULT_RPC_PORT_STR ")", "p", true, "<port>" },
    { 't', "auth", "Require authentication", "t", false, NULL },
    { 'T', "no-auth", "Don't require authentication", "T", false, NULL },
    { 'u', "username", "Set username for authentication", "u", true, "<username>" },
    { 'v', "password", "Set password for authentication", "v", true, "<password>" },
    { 'V', "version", "Show version number and exit", "V", false, NULL },
    { 810, "log-error", "Show error messages", NULL, false, NULL },
    { 811, "log-info", "Show error and info messages", NULL, false, NULL },
    { 812, "log-debug", "Show error, info, and debug messages", NULL, false, NULL },
    { 'w', "download-dir", "Where to save downloaded data", "w", true, "<path>" },
    { 800, "paused", "Pause all torrents on startup", NULL, false, NULL },
    { 'o', "dht", "Enable distributed hash tables (DHT)", "o", false, NULL },
    { 'O', "no-dht", "Disable distributed hash tables (DHT)", "O", false, NULL },
    { 'y', "lpd", "Enable local peer discovery (LPD)", "y", false, NULL },
    { 'Y', "no-lpd", "Disable local peer discovery (LPD)", "Y", false, NULL },
    { 830, "utp", "Enable uTP for peer connections", NULL, false, NULL },
    { 831, "no-utp", "Disable uTP for peer connections", NULL, false, NULL },
    { 'P', "peerport", "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "P", true, "<port>" },
    { 'm', "portmap", "Enable portmapping via NAT-PMP or UPnP", "m", false, NULL },
    { 'M', "no-portmap", "Disable portmapping", "M", false, NULL },
    { 'L', "peerlimit-global", "Maximum overall number of peers (Default: " TR_DEFAULT_PEER_LIMIT_GLOBAL_STR ")", "L", true,
        "<limit>" },
    { 'l', "peerlimit-torrent", "Maximum number of peers per torrent (Default: " TR_DEFAULT_PEER_LIMIT_TORRENT_STR ")", "l",
        true, "<limit>" },
    { 910, "encryption-required", "Encrypt all peer connections", "er", false, NULL },
    { 911, "encryption-preferred", "Prefer encrypted peer connections", "ep", false, NULL },
    { 912, "encryption-tolerated", "Prefer unencrypted peer connections", "et", false, NULL },
    { 'i', "bind-address-ipv4", "Where to listen for peer connections", "i", true, "<ipv4 addr>" },
    { 'I', "bind-address-ipv6", "Where to listen for peer connections", "I", true, "<ipv6 addr>" },
    { 'r', "rpc-bind-address", "Where to listen for RPC connections", "r", true, "<ip addr>" },
    { 953, "global-seedratio", "All torrents, unless overridden by a per-torrent setting, should seed until a specific ratio",
        "gsr", true, "ratio" },
    { 954, "no-global-seedratio", "All torrents, unless overridden by a per-torrent setting, should seed regardless of ratio",
        "GSR", false, NULL },
    { 'x', "pid-file", "Enable PID file", "x", true, "<pid-file>" },
    { 0, NULL, NULL, NULL, false, NULL }
};

static bool reopen_log_file(char const* filename)
{
    tr_error* error = NULL;
    tr_sys_file_t const old_log_file = logfile;
    tr_sys_file_t const new_log_file = tr_sys_file_open(filename, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_APPEND,
        0666, &error);

    if (new_log_file == TR_BAD_SYS_FILE)
    {
        fprintf(stderr, "Couldn't (re)open log file \"%s\": %s\n", filename, error->message);
        tr_error_free(error);
        return false;
    }

    logfile = new_log_file;

    if (old_log_file != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(old_log_file, NULL);
    }

    return true;
}

static char const* getConfigDir(int argc, char const* const* argv)
{
    int c;
    char const* configDir = NULL;
    char const* optarg;
    int const ind = tr_optind;

    while ((c = tr_getopt(getUsage(), argc, argv, options, &optarg)) != TR_OPT_DONE)
    {
        if (c == 'g')
        {
            configDir = optarg;
            break;
        }
    }

    tr_optind = ind;

    if (configDir == NULL)
    {
        configDir = tr_getDefaultConfigDir(MY_NAME);
    }

    return configDir;
}

static tr_watchdir_status onFileAdded(tr_watchdir_t dir, char const* name, void* context)
{
    tr_session* session = context;

    if (!tr_str_has_suffix(name, ".torrent"))
    {
        return TR_WATCHDIR_IGNORE;
    }

    char* filename = tr_buildPath(tr_watchdir_get_path(dir), name, NULL);
    tr_ctor* ctor = tr_ctorNew(session);
    int err = tr_ctorSetMetainfoFromFile(ctor, filename);

    if (err == 0)
    {
        tr_torrentNew(ctor, &err, NULL);

        if (err == TR_PARSE_ERR)
        {
            tr_logAddError("Error parsing .torrent file \"%s\"", name);
        }
        else
        {
            bool trash = false;
            bool const test = tr_ctorGetDeleteSource(ctor, &trash);

            tr_logAddInfo("Parsing .torrent file successful \"%s\"", name);

            if (test && trash)
            {
                tr_error* error = NULL;

                tr_logAddInfo("Deleting input .torrent file \"%s\"", name);

                if (!tr_sys_path_remove(filename, &error))
                {
                    tr_logAddError("Error deleting .torrent file: %s", error->message);
                    tr_error_free(error);
                }
            }
            else
            {
                char* new_filename = tr_strdup_printf("%s.added", filename);
                tr_sys_path_rename(filename, new_filename, NULL);
                tr_free(new_filename);
            }
        }
    }
    else
    {
        err = TR_PARSE_ERR;
    }

    tr_ctorFree(ctor);
    tr_free(filename);

    return err == TR_PARSE_ERR ? TR_WATCHDIR_RETRY : TR_WATCHDIR_ACCEPT;
}

static void printMessage(tr_sys_file_t logfile, int level, char const* name, char const* message, char const* file, int line)
{
    if (logfile != TR_BAD_SYS_FILE)
    {
        char timestr[64];
        tr_logGetTimeStr(timestr, sizeof(timestr));

        if (name != NULL)
        {
            tr_sys_file_write_fmt(logfile, "[%s] %s %s (%s:%d)" TR_NATIVE_EOL_STR, NULL, timestr, name, message, file, line);
        }
        else
        {
            tr_sys_file_write_fmt(logfile, "[%s] %s (%s:%d)" TR_NATIVE_EOL_STR, NULL, timestr, message, file, line);
        }
    }

#ifdef HAVE_SYSLOG

    else /* daemon... write to syslog */
    {
        int priority;

        /* figure out the syslog priority */
        switch (level)
        {
        case TR_LOG_ERROR:
            priority = LOG_ERR;
            break;

        case TR_LOG_DEBUG:
            priority = LOG_DEBUG;
            break;

        default:
            priority = LOG_INFO;
            break;
        }

        if (name != NULL)
        {
            syslog(priority, "%s %s (%s:%d)", name, message, file, line);
        }
        else
        {
            syslog(priority, "%s (%s:%d)", message, file, line);
        }
    }

#else

    (void)level;

#endif
}

static void pumpLogMessages(tr_sys_file_t logfile)
{
    tr_log_message* list = tr_logGetQueue();

    for (tr_log_message const* l = list; l != NULL; l = l->next)
    {
        printMessage(logfile, l->level, l->name, l->message, l->file, l->line);
    }

    if (logfile != TR_BAD_SYS_FILE)
    {
        tr_sys_file_flush(logfile, NULL);
    }

    tr_logFreeQueue(list);
}

static void reportStatus(void)
{
    double const up = tr_sessionGetRawSpeed_KBps(mySession, TR_UP);
    double const dn = tr_sessionGetRawSpeed_KBps(mySession, TR_DOWN);

    if (up > 0 || dn > 0)
    {
        sd_notifyf(0, "STATUS=Uploading %.2f KBps, Downloading %.2f KBps.\n", up, dn);
    }
    else
    {
        sd_notify(0, "STATUS=Idle.\n");
    }
}

static void periodicUpdate(evutil_socket_t fd UNUSED, short what UNUSED, void* context UNUSED)
{
    pumpLogMessages(logfile);
    reportStatus();
}

static tr_rpc_callback_status on_rpc_callback(tr_session* session UNUSED, tr_rpc_callback_type type,
    struct tr_torrent* tor UNUSED, void* user_data UNUSED)
{
    if (type == TR_RPC_SESSION_CLOSE)
    {
        event_base_loopexit(ev_base, NULL);
    }

    return TR_RPC_OK;
}

static bool parse_args(int argc, char const** argv, tr_variant* settings, bool* paused, bool* dump_settings, bool* foreground,
    int* exit_code)
{
    int c;
    char const* optarg;

    *paused = false;
    *dump_settings = false;
    *foreground = false;

    tr_optind = 1;

    while ((c = tr_getopt(getUsage(), argc, argv, options, &optarg)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'a':
            tr_variantDictAddStr(settings, TR_KEY_rpc_whitelist, optarg);
            tr_variantDictAddBool(settings, TR_KEY_rpc_whitelist_enabled, true);
            break;

        case 'b':
            tr_variantDictAddBool(settings, TR_KEY_blocklist_enabled, true);
            break;

        case 'B':
            tr_variantDictAddBool(settings, TR_KEY_blocklist_enabled, false);
            break;

        case 'c':
            tr_variantDictAddStr(settings, TR_KEY_watch_dir, optarg);
            tr_variantDictAddBool(settings, TR_KEY_watch_dir_enabled, true);
            break;

        case 'C':
            tr_variantDictAddBool(settings, TR_KEY_watch_dir_enabled, false);
            break;

        case 941:
            tr_variantDictAddStr(settings, TR_KEY_incomplete_dir, optarg);
            tr_variantDictAddBool(settings, TR_KEY_incomplete_dir_enabled, true);
            break;

        case 942:
            tr_variantDictAddBool(settings, TR_KEY_incomplete_dir_enabled, false);
            break;

        case 'd':
            *dump_settings = true;
            break;

        case 'e':
            if (reopen_log_file(optarg))
            {
                logfileName = optarg;
            }

            break;

        case 'f':
            *foreground = true;
            break;

        case 'g': /* handled above */
            break;

        case 'V': /* version */
            fprintf(stderr, "%s %s\n", MY_NAME, LONG_VERSION_STRING);
            *exit_code = 0;
            return false;

        case 'o':
            tr_variantDictAddBool(settings, TR_KEY_dht_enabled, true);
            break;

        case 'O':
            tr_variantDictAddBool(settings, TR_KEY_dht_enabled, false);
            break;

        case 'p':
            tr_variantDictAddInt(settings, TR_KEY_rpc_port, atoi(optarg));
            break;

        case 't':
            tr_variantDictAddBool(settings, TR_KEY_rpc_authentication_required, true);
            break;

        case 'T':
            tr_variantDictAddBool(settings, TR_KEY_rpc_authentication_required, false);
            break;

        case 'u':
            tr_variantDictAddStr(settings, TR_KEY_rpc_username, optarg);
            break;

        case 'v':
            tr_variantDictAddStr(settings, TR_KEY_rpc_password, optarg);
            break;

        case 'w':
            tr_variantDictAddStr(settings, TR_KEY_download_dir, optarg);
            break;

        case 'P':
            tr_variantDictAddInt(settings, TR_KEY_peer_port, atoi(optarg));
            break;

        case 'm':
            tr_variantDictAddBool(settings, TR_KEY_port_forwarding_enabled, true);
            break;

        case 'M':
            tr_variantDictAddBool(settings, TR_KEY_port_forwarding_enabled, false);
            break;

        case 'L':
            tr_variantDictAddInt(settings, TR_KEY_peer_limit_global, atoi(optarg));
            break;

        case 'l':
            tr_variantDictAddInt(settings, TR_KEY_peer_limit_per_torrent, atoi(optarg));
            break;

        case 800:
            *paused = true;
            break;

        case 910:
            tr_variantDictAddInt(settings, TR_KEY_encryption, TR_ENCRYPTION_REQUIRED);
            break;

        case 911:
            tr_variantDictAddInt(settings, TR_KEY_encryption, TR_ENCRYPTION_PREFERRED);
            break;

        case 912:
            tr_variantDictAddInt(settings, TR_KEY_encryption, TR_CLEAR_PREFERRED);
            break;

        case 'i':
            tr_variantDictAddStr(settings, TR_KEY_bind_address_ipv4, optarg);
            break;

        case 'I':
            tr_variantDictAddStr(settings, TR_KEY_bind_address_ipv6, optarg);
            break;

        case 'r':
            tr_variantDictAddStr(settings, TR_KEY_rpc_bind_address, optarg);
            break;

        case 953:
            tr_variantDictAddReal(settings, TR_KEY_ratio_limit, atof(optarg));
            tr_variantDictAddBool(settings, TR_KEY_ratio_limit_enabled, true);
            break;

        case 954:
            tr_variantDictAddBool(settings, TR_KEY_ratio_limit_enabled, false);
            break;

        case 'x':
            tr_variantDictAddStr(settings, key_pidfile, optarg);
            break;

        case 'y':
            tr_variantDictAddBool(settings, TR_KEY_lpd_enabled, true);
            break;

        case 'Y':
            tr_variantDictAddBool(settings, TR_KEY_lpd_enabled, false);
            break;

        case 810:
            tr_variantDictAddInt(settings, TR_KEY_message_level, TR_LOG_ERROR);
            break;

        case 811:
            tr_variantDictAddInt(settings, TR_KEY_message_level, TR_LOG_INFO);
            break;

        case 812:
            tr_variantDictAddInt(settings, TR_KEY_message_level, TR_LOG_DEBUG);
            break;

        case 830:
            tr_variantDictAddBool(settings, TR_KEY_utp_enabled, true);
            break;

        case 831:
            tr_variantDictAddBool(settings, TR_KEY_utp_enabled, false);
            break;

        default:
            tr_getopt_usage(MY_NAME, getUsage(), options);
            *exit_code = 0;
            return false;
        }
    }

    return true;
}

struct daemon_data
{
    tr_variant settings;
    char const* configDir;
    bool paused;
};

static void daemon_reconfigure(void* arg UNUSED)
{
    if (mySession == NULL)
    {
        tr_logAddInfo("Deferring reload until session is fully started.");
        seenHUP = true;
    }
    else
    {
        tr_variant settings;
        char const* configDir;

        /* reopen the logfile to allow for log rotation */
        if (logfileName != NULL)
        {
            reopen_log_file(logfileName);
        }

        configDir = tr_sessionGetConfigDir(mySession);
        tr_logAddInfo("Reloading settings from \"%s\"", configDir);
        tr_variantInitDict(&settings, 0);
        tr_variantDictAddBool(&settings, TR_KEY_rpc_enabled, true);
        tr_sessionLoadSettings(&settings, configDir, MY_NAME);
        tr_sessionSet(mySession, &settings);
        tr_variantFree(&settings);
        tr_sessionReloadBlocklists(mySession);
    }
}

static void daemon_stop(void* arg UNUSED)
{
    event_base_loopexit(ev_base, NULL);
}

static int daemon_start(void* raw_arg, bool foreground)
{
    bool boolVal;
    char const* pid_filename;
    bool pidfile_created = false;
    tr_session* session = NULL;
    struct event* status_ev = NULL;
    tr_watchdir_t watchdir = NULL;

    struct daemon_data* const arg = raw_arg;
    tr_variant* const settings = &arg->settings;
    char const* const configDir = arg->configDir;

#ifndef HAVE_SYSLOG
    (void)foreground;
#endif

    sd_notifyf(0, "MAINPID=%d\n", (int)getpid());

    /* should go before libevent calls */
    tr_net_init();

    /* setup event state */
    ev_base = event_base_new();

    if (ev_base == NULL)
    {
        char buf[256];
        tr_snprintf(buf, sizeof(buf), "Failed to init daemon event state: %s", tr_strerror(errno));
        printMessage(logfile, TR_LOG_ERROR, MY_NAME, buf, __FILE__, __LINE__);
        return 1;
    }

    /* start the session */
    tr_formatter_mem_init(MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
    tr_formatter_size_init(DISK_K, DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
    tr_formatter_speed_init(SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);
    session = tr_sessionInit(configDir, true, settings);
    tr_sessionSetRPCCallback(session, on_rpc_callback, NULL);
    tr_logAddNamedInfo(NULL, "Using settings from \"%s\"", configDir);
    tr_sessionSaveSettings(session, configDir, settings);

    pid_filename = NULL;
    tr_variantDictFindStr(settings, key_pidfile, &pid_filename, NULL);

    if (!tr_str_is_empty(pid_filename))
    {
        tr_error* error = NULL;
        tr_sys_file_t fp = tr_sys_file_open(pid_filename, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0666,
            &error);

        if (fp != TR_BAD_SYS_FILE)
        {
            tr_sys_file_write_fmt(fp, "%d", NULL, (int)getpid());
            tr_sys_file_close(fp, NULL);
            tr_logAddInfo("Saved pidfile \"%s\"", pid_filename);
            pidfile_created = true;
        }
        else
        {
            tr_logAddError("Unable to save pidfile \"%s\": %s", pid_filename, error->message);
            tr_error_free(error);
        }
    }

    if (tr_variantDictFindBool(settings, TR_KEY_rpc_authentication_required, &boolVal) && boolVal)
    {
        tr_logAddNamedInfo(MY_NAME, "requiring authentication");
    }

    mySession = session;

    /* If we got a SIGHUP during startup, process that now. */
    if (seenHUP)
    {
        daemon_reconfigure(arg);
    }

    /* maybe add a watchdir */
    if (tr_variantDictFindBool(settings, TR_KEY_watch_dir_enabled, &boolVal) && boolVal)
    {
        char const* dir;
        bool force_generic;

        if (!tr_variantDictFindBool(settings, key_watch_dir_force_generic, &force_generic))
        {
            force_generic = false;
        }

        if (tr_variantDictFindStr(settings, TR_KEY_watch_dir, &dir, NULL) && !tr_str_is_empty(dir))
        {
            tr_logAddInfo("Watching \"%s\" for new .torrent files", dir);

            if ((watchdir = tr_watchdir_new(dir, &onFileAdded, mySession, ev_base, force_generic)) == NULL)
            {
                goto cleanup;
            }
        }
    }

    /* load the torrents */
    {
        tr_torrent** torrents;
        tr_ctor* ctor = tr_ctorNew(mySession);

        if (arg->paused)
        {
            tr_ctorSetPaused(ctor, TR_FORCE, true);
        }

        torrents = tr_sessionLoadTorrents(mySession, ctor, NULL);
        tr_free(torrents);
        tr_ctorFree(ctor);
    }

#ifdef HAVE_SYSLOG

    if (!foreground)
    {
        openlog(MY_NAME, LOG_CONS | LOG_PID, LOG_DAEMON);
    }

#endif

    /* Create new timer event to report daemon status */
    {
        struct timeval one_sec = { .tv_sec = 1, .tv_usec = 0 };
        status_ev = event_new(ev_base, -1, EV_PERSIST, &periodicUpdate, NULL);

        if (status_ev == NULL)
        {
            tr_logAddError("Failed to create status event %s", tr_strerror(errno));
            goto cleanup;
        }

        if (event_add(status_ev, &one_sec) == -1)
        {
            tr_logAddError("Failed to add status event %s", tr_strerror(errno));
            goto cleanup;
        }
    }

    sd_notify(0, "READY=1\n");

    /* Run daemon event loop */
    if (event_base_dispatch(ev_base) == -1)
    {
        tr_logAddError("Failed to launch daemon event loop: %s", tr_strerror(errno));
        goto cleanup;
    }

cleanup:
    sd_notify(0, "STATUS=Closing transmission session...\n");
    printf("Closing transmission session...");

    tr_watchdir_free(watchdir);

    if (status_ev != NULL)
    {
        event_del(status_ev);
        event_free(status_ev);
    }

    event_base_free(ev_base);

    tr_sessionSaveSettings(mySession, configDir, settings);
    tr_sessionClose(mySession);
    pumpLogMessages(logfile);
    printf(" done.\n");

    /* shutdown */
#ifdef HAVE_SYSLOG

    if (!foreground)
    {
        syslog(LOG_INFO, "%s", "Closing session");
        closelog();
    }

#endif

    /* cleanup */
    if (pidfile_created)
    {
        tr_sys_path_remove(pid_filename, NULL);
    }

    sd_notify(0, "STATUS=\n");

    return 0;
}

static bool init_daemon_data(int argc, char* argv[], struct daemon_data* data, bool* foreground, int* ret)
{
    data->configDir = getConfigDir(argc, (char const* const*)argv);

    /* load settings from defaults + config file */
    tr_variantInitDict(&data->settings, 0);
    tr_variantDictAddBool(&data->settings, TR_KEY_rpc_enabled, true);
    bool const loaded = tr_sessionLoadSettings(&data->settings, data->configDir, MY_NAME);

    bool dumpSettings;

    *ret = 0;

    /* overwrite settings from the command line */
    if (!parse_args(argc, (char const**)argv, &data->settings, &data->paused, &dumpSettings, foreground, ret))
    {
        goto exit_early;
    }

    if (*foreground && logfile == TR_BAD_SYS_FILE)
    {
        logfile = tr_sys_file_get_std(TR_STD_SYS_FILE_ERR, NULL);
    }

    if (!loaded)
    {
        printMessage(logfile, TR_LOG_ERROR, MY_NAME, "Error loading config file -- exiting.", __FILE__, __LINE__);
        *ret = 1;
        goto exit_early;
    }

    if (dumpSettings)
    {
        char* str = tr_variantToStr(&data->settings, TR_VARIANT_FMT_JSON, NULL);
        fprintf(stderr, "%s", str);
        tr_free(str);
        goto exit_early;
    }

    return true;

exit_early:
    tr_variantFree(&data->settings);
    return false;
}

int tr_main(int argc, char* argv[])
{
    key_pidfile = tr_quark_new("pidfile", TR_BAD_SIZE);
    key_watch_dir_force_generic = tr_quark_new("watch-dir-force-generic", TR_BAD_SIZE);

    struct daemon_data data;
    bool foreground;
    int ret;

    if (!init_daemon_data(argc, argv, &data, &foreground, &ret))
    {
        return ret;
    }

    dtr_callbacks const cb =
    {
        .on_start = &daemon_start,
        .on_stop = &daemon_stop,
        .on_reconfigure = &daemon_reconfigure,
    };

    tr_error* error = NULL;

    if (!dtr_daemon(&cb, &data, foreground, &ret, &error))
    {
        char buf[256];
        tr_snprintf(buf, sizeof(buf), "Failed to daemonize: %s", error->message);
        printMessage(logfile, TR_LOG_ERROR, MY_NAME, buf, __FILE__, __LINE__);
        tr_error_free(error);
    }

    tr_variantFree(&data.settings);
    return ret;
}
