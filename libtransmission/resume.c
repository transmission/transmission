/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h>

#include "transmission.h"
#include "completion.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "metainfo.h" /* tr_metainfoGetBasename() */
#include "peer-mgr.h" /* pex */
#include "platform.h" /* tr_getResumeDir() */
#include "resume.h"
#include "session.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h" /* tr_buildPath */
#include "variant.h"

enum
{
    MAX_REMEMBERED_PEERS = 200
};

static char* getResumeFilename(tr_torrent const* tor, enum tr_metainfo_basename_format format)
{
    char* base = tr_metainfoGetBasename(tr_torrentInfo(tor), format);
    char* filename = tr_strdup_printf("%s" TR_PATH_DELIMITER_STR "%s.resume", tr_getResumeDir(tor->session), base);
    tr_free(base);
    return filename;
}

/***
****
***/

static void savePeers(tr_variant* dict, tr_torrent const* tor)
{
    int count;
    tr_pex* pex;

    count = tr_peerMgrGetPeers((tr_torrent*)tor, &pex, TR_AF_INET, TR_PEERS_INTERESTING, MAX_REMEMBERED_PEERS);

    if (count > 0)
    {
        tr_variantDictAddRaw(dict, TR_KEY_peers2, pex, sizeof(tr_pex) * count);
    }

    tr_free(pex);

    count = tr_peerMgrGetPeers((tr_torrent*)tor, &pex, TR_AF_INET6, TR_PEERS_INTERESTING, MAX_REMEMBERED_PEERS);

    if (count > 0)
    {
        tr_variantDictAddRaw(dict, TR_KEY_peers2_6, pex, sizeof(tr_pex) * count);
    }

    tr_free(pex);
}

static int addPeers(tr_torrent* tor, uint8_t const* buf, int buflen)
{
    int numAdded = 0;
    int const count = buflen / sizeof(tr_pex);

    for (int i = 0; i < count && numAdded < MAX_REMEMBERED_PEERS; ++i)
    {
        tr_pex pex;
        memcpy(&pex, buf + i * sizeof(tr_pex), sizeof(tr_pex));

        if (tr_isPex(&pex))
        {
            tr_peerMgrAddPex(tor, TR_PEER_FROM_RESUME, &pex, -1);
            ++numAdded;
        }
    }

    return numAdded;
}

static uint64_t loadPeers(tr_variant* dict, tr_torrent* tor)
{
    uint8_t const* str;
    size_t len;
    uint64_t ret = 0;

    if (tr_variantDictFindRaw(dict, TR_KEY_peers2, &str, &len))
    {
        int const numAdded = addPeers(tor, str, len);
        tr_logAddTorDbg(tor, "Loaded %d IPv4 peers from resume file", numAdded);
        ret = TR_FR_PEERS;
    }

    if (tr_variantDictFindRaw(dict, TR_KEY_peers2_6, &str, &len))
    {
        int const numAdded = addPeers(tor, str, len);
        tr_logAddTorDbg(tor, "Loaded %d IPv6 peers from resume file", numAdded);
        ret = TR_FR_PEERS;
    }

    return ret;
}

/***
****
***/

static void saveLabels(tr_variant* dict, tr_torrent const* tor)
{
    int const n = tr_ptrArraySize(&tor->labels);
    tr_variant* list = tr_variantDictAddList(dict, TR_KEY_labels, n);
    char const* const* labels = (char const* const*)tr_ptrArrayBase(&tor->labels);
    for (int i = 0; i < n; ++i)
    {
        tr_variantListAddStr(list, labels[i]);
    }
}

static uint64_t loadLabels(tr_variant* dict, tr_torrent* tor)
{
    uint64_t ret = 0;
    tr_variant* list;
    if (tr_variantDictFindList(dict, TR_KEY_labels, &list))
    {
        int const n = tr_variantListSize(list);
        char const* str;
        size_t str_len;
        for (int i = 0; i < n; ++i)
        {
            if (tr_variantGetStr(tr_variantListChild(list, i), &str, &str_len) && str != NULL && str_len != 0)
            {
                tr_ptrArrayAppend(&tor->labels, tr_strndup(str, str_len));
            }
        }

        ret = TR_FR_LABELS;
    }

    return ret;
}

/***
****
***/

