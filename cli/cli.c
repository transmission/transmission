/******************************************************************************
 * Copyright (c) Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <stdio.h> /* fprintf () */
#include <stdlib.h> /* atoi () */
#include <string.h> /* memcmp () */
#include <signal.h>

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h> /* tr_wait_msec */
#include <libtransmission/variant.h>
#include <libtransmission/version.h>
#include <libtransmission/web.h> /* tr_webRun */

/***
****
***/

#define MEM_K 1024
#define MEM_K_STR "KiB"
#define MEM_M_STR "MiB"
#define MEM_G_STR "GiB"
#define MEM_T_STR "TiB"

#define DISK_K 1000
#define DISK_B_STR   "B"
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
***/

#define LINEWIDTH 80
#define MY_CONFIG_NAME "transmission"
#define MY_READABLE_NAME "transmission-cli"

static bool showVersion = false;
static bool verify = false;
static sig_atomic_t gotsig = false;
static sig_atomic_t manualUpdate = false;

static char const* torrentPath = NULL;

static struct tr_option const options[] =
{
    { 'b', "blocklist", "Enable peer blocklists", "b", false, NULL },
    { 'B', "no-blocklist", "Disable peer blocklists", "B", false, NULL },
    { 'd', "downlimit", "Set max download speed in "SPEED_K_STR, "d", true, "<speed>" },
    { 'D', "no-downlimit", "Don't limit the download speed", "D", false, NULL },
    { 910, "encryption-required", "Encrypt all peer connections", "er", false, NULL },
    { 911, "encryption-preferred", "Prefer encrypted peer connections", "ep", false, NULL },
    { 912, "encryption-tolerated", "Prefer unencrypted peer connections", "et", false, NULL },
    { 'f', "finish", "Run a script when the torrent finishes", "f", true, "<script>" },
    { 'g', "config-dir", "Where to find configuration files", "g", true, "<path>" },
    { 'm', "portmap", "Enable portmapping via NAT-PMP or UPnP", "m", false, NULL },
    { 'M', "no-portmap", "Disable portmapping", "M", false, NULL },
    { 'p', "port", "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "p", true, "<port>" },
    { 't', "tos", "Peer socket TOS (0 to 255, default=" TR_DEFAULT_PEER_SOCKET_TOS_STR ")", "t", true, "<tos>" },
    { 'u', "uplimit", "Set max upload speed in "SPEED_K_STR, "u", true, "<speed>" },
    { 'U', "no-uplimit", "Don't limit the upload speed", "U", false, NULL },
    { 'v', "verify", "Verify the specified torrent", "v", false, NULL },
    { 'V', "version", "Show version number and exit", "V", false, NULL },
    { 'w', "download-dir", "Where to save downloaded data", "w", true, "<path>" },
    { 0, NULL, NULL, NULL, false, NULL }
};

static char const* getUsage(void)
{
    return "A fast and easy BitTorrent client\n"
        "\n"
        "Usage: " MY_READABLE_NAME " [options] <file|url|magnet>";
}

static int parseCommandLine(tr_variant*, int argc, char const** argv);

static void sigHandler(int signal);

static char* tr_strlratio(char* buf, double ratio, size_t buflen)
{
    if ((int)ratio == TR_RATIO_NA)
    {
        tr_strlcpy(buf, _("None"), buflen);
    }
    else if ((int)ratio == TR_RATIO_INF)
    {
        tr_strlcpy(buf, "Inf", buflen);
    }
    else if (ratio < 10.0)
    {
        tr_snprintf(buf, buflen, "%.2f", ratio);
    }
    else if (ratio < 100.0)
    {
        tr_snprintf(buf, buflen, "%.1f", ratio);
    }
    else
    {
        tr_snprintf(buf, buflen, "%.0f", ratio);
    }

    return buf;
}

static bool waitingOnWeb;

static void onTorrentFileDownloaded(tr_session* session UNUSED, bool did_connect UNUSED, bool did_timeout UNUSED,
    long response_code UNUSED, void const* response, size_t response_byte_count, void* ctor)
{
    tr_ctorSetMetainfo(ctor, response, response_byte_count);
    waitingOnWeb = false;
}

static void getStatusStr(tr_stat const* st, char* buf, size_t buflen)
{
    if (st->activity == TR_STATUS_CHECK_WAIT)
    {
        tr_snprintf(buf, buflen, "Waiting to verify local files");
    }
    else if (st->activity == TR_STATUS_CHECK)
    {
        tr_snprintf(buf, buflen, "Verifying local files (%.2f%%, %.2f%% valid)", tr_truncd(100 * st->recheckProgress, 2),
            tr_truncd(100 * st->percentDone, 2));
    }
    else if (st->activity == TR_STATUS_DOWNLOAD)
    {
        char upStr[80];
        char dnStr[80];
        char ratioStr[80];

        tr_formatter_speed_KBps(upStr, st->pieceUploadSpeed_KBps, sizeof(upStr));
        tr_formatter_speed_KBps(dnStr, st->pieceDownloadSpeed_KBps, sizeof(dnStr));
        tr_strlratio(ratioStr, st->ratio, sizeof(ratioStr));

        tr_snprintf(buf, buflen, "Progress: %.1f%%, dl from %d of %d peers (%s), ul to %d (%s) [%s]",
            tr_truncd(100 * st->percentDone, 1), st->peersSendingToUs, st->peersConnected, dnStr, st->peersGettingFromUs, upStr,
            ratioStr);
    }
    else if (st->activity == TR_STATUS_SEED)
    {
        char upStr[80];
        char ratioStr[80];

        tr_formatter_speed_KBps(upStr, st->pieceUploadSpeed_KBps, sizeof(upStr));
        tr_strlratio(ratioStr, st->ratio, sizeof(ratioStr));

        tr_snprintf(buf, buflen, "Seeding, uploading to %d of %d peer(s), %s [%s]", st->peersGettingFromUs, st->peersConnected,
            upStr, ratioStr);
    }
    else
    {
        *buf = '\0';
    }
}

static char const* getConfigDir(int argc, char const** argv)
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
        configDir = tr_getDefaultConfigDir(MY_CONFIG_NAME);
    }

    return configDir;
}