static void saveDND(tr_variant* dict, tr_torrent const* tor)
{
    tr_variant* list;
    tr_info const* const inf = tr_torrentInfo(tor);
    tr_file_index_t const n = inf->fileCount;

    list = tr_variantDictAddList(dict, TR_KEY_dnd, n);

    for (tr_file_index_t i = 0; i < n; ++i)
    {
        tr_variantListAddBool(list, inf->files[i].dnd);
    }
}

static uint64_t loadDND(tr_variant* dict, tr_torrent* tor)
{
    uint64_t ret = 0;
    tr_variant* list = NULL;
    tr_file_index_t const n = tor->info.fileCount;

    if (tr_variantDictFindList(dict, TR_KEY_dnd, &list) && tr_variantListSize(list) == n)
    {
        bool tmp;
        tr_file_index_t* dl = tr_new(tr_file_index_t, n);
        tr_file_index_t* dnd = tr_new(tr_file_index_t, n);
        tr_file_index_t dlCount = 0;
        tr_file_index_t dndCount = 0;

        for (tr_file_index_t i = 0; i < n; ++i)
        {
            if (tr_variantGetBool(tr_variantListChild(list, i), &tmp) && tmp)
            {
                dnd[dndCount++] = i;
            }
            else
            {
                dl[dlCount++] = i;
            }
        }

        if (dndCount != 0)
        {
            tr_torrentInitFileDLs(tor, dnd, dndCount, false);
            tr_logAddTorDbg(tor, "Resume file found %d files listed as dnd", dndCount);
        }

        if (dlCount != 0)
        {
            tr_torrentInitFileDLs(tor, dl, dlCount, true);
            tr_logAddTorDbg(tor, "Resume file found %d files marked for download", dlCount);
        }

        tr_free(dnd);
        tr_free(dl);
        ret = TR_FR_DND;
    }
    else
    {
        tr_logAddTorDbg(tor, "Couldn't load DND flags. DND list (%p) has %zu" " children; torrent has %d files", (void*)list,
            tr_variantListSize(list), (int)n);
    }

    return ret;
}

/***
****
***/

static void saveFilePriorities(tr_variant* dict, tr_torrent const* tor)
{
    tr_variant* list;
    tr_info const* const inf = tr_torrentInfo(tor);
    tr_file_index_t const n = inf->fileCount;

    list = tr_variantDictAddList(dict, TR_KEY_priority, n);

    for (tr_file_index_t i = 0; i < n; ++i)
    {
        tr_variantListAddInt(list, inf->files[i].priority);
    }
}

static uint64_t loadFilePriorities(tr_variant* dict, tr_torrent* tor)
{
    tr_variant* list;
    uint64_t ret = 0;
    tr_file_index_t const n = tor->info.fileCount;

    if (tr_variantDictFindList(dict, TR_KEY_priority, &list) && tr_variantListSize(list) == n)
    {
        int64_t priority;

        for (tr_file_index_t i = 0; i < n; ++i)
        {
            if (tr_variantGetInt(tr_variantListChild(list, i), &priority))
            {
                tr_torrentInitFilePriority(tor, i, priority);
            }
        }

        ret = TR_FR_FILE_PRIORITIES;
    }

    return ret;
}

/***
****
***/

static void saveSingleSpeedLimit(tr_variant* d, tr_torrent* tor, tr_direction dir)
{
    tr_variantDictReserve(d, 3);
    tr_variantDictAddInt(d, TR_KEY_speed_Bps, tr_torrentGetSpeedLimit_Bps(tor, dir));
    tr_variantDictAddBool(d, TR_KEY_use_global_speed_limit, tr_torrentUsesSessionLimits(tor));
    tr_variantDictAddBool(d, TR_KEY_use_speed_limit, tr_torrentUsesSpeedLimit(tor, dir));
}

static void saveSpeedLimits(tr_variant* dict, tr_torrent* tor)
{
    saveSingleSpeedLimit(tr_variantDictAddDict(dict, TR_KEY_speed_limit_down, 0), tor, TR_DOWN);
    saveSingleSpeedLimit(tr_variantDictAddDict(dict, TR_KEY_speed_limit_up, 0), tor, TR_UP);
}

static void saveRatioLimits(tr_variant* dict, tr_torrent* tor)
{
    tr_variant* d = tr_variantDictAddDict(dict, TR_KEY_ratio_limit, 2);
    tr_variantDictAddReal(d, TR_KEY_ratio_limit, tr_torrentGetRatioLimit(tor));
    tr_variantDictAddInt(d, TR_KEY_ratio_mode, tr_torrentGetRatioMode(tor));
}

static void saveIdleLimits(tr_variant* dict, tr_torrent* tor)
{
    tr_variant* d = tr_variantDictAddDict(dict, TR_KEY_idle_limit, 2);
    tr_variantDictAddInt(d, TR_KEY_idle_limit, tr_torrentGetIdleLimit(tor));
    tr_variantDictAddInt(d, TR_KEY_idle_mode, tr_torrentGetIdleMode(tor));
}

static void loadSingleSpeedLimit(tr_variant* d, tr_direction dir, tr_torrent* tor)
{
    int64_t i;
    bool boolVal;

    if (tr_variantDictFindInt(d, TR_KEY_speed_Bps, &i))
    {
        tr_torrentSetSpeedLimit_Bps(tor, dir, i);
    }
    else if (tr_variantDictFindInt(d, TR_KEY_speed, &i))
    {
        tr_torrentSetSpeedLimit_Bps(tor, dir, i * 1024);
    }

    if (tr_variantDictFindBool(d, TR_KEY_use_speed_limit, &boolVal))
    {
        tr_torrentUseSpeedLimit(tor, dir, boolVal);
    }

    if (tr_variantDictFindBool(d, TR_KEY_use_global_speed_limit, &boolVal))
    {
        tr_torrentUseSessionLimits(tor, boolVal);
    }
}

static uint64_t loadSpeedLimits(tr_variant* dict, tr_torrent* tor)
{
    tr_variant* d;
    uint64_t ret = 0;

    if (tr_variantDictFindDict(dict, TR_KEY_speed_limit_up, &d))
    {
        loadSingleSpeedLimit(d, TR_UP, tor);
        ret = TR_FR_SPEEDLIMIT;
    }

    if (tr_variantDictFindDict(dict, TR_KEY_speed_limit_down, &d))
    {
        loadSingleSpeedLimit(d, TR_DOWN, tor);
        ret = TR_FR_SPEEDLIMIT;
    }

    return ret;
}

static uint64_t loadRatioLimits(tr_variant* dict, tr_torrent* tor)
{
    tr_variant* d;
    uint64_t ret = 0;

    if (tr_variantDictFindDict(dict, TR_KEY_ratio_limit, &d))
    {
        int64_t i;
        double dratio;

        if (tr_variantDictFindReal(d, TR_KEY_ratio_limit, &dratio))
        {
            tr_torrentSetRatioLimit(tor, dratio);
        }

        if (tr_variantDictFindInt(d, TR_KEY_ratio_mode, &i))
        {
            tr_torrentSetRatioMode(tor, i);
        }

        ret = TR_FR_RATIOLIMIT;
    }

    return ret;
}

static uint64_t loadIdleLimits(tr_variant* dict, tr_torrent* tor)
{
    tr_variant* d;
    uint64_t ret = 0;

    if (tr_variantDictFindDict(dict, TR_KEY_idle_limit, &d))
    {
        int64_t i;
        int64_t imin;

        if (tr_variantDictFindInt(d, TR_KEY_idle_limit, &imin))
        {
            tr_torrentSetIdleLimit(tor, imin);
        }

        if (tr_variantDictFindInt(d, TR_KEY_idle_mode, &i))
        {
            tr_torrentSetIdleMode(tor, i);
        }

        ret = TR_FR_IDLELIMIT;
    }

    return ret;
}

/***
****
***/

static void saveName(tr_variant* dict, tr_torrent const* tor)
{
    tr_variantDictAddStr(dict, TR_KEY_name, tr_torrentName(tor));
}

static uint64_t loadName(tr_variant* dict, tr_torrent* tor)
{
    uint64_t ret = 0;
    char const* name;

    if (tr_variantDictFindStr(dict, TR_KEY_name, &name, NULL))
    {
        ret = TR_FR_NAME;

        if (tr_strcmp0(tr_torrentName(tor), name) != 0)
        {
            tr_free(tor->info.name);
            tor->info.name = tr_strdup(name);
        }
    }

    return ret;
}

/***
****
***/