int tr_main(int argc, char* argv[])
{
    tr_session* h;
    tr_ctor* ctor;
    tr_torrent* tor = NULL;
    tr_variant settings;
    char const* configDir;
    uint8_t* fileContents;
    size_t fileLength;
    char const* str;

    tr_formatter_mem_init(MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
    tr_formatter_size_init(DISK_K, DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
    tr_formatter_speed_init(SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);

    printf("%s %s\n", MY_READABLE_NAME, LONG_VERSION_STRING);

    /* user needs to pass in at least one argument */
    if (argc < 2)
    {
        tr_getopt_usage(MY_READABLE_NAME, getUsage(), options);
        return EXIT_FAILURE;
    }

    /* load the defaults from config file + libtransmission defaults */
    tr_variantInitDict(&settings, 0);
    configDir = getConfigDir(argc, (char const**)argv);
    tr_sessionLoadSettings(&settings, configDir, MY_CONFIG_NAME);

    /* the command line overrides defaults */
    if (parseCommandLine(&settings, argc, (char const**)argv) != 0)
    {
        return EXIT_FAILURE;
    }

    if (showVersion)
    {
        return EXIT_SUCCESS;
    }

    /* Check the options for validity */
    if (torrentPath == NULL)
    {
        fprintf(stderr, "No torrent specified!\n");
        return EXIT_FAILURE;
    }

    if (tr_variantDictFindStr(&settings, TR_KEY_download_dir, &str, NULL))
    {
        if (!tr_sys_path_exists(str, NULL))
        {
            tr_error* error = NULL;

            if (!tr_sys_dir_create(str, TR_SYS_DIR_CREATE_PARENTS, 0700, &error))
            {
                fprintf(stderr, "Unable to create download directory \"%s\": %s\n", str, error->message);
                tr_error_free(error);
                return EXIT_FAILURE;
            }
        }
    }

    h = tr_sessionInit(configDir, false, &settings);

    ctor = tr_ctorNew(h);

    fileContents = tr_loadFile(torrentPath, &fileLength, NULL);
    tr_ctorSetPaused(ctor, TR_FORCE, false);

    if (fileContents != NULL)
    {
        tr_ctorSetMetainfo(ctor, fileContents, fileLength);
    }
    else if (memcmp(torrentPath, "magnet:?", 8) == 0)
    {
        tr_ctorSetMetainfoFromMagnetLink(ctor, torrentPath);
    }
    else if (memcmp(torrentPath, "http", 4) == 0)
    {
        tr_webRun(h, torrentPath, onTorrentFileDownloaded, ctor);
        waitingOnWeb = true;

        while (waitingOnWeb)
        {
            tr_wait_msec(1000);
        }
    }
    else
    {
        fprintf(stderr, "ERROR: Unrecognized torrent \"%s\".\n", torrentPath);
        fprintf(stderr, " * If you're trying to create a torrent, use transmission-create.\n");
        fprintf(stderr, " * If you're trying to see a torrent's info, use transmission-show.\n");
        tr_sessionClose(h);
        return EXIT_FAILURE;
    }

    tr_free(fileContents);

    tor = tr_torrentNew(ctor, NULL, NULL);
    tr_ctorFree(ctor);

    if (tor == NULL)
    {
        fprintf(stderr, "Failed opening torrent file `%s'\n", torrentPath);
        tr_sessionClose(h);
        return EXIT_FAILURE;
    }

    signal(SIGINT, sigHandler);
#ifndef _WIN32
    signal(SIGHUP, sigHandler);
#endif
    tr_torrentStart(tor);

    if (verify)
    {
        verify = false;
        tr_torrentVerify(tor, NULL, NULL);
    }

    for (;;)
    {
        char line[LINEWIDTH];
        tr_stat const* st;
        char const* messageName[] =
        {
            NULL,
            "Tracker gave a warning:",
            "Tracker gave an error:",
            "Error:"
        };

        tr_wait_msec(200);

        if (gotsig)
        {
            gotsig = false;
            printf("\nStopping torrent...\n");
            tr_torrentStop(tor);
        }

        if (manualUpdate)
        {
            manualUpdate = false;

            if (!tr_torrentCanManualUpdate(tor))
            {
                fprintf(stderr, "\nReceived SIGHUP, but can't send a manual update now\n");
            }
            else
            {
                fprintf(stderr, "\nReceived SIGHUP: manual update scheduled\n");
                tr_torrentManualUpdate(tor);
            }
        }

        st = tr_torrentStat(tor);

        if (st->activity == TR_STATUS_STOPPED)
        {
            break;
        }

        getStatusStr(st, line, sizeof(line));
        printf("\r%-*s", LINEWIDTH, line);

        if (messageName[st->error])
        {
            fprintf(stderr, "\n%s: %s\n", messageName[st->error], st->errorString);
        }
    }

    tr_sessionSaveSettings(h, configDir, &settings);

    printf("\n");
    tr_variantFree(&settings);
    tr_sessionClose(h);
    return EXIT_SUCCESS;
}

/***
****
****
***/

static int parseCommandLine(tr_variant* d, int argc, char const** argv)
{
    int c;
    char const* optarg;

    while ((c = tr_getopt(getUsage(), argc, argv, options, &optarg)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'b':
            tr_variantDictAddBool(d, TR_KEY_blocklist_enabled, true);
            break;

        case 'B':
            tr_variantDictAddBool(d, TR_KEY_blocklist_enabled, false);
            break;

        case 'd':
            tr_variantDictAddInt(d, TR_KEY_speed_limit_down, atoi(optarg));
            tr_variantDictAddBool(d, TR_KEY_speed_limit_down_enabled, true);
            break;

        case 'D':
            tr_variantDictAddBool(d, TR_KEY_speed_limit_down_enabled, false);
            break;

        case 'f':
            tr_variantDictAddStr(d, TR_KEY_script_torrent_done_filename, optarg);
            tr_variantDictAddBool(d, TR_KEY_script_torrent_done_enabled, true);
            break;

        case 'g': /* handled above */
            break;

        case 'm':
            tr_variantDictAddBool(d, TR_KEY_port_forwarding_enabled, true);
            break;

        case 'M':
            tr_variantDictAddBool(d, TR_KEY_port_forwarding_enabled, false);
            break;

        case 'p':
            tr_variantDictAddInt(d, TR_KEY_peer_port, atoi(optarg));
            break;

        case 't':
            tr_variantDictAddInt(d, TR_KEY_peer_socket_tos, atoi(optarg));
            break;

        case 'u':
            tr_variantDictAddInt(d, TR_KEY_speed_limit_up, atoi(optarg));
            tr_variantDictAddBool(d, TR_KEY_speed_limit_up_enabled, true);
            break;

        case 'U':
            tr_variantDictAddBool(d, TR_KEY_speed_limit_up_enabled, false);
            break;

        case 'v':
            verify = true;
            break;

        case 'V':
            showVersion = true;
            break;

        case 'w':
            tr_variantDictAddStr(d, TR_KEY_download_dir, optarg);
            break;

        case 910:
            tr_variantDictAddInt(d, TR_KEY_encryption, TR_ENCRYPTION_REQUIRED);
            break;

        case 911:
            tr_variantDictAddInt(d, TR_KEY_encryption, TR_ENCRYPTION_PREFERRED);
            break;

        case 912:
            tr_variantDictAddInt(d, TR_KEY_encryption, TR_CLEAR_PREFERRED);
            break;

        case TR_OPT_UNK:
            if (torrentPath == NULL)
            {
                torrentPath = optarg;
            }

            break;

        default:
            return 1;
        }
    }

    return 0;
}

static void sigHandler(int signal)
{
    switch (signal)
    {
    case SIGINT:
        gotsig = true;
        break;

#ifndef _WIN32

    case SIGHUP:
        manualUpdate = true;
        break;

#endif

    default:
        break;
    }
}