static void saveFilenames(tr_variant* dict, tr_torrent const* tor)
{
    bool any_renamed;
    tr_file_index_t const n = tor->info.fileCount;
    tr_file const* files = tor->info.files;

    any_renamed = false;

    for (tr_file_index_t i = 0; !any_renamed && i < n; ++i)
    {
        any_renamed = files[i].is_renamed;
    }

    if (any_renamed)
    {
        tr_variant* list = tr_variantDictAddList(dict, TR_KEY_files, n);

        for (tr_file_index_t i = 0; i < n; ++i)
        {
            tr_variantListAddStr(list, files[i].is_renamed ? files[i].name : "");
        }
    }
}

static uint64_t loadFilenames(tr_variant* dict, tr_torrent* tor)
{
    tr_variant* list;
    uint64_t ret = 0;

    if (tr_variantDictFindList(dict, TR_KEY_files, &list))
    {
        size_t const n = tr_variantListSize(list);
        tr_file* files = tor->info.files;

        for (size_t i = 0; i < tor->info.fileCount && i < n; ++i)
        {
            char const* str;
            size_t str_len;

            if (tr_variantGetStr(tr_variantListChild(list, i), &str, &str_len) && str != NULL && str_len != 0)
            {
                tr_free(files[i].name);
                files[i].name = tr_strndup(str, str_len);
                files[i].is_renamed = true;
            }
        }

        ret = TR_FR_FILENAMES;
    }

    return ret;
}

/***
****
***/

static void bitfieldToBenc(tr_bitfield const* b, tr_variant* benc)
{
    if (tr_bitfieldHasAll(b))
    {
        tr_variantInitStr(benc, "all", 3);
    }
    else if (tr_bitfieldHasNone(b))
    {
        tr_variantInitStr(benc, "none", 4);
    }
    else
    {
        size_t byte_count = 0;
        uint8_t* raw = tr_bitfieldGetRaw(b, &byte_count);
        tr_variantInitRaw(benc, raw, byte_count);
        tr_free(raw);
    }
}

static void saveProgress(tr_variant* dict, tr_torrent* tor)
{
    tr_variant* l;
    tr_variant* prog;
    tr_info const* inf = tr_torrentInfo(tor);
    time_t const now = tr_time();

    prog = tr_variantDictAddDict(dict, TR_KEY_progress, 3);

    /* add the file/piece check timestamps... */
    l = tr_variantDictAddList(prog, TR_KEY_time_checked, inf->fileCount);

    for (tr_file_index_t fi = 0; fi < inf->fileCount; ++fi)
    {
        time_t oldest_nonzero = now;
        time_t newest = 0;
        bool has_zero = false;
        time_t const mtime = tr_torrentGetFileMTime(tor, fi);
        tr_file const* f = &inf->files[fi];

        /* get the oldest and newest nonzero timestamps for pieces in this file */
        for (tr_piece_index_t i = f->firstPiece; i <= f->lastPiece; ++i)
        {
            tr_piece const* const p = &inf->pieces[i];

            if (p->timeChecked == 0)
            {
                has_zero = true;
            }
            else if (oldest_nonzero > p->timeChecked)
            {
                oldest_nonzero = p->timeChecked;
            }

            if (newest < p->timeChecked)
            {
                newest = p->timeChecked;
            }
        }

        /* If some of a file's pieces have been checked more recently than
           the file's mtime, and some less recently, then that file will
           have a list containing timestamps for each piece.

           However, the most common use case is that the file doesn't change
           after it's downloaded. To reduce overhead in the .resume file,
           only a single timestamp is saved for the file if *all* or *none*
           of the pieces were tested more recently than the file's mtime. */

        if (!has_zero && mtime <= oldest_nonzero) /* all checked */
        {
            tr_variantListAddInt(l, oldest_nonzero);
        }
        else if (newest < mtime) /* none checked */
        {
            tr_variantListAddInt(l, newest);
        }
        else /* some are checked, some aren't... so list piece by piece */
        {
            int const offset = oldest_nonzero - 1;
            tr_variant* ll = tr_variantListAddList(l, 2 + f->lastPiece - f->firstPiece);
            tr_variantListAddInt(ll, offset);

            for (tr_piece_index_t i = f->firstPiece; i <= f->lastPiece; ++i)
            {
                tr_piece const* const p = &inf->pieces[i];

                tr_variantListAddInt(ll, p->timeChecked != 0 ? p->timeChecked - offset : 0);
            }
        }
    }

    /* add the progress */
    if (tor->completeness == TR_SEED)
    {
        tr_variantDictAddStr(prog, TR_KEY_have, "all");
    }

    /* add the blocks bitfield */
    bitfieldToBenc(&tor->completion.blockBitfield, tr_variantDictAdd(prog, TR_KEY_blocks));
}

static uint64_t loadProgress(tr_variant* dict, tr_torrent* tor)
{
    uint64_t ret = 0;
    tr_variant* prog;
    tr_info const* inf = tr_torrentInfo(tor);

    for (size_t i = 0; i < inf->pieceCount; ++i)
    {
        inf->pieces[i].timeChecked = 0;
    }

    if (tr_variantDictFindDict(dict, TR_KEY_progress, &prog))
    {
        char const* err;
        char const* str;
        uint8_t const* raw;
        size_t rawlen;
        tr_variant* l;
        tr_variant* b;
        struct tr_bitfield blocks = TR_BITFIELD_INIT;

        if (tr_variantDictFindList(prog, TR_KEY_time_checked, &l))
        {
            /* per-piece timestamps were added in 2.20.

               If some of a file's pieces have been checked more recently than
               the file's mtime, and some lest recently, then that file will
               have a list containing timestamps for each piece.

               However, the most common use case is that the file doesn't change
               after it's downloaded. To reduce overhead in the .resume file,
               only a single timestamp is saved for the file if *all* or *none*
               of the pieces were tested more recently than the file's mtime. */

            for (tr_file_index_t fi = 0; fi < inf->fileCount; ++fi)
            {
                tr_variant* b = tr_variantListChild(l, fi);
                tr_file const* f = &inf->files[fi];

                if (tr_variantIsInt(b))
                {
                    int64_t t;
                    tr_variantGetInt(b, &t);

                    for (tr_piece_index_t i = f->firstPiece; i <= f->lastPiece; ++i)
                    {
                        inf->pieces[i].timeChecked = (time_t)t;
                    }
                }
                else if (tr_variantIsList(b))
                {
                    int64_t offset = 0;
                    int const pieces = f->lastPiece + 1 - f->firstPiece;

                    tr_variantGetInt(tr_variantListChild(b, 0), &offset);

                    for (int i = 0; i < pieces; ++i)
                    {
                        int64_t t = 0;
                        tr_variantGetInt(tr_variantListChild(b, i + 1), &t);
                        inf->pieces[f->firstPiece + i].timeChecked = (time_t)(t != 0 ? t + offset : 0);
                    }
                }
            }
        }
        else if (tr_variantDictFindList(prog, TR_KEY_mtimes, &l))
        {
            /* Before 2.20, we stored the files' mtimes in the .resume file.
               When loading the .resume file, a torrent's file would be flagged
               as untested if its stored mtime didn't match its real mtime. */

            for (tr_file_index_t fi = 0; fi < inf->fileCount; ++fi)
            {
                int64_t t;

                if (tr_variantGetInt(tr_variantListChild(l, fi), &t))
                {
                    tr_file const* f = &inf->files[fi];
                    time_t const mtime = tr_torrentGetFileMTime(tor, fi);
                    time_t const timeChecked = mtime == t ? mtime : 0;

                    for (tr_piece_index_t i = f->firstPiece; i <= f->lastPiece; ++i)
                    {
                        inf->pieces[i].timeChecked = timeChecked;
                    }
                }
            }
        }

        err = NULL;
        tr_bitfieldConstruct(&blocks, tor->blockCount);

        if ((b = tr_variantDictFind(prog, TR_KEY_blocks)) != NULL)
        {
            size_t buflen;
            uint8_t const* buf;

            if (!tr_variantGetRaw(b, &buf, &buflen))
            {
                err = "Invalid value for \"blocks\"";
            }
            else if (buflen == 3 && memcmp(buf, "all", 3) == 0)
            {
                tr_bitfieldSetHasAll(&blocks);
            }
            else if (buflen == 4 && memcmp(buf, "none", 4) == 0)
            {
                tr_bitfieldSetHasNone(&blocks);
            }
            else
            {
                tr_bitfieldSetRaw(&blocks, buf, buflen, true);
            }
        }
        else if (tr_variantDictFindStr(prog, TR_KEY_have, &str, NULL))
        {
            if (strcmp(str, "all") == 0)
            {
                tr_bitfieldSetHasAll(&blocks);
            }
            else
            {
                err = "Invalid value for HAVE";
            }
        }
        else if (tr_variantDictFindRaw(prog, TR_KEY_bitfield, &raw, &rawlen))
        {
            tr_bitfieldSetRaw(&blocks, raw, rawlen, true);
        }
        else
        {
            err = "Couldn't find 'pieces' or 'have' or 'bitfield'";
        }

        if (err != NULL)
        {
            tr_logAddTorDbg(tor, "Torrent needs to be verified - %s", err);
        }
        else
        {
            tr_cpBlockInit(&tor->completion, &blocks);
        }

        tr_bitfieldDestruct(&blocks);
        ret = TR_FR_PROGRESS;
    }

    return ret;
}

/***
****
***/

void tr_torrentSaveResume(tr_torrent* tor)
{
    int err;
    tr_variant top;
    char* filename;

    if (!tr_isTorrent(tor))
    {
        return;
    }

    tr_variantInitDict(&top, 50); /* arbitrary "big enough" number */
    tr_variantDictAddInt(&top, TR_KEY_seeding_time_seconds, tor->secondsSeeding);
    tr_variantDictAddInt(&top, TR_KEY_downloading_time_seconds, tor->secondsDownloading);
    tr_variantDictAddInt(&top, TR_KEY_activity_date, tor->activityDate);
    tr_variantDictAddInt(&top, TR_KEY_added_date, tor->addedDate);
    tr_variantDictAddInt(&top, TR_KEY_corrupt, tor->corruptPrev + tor->corruptCur);
    tr_variantDictAddInt(&top, TR_KEY_done_date, tor->doneDate);
    tr_variantDictAddStr(&top, TR_KEY_destination, tor->downloadDir);

    if (tor->incompleteDir != NULL)
    {
        tr_variantDictAddStr(&top, TR_KEY_incomplete_dir, tor->incompleteDir);
    }

    tr_variantDictAddInt(&top, TR_KEY_downloaded, tor->downloadedPrev + tor->downloadedCur);
    tr_variantDictAddInt(&top, TR_KEY_uploaded, tor->uploadedPrev + tor->uploadedCur);
    tr_variantDictAddInt(&top, TR_KEY_max_peers, tor->maxConnectedPeers);
    tr_variantDictAddInt(&top, TR_KEY_bandwidth_priority, tr_torrentGetPriority(tor));
    tr_variantDictAddBool(&top, TR_KEY_paused, !tor->isRunning && !tor->isQueued);
    savePeers(&top, tor);

    if (tr_torrentHasMetadata(tor))
    {
        saveFilePriorities(&top, tor);
        saveDND(&top, tor);
        saveProgress(&top, tor);
    }

    saveSpeedLimits(&top, tor);
    saveRatioLimits(&top, tor);
    saveIdleLimits(&top, tor);
    saveFilenames(&top, tor);
    saveName(&top, tor);
    saveLabels(&top, tor);

    filename = getResumeFilename(tor, TR_METAINFO_BASENAME_HASH);

    if ((err = tr_variantToFile(&top, TR_VARIANT_FMT_BENC, filename)) != 0)
    {
        tr_torrentSetLocalError(tor, "Unable to save resume file: %s", tr_strerror(err));
    }

    tr_free(filename);

    tr_variantFree(&top);
}

static uint64_t loadFromFile(tr_torrent* tor, uint64_t fieldsToLoad, bool* didRenameToHashOnlyName)
{
    TR_ASSERT(tr_isTorrent(tor));

    size_t len;
    int64_t i;
    char const* str;
    char* filename;
    tr_variant top;
    bool boolVal;
    uint64_t fieldsLoaded = 0;
    bool const wasDirty = tor->isDirty;
    tr_error* error = NULL;

    if (didRenameToHashOnlyName != NULL)
    {
        *didRenameToHashOnlyName = false;
    }

    filename = getResumeFilename(tor, TR_METAINFO_BASENAME_HASH);

    if (!tr_variantFromFile(&top, TR_VARIANT_FMT_BENC, filename, &error))
    {
        tr_logAddTorDbg(tor, "Couldn't read \"%s\": %s", filename, error->message);
        tr_error_clear(&error);

        char* old_filename = getResumeFilename(tor, TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH);

        if (!tr_variantFromFile(&top, TR_VARIANT_FMT_BENC, old_filename, &error))
        {
            tr_logAddTorDbg(tor, "Couldn't read \"%s\" either: %s", old_filename, error->message);
            tr_error_free(error);

            tr_free(old_filename);
            tr_free(filename);
            return fieldsLoaded;
        }

        if (tr_sys_path_rename(old_filename, filename, NULL))
        {
            tr_logAddTorDbg(tor, "Migrated resume file from \"%s\" to \"%s\"", old_filename, filename);

            if (didRenameToHashOnlyName != NULL)
            {
                *didRenameToHashOnlyName = true;
            }
        }

        tr_free(old_filename);
    }

    tr_logAddTorDbg(tor, "Read resume file \"%s\"", filename);

    if ((fieldsToLoad & TR_FR_CORRUPT) != 0 && tr_variantDictFindInt(&top, TR_KEY_corrupt, &i))
    {
        tor->corruptPrev = i;
        fieldsLoaded |= TR_FR_CORRUPT;
    }

    if ((fieldsToLoad & (TR_FR_PROGRESS | TR_FR_DOWNLOAD_DIR)) != 0 &&
        tr_variantDictFindStr(&top, TR_KEY_destination, &str, &len) && !tr_str_is_empty(str))
    {
        bool const is_current_dir = tor->currentDir == tor->downloadDir;
        tr_free(tor->downloadDir);
        tor->downloadDir = tr_strndup(str, len);

        if (is_current_dir)
        {
            tor->currentDir = tor->downloadDir;
        }

        fieldsLoaded |= TR_FR_DOWNLOAD_DIR;
    }

    if ((fieldsToLoad & (TR_FR_PROGRESS | TR_FR_INCOMPLETE_DIR)) != 0 &&
        tr_variantDictFindStr(&top, TR_KEY_incomplete_dir, &str, &len) && !tr_str_is_empty(str))
    {
        bool const is_current_dir = tor->currentDir == tor->incompleteDir;
        tr_free(tor->incompleteDir);
        tor->incompleteDir = tr_strndup(str, len);

        if (is_current_dir)
        {
            tor->currentDir = tor->incompleteDir;
        }

        fieldsLoaded |= TR_FR_INCOMPLETE_DIR;
    }

    if ((fieldsToLoad & TR_FR_DOWNLOADED) != 0 && tr_variantDictFindInt(&top, TR_KEY_downloaded, &i))
    {
        tor->downloadedPrev = i;
        fieldsLoaded |= TR_FR_DOWNLOADED;
    }

    if ((fieldsToLoad & TR_FR_UPLOADED) != 0 && tr_variantDictFindInt(&top, TR_KEY_uploaded, &i))
    {
        tor->uploadedPrev = i;
        fieldsLoaded |= TR_FR_UPLOADED;
    }

    if ((fieldsToLoad & TR_FR_MAX_PEERS) != 0 && tr_variantDictFindInt(&top, TR_KEY_max_peers, &i))
    {
        tor->maxConnectedPeers = i;
        fieldsLoaded |= TR_FR_MAX_PEERS;
    }

    if ((fieldsToLoad & TR_FR_RUN) != 0 && tr_variantDictFindBool(&top, TR_KEY_paused, &boolVal))
    {
        tor->isRunning = !boolVal;
        fieldsLoaded |= TR_FR_RUN;
    }

    if ((fieldsToLoad & TR_FR_ADDED_DATE) != 0 && tr_variantDictFindInt(&top, TR_KEY_added_date, &i))
    {
        tor->addedDate = i;
        fieldsLoaded |= TR_FR_ADDED_DATE;
    }

    if ((fieldsToLoad & TR_FR_DONE_DATE) != 0 && tr_variantDictFindInt(&top, TR_KEY_done_date, &i))
    {
        tor->doneDate = i;
        fieldsLoaded |= TR_FR_DONE_DATE;
    }

    if ((fieldsToLoad & TR_FR_ACTIVITY_DATE) != 0 && tr_variantDictFindInt(&top, TR_KEY_activity_date, &i))
    {
        tr_torrentSetDateActive(tor, i);
        fieldsLoaded |= TR_FR_ACTIVITY_DATE;
    }

    if ((fieldsToLoad & TR_FR_TIME_SEEDING) != 0 && tr_variantDictFindInt(&top, TR_KEY_seeding_time_seconds, &i))
    {
        tor->secondsSeeding = i;
        fieldsLoaded |= TR_FR_TIME_SEEDING;
    }

    if ((fieldsToLoad & TR_FR_TIME_DOWNLOADING) != 0 && tr_variantDictFindInt(&top, TR_KEY_downloading_time_seconds, &i))
    {
        tor->secondsDownloading = i;
        fieldsLoaded |= TR_FR_TIME_DOWNLOADING;
    }

    if ((fieldsToLoad & TR_FR_BANDWIDTH_PRIORITY) != 0 &&
        tr_variantDictFindInt(&top, TR_KEY_bandwidth_priority, &i) && tr_isPriority(i))
    {
        tr_torrentSetPriority(tor, i);
        fieldsLoaded |= TR_FR_BANDWIDTH_PRIORITY;
    }

    if ((fieldsToLoad & TR_FR_PEERS) != 0)
    {
        fieldsLoaded |= loadPeers(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_FILE_PRIORITIES) != 0)
    {
        fieldsLoaded |= loadFilePriorities(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_PROGRESS) != 0)
    {
        fieldsLoaded |= loadProgress(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_DND) != 0)
    {
        fieldsLoaded |= loadDND(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_SPEEDLIMIT) != 0)
    {
        fieldsLoaded |= loadSpeedLimits(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_RATIOLIMIT) != 0)
    {
        fieldsLoaded |= loadRatioLimits(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_IDLELIMIT) != 0)
    {
        fieldsLoaded |= loadIdleLimits(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_FILENAMES) != 0)
    {
        fieldsLoaded |= loadFilenames(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_NAME) != 0)
    {
        fieldsLoaded |= loadName(&top, tor);
    }

    if ((fieldsToLoad & TR_FR_LABELS) != 0)
    {
        fieldsLoaded |= loadLabels(&top, tor);
    }

    /* loading the resume file triggers of a lot of changes,
     * but none of them needs to trigger a re-saving of the
     * same resume information... */
    tor->isDirty = wasDirty;

    tr_variantFree(&top);
    tr_free(filename);
    return fieldsLoaded;
}

static uint64_t setFromCtor(tr_torrent* tor, uint64_t fields, tr_ctor const* ctor, int mode)
{
    uint64_t ret = 0;

    if ((fields & TR_FR_RUN) != 0)
    {
        bool isPaused;

        if (tr_ctorGetPaused(ctor, mode, &isPaused))
        {
            tor->isRunning = !isPaused;
            ret |= TR_FR_RUN;
        }
    }

    if ((fields & TR_FR_MAX_PEERS) != 0)
    {
        if (tr_ctorGetPeerLimit(ctor, mode, &tor->maxConnectedPeers))
        {
            ret |= TR_FR_MAX_PEERS;
        }
    }

    if ((fields & TR_FR_DOWNLOAD_DIR) != 0)
    {
        char const* path;

        if (tr_ctorGetDownloadDir(ctor, mode, &path) && !tr_str_is_empty(path))
        {
            ret |= TR_FR_DOWNLOAD_DIR;
            tr_free(tor->downloadDir);
            tor->downloadDir = tr_strdup(path);
        }
    }

    return ret;
}

static uint64_t useManditoryFields(tr_torrent* tor, uint64_t fields, tr_ctor const* ctor)
{
    return setFromCtor(tor, fields, ctor, TR_FORCE);
}

static uint64_t useFallbackFields(tr_torrent* tor, uint64_t fields, tr_ctor const* ctor)
{
    return setFromCtor(tor, fields, ctor, TR_FALLBACK);
}

uint64_t tr_torrentLoadResume(tr_torrent* tor, uint64_t fieldsToLoad, tr_ctor const* ctor, bool* didRenameToHashOnlyName)
{
    TR_ASSERT(tr_isTorrent(tor));

    uint64_t ret = 0;

    ret |= useManditoryFields(tor, fieldsToLoad, ctor);
    fieldsToLoad &= ~ret;
    ret |= loadFromFile(tor, fieldsToLoad, didRenameToHashOnlyName);
    fieldsToLoad &= ~ret;
    ret |= useFallbackFields(tor, fieldsToLoad, ctor);

    return ret;
}

void tr_torrentRemoveResume(tr_torrent const* tor)
{
    char* filename;

    filename = getResumeFilename(tor, TR_METAINFO_BASENAME_HASH);
    tr_sys_path_remove(filename, NULL);
    tr_free(filename);

    filename = getResumeFilename(tor, TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH);
    tr_sys_path_remove(filename, NULL);
    tr_free(filename);
}
