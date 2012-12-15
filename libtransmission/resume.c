/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <unistd.h> /* unlink */

#include <string.h>

#include "transmission.h"
#include "completion.h"
#include "metainfo.h" /* tr_metainfoGetBasename () */
#include "peer-mgr.h" /* pex */
#include "platform.h" /* tr_getResumeDir () */
#include "resume.h"
#include "session.h"
#include "torrent.h"
#include "utils.h" /* tr_buildPath */
#include "variant.h"

#define KEY_ACTIVITY_DATE       "activity-date"
#define KEY_ADDED_DATE          "added-date"
#define KEY_CORRUPT             "corrupt"
#define KEY_DONE_DATE           "done-date"
#define KEY_DOWNLOAD_DIR        "destination"
#define KEY_DND                 "dnd"
#define KEY_DOWNLOADED          "downloaded"
#define KEY_INCOMPLETE_DIR      "incomplete-dir"
#define KEY_MAX_PEERS           "max-peers"
#define KEY_PAUSED              "paused"
#define KEY_PEERS               "peers2"
#define KEY_PEERS6              "peers2-6"
#define KEY_FILE_PRIORITIES     "priority"
#define KEY_BANDWIDTH_PRIORITY  "bandwidth-priority"
#define KEY_PROGRESS            "progress"
#define KEY_SPEEDLIMIT_OLD      "speed-limit"
#define KEY_SPEEDLIMIT_UP       "speed-limit-up"
#define KEY_SPEEDLIMIT_DOWN     "speed-limit-down"
#define KEY_RATIOLIMIT          "ratio-limit"
#define KEY_IDLELIMIT           "idle-limit"
#define KEY_UPLOADED            "uploaded"

#define KEY_SPEED_KiBps            "speed"
#define KEY_SPEED_Bps              "speed-Bps"
#define KEY_USE_GLOBAL_SPEED_LIMIT "use-global-speed-limit"
#define KEY_USE_SPEED_LIMIT        "use-speed-limit"
#define KEY_TIME_SEEDING           "seeding-time-seconds"
#define KEY_TIME_DOWNLOADING       "downloading-time-seconds"
#define KEY_SPEEDLIMIT_DOWN_SPEED  "down-speed"
#define KEY_SPEEDLIMIT_DOWN_MODE   "down-mode"
#define KEY_SPEEDLIMIT_UP_SPEED    "up-speed"
#define KEY_SPEEDLIMIT_UP_MODE     "up-mode"
#define KEY_RATIOLIMIT_RATIO       "ratio-limit"
#define KEY_RATIOLIMIT_MODE        "ratio-mode"
#define KEY_IDLELIMIT_MINS         "idle-limit"
#define KEY_IDLELIMIT_MODE         "idle-mode"

#define KEY_PROGRESS_CHECKTIME "time-checked"
#define KEY_PROGRESS_MTIMES    "mtimes"
#define KEY_PROGRESS_BITFIELD  "bitfield"
#define KEY_PROGRESS_BLOCKS    "blocks"
#define KEY_PROGRESS_BLOCKS_STRLEN 6
#define KEY_PROGRESS_HAVE      "have"

enum
{
    MAX_REMEMBERED_PEERS = 200
};

static char*
getResumeFilename (const tr_torrent * tor)
{
    char * base = tr_metainfoGetBasename (tr_torrentInfo (tor));
    char * filename = tr_strdup_printf ("%s" TR_PATH_DELIMITER_STR "%s.resume",
                                        tr_getResumeDir (tor->session), base);
    tr_free (base);
    return filename;
}

/***
****
***/

static void
savePeers (tr_variant * dict, const tr_torrent * tor)
{
    int count;
    tr_pex * pex;

    count = tr_peerMgrGetPeers ((tr_torrent*) tor, &pex, TR_AF_INET, TR_PEERS_INTERESTING, MAX_REMEMBERED_PEERS);
    if (count > 0)
        tr_variantDictAddRaw (dict, KEY_PEERS, pex, sizeof (tr_pex) * count);
    tr_free (pex);

    count = tr_peerMgrGetPeers ((tr_torrent*) tor, &pex, TR_AF_INET6, TR_PEERS_INTERESTING, MAX_REMEMBERED_PEERS);
    if (count > 0)
        tr_variantDictAddRaw (dict, KEY_PEERS6, pex, sizeof (tr_pex) * count);

    tr_free (pex);
}

static int
addPeers (tr_torrent * tor, const uint8_t * buf, int buflen)
{
    int i;
    int numAdded = 0;
    const int count = buflen / sizeof (tr_pex);

    for (i=0; i<count && numAdded<MAX_REMEMBERED_PEERS; ++i)
    {
        tr_pex pex;
        memcpy (&pex, buf + (i * sizeof (tr_pex)), sizeof (tr_pex));
        if (tr_isPex (&pex))
        {
            tr_peerMgrAddPex (tor, TR_PEER_FROM_RESUME, &pex, -1);
            ++numAdded;
        }
    }

    return numAdded;
}


static uint64_t
loadPeers (tr_variant * dict, tr_torrent * tor)
{
    uint64_t        ret = 0;
    const uint8_t * str;
    size_t          len;

    if (tr_variantDictFindRaw (dict, KEY_PEERS, &str, &len))
    {
        const int numAdded = addPeers (tor, str, len);
        tr_tordbg (tor, "Loaded %d IPv4 peers from resume file", numAdded);
        ret = TR_FR_PEERS;
    }

    if (tr_variantDictFindRaw (dict, KEY_PEERS6, &str, &len))
    {
        const int numAdded = addPeers (tor, str, len);
        tr_tordbg (tor, "Loaded %d IPv6 peers from resume file", numAdded);
        ret = TR_FR_PEERS;
    }

    return ret;
}

/***
****
***/

static void
saveDND (tr_variant * dict, const tr_torrent * tor)
{
    tr_variant * list;
    tr_file_index_t i;
    const tr_info * const inf = tr_torrentInfo (tor);
    const tr_file_index_t n = inf->fileCount;

    list = tr_variantDictAddList (dict, KEY_DND, n);
    for (i=0; i<n; ++i)
        tr_variantListAddInt (list, inf->files[i].dnd ? 1 : 0);
}

static uint64_t
loadDND (tr_variant * dict, tr_torrent * tor)
{
    uint64_t ret = 0;
    tr_variant * list = NULL;
    const tr_file_index_t n = tor->info.fileCount;

    if (tr_variantDictFindList (dict, KEY_DND, &list)
      && (tr_variantListSize (list) == n))
    {
        int64_t           tmp;
        tr_file_index_t * dl = tr_new (tr_file_index_t, n);
        tr_file_index_t * dnd = tr_new (tr_file_index_t, n);
        tr_file_index_t   i, dlCount = 0, dndCount = 0;

        for (i=0; i<n; ++i)
        {
            if (tr_variantGetInt (tr_variantListChild (list, i), &tmp) && tmp)
                dnd[dndCount++] = i;
            else
                dl[dlCount++] = i;
        }

        if (dndCount)
        {
            tr_torrentInitFileDLs (tor, dnd, dndCount, false);
            tr_tordbg (tor, "Resume file found %d files listed as dnd",
                       dndCount);
        }
        if (dlCount)
        {
            tr_torrentInitFileDLs (tor, dl, dlCount, true);
            tr_tordbg (tor,
                       "Resume file found %d files marked for download",
                       dlCount);
        }

        tr_free (dnd);
        tr_free (dl);
        ret = TR_FR_DND;
    }
    else
    {
        tr_tordbg (
            tor,
            "Couldn't load DND flags. DND list (%p) has %zu children; torrent has %d files",
            list, tr_variantListSize (list), (int)n);
    }

    return ret;
}

/***
****
***/

static void
saveFilePriorities (tr_variant * dict, const tr_torrent * tor)
{
    tr_variant * list;
    tr_file_index_t i;
    const tr_info * const inf = tr_torrentInfo (tor);
    const tr_file_index_t n = inf->fileCount;

    list = tr_variantDictAddList (dict, KEY_FILE_PRIORITIES, n);
    for (i = 0; i < n; ++i)
        tr_variantListAddInt (list, inf->files[i].priority);
}

static uint64_t
loadFilePriorities (tr_variant * dict, tr_torrent * tor)
{
    tr_variant * list;
    uint64_t ret = 0;
    const tr_file_index_t n = tor->info.fileCount;

    if (tr_variantDictFindList (dict, KEY_FILE_PRIORITIES, &list)
      && (tr_variantListSize (list) == n))
    {
        int64_t priority;
        tr_file_index_t i;
        for (i = 0; i < n; ++i)
            if (tr_variantGetInt (tr_variantListChild (list, i), &priority))
                tr_torrentInitFilePriority (tor, i, priority);
        ret = TR_FR_FILE_PRIORITIES;
    }

    return ret;
}

/***
****
***/

static void
saveSingleSpeedLimit (tr_variant * d, tr_torrent * tor, tr_direction dir)
{
    tr_variantDictReserve (d, 3);
    tr_variantDictAddInt (d, KEY_SPEED_Bps, tr_torrentGetSpeedLimit_Bps (tor, dir));
    tr_variantDictAddBool (d, KEY_USE_GLOBAL_SPEED_LIMIT, tr_torrentUsesSessionLimits (tor));
    tr_variantDictAddBool (d, KEY_USE_SPEED_LIMIT, tr_torrentUsesSpeedLimit (tor, dir));
}

static void
saveSpeedLimits (tr_variant * dict, tr_torrent * tor)
{
    saveSingleSpeedLimit (tr_variantDictAddDict (dict, KEY_SPEEDLIMIT_DOWN, 0), tor, TR_DOWN);
    saveSingleSpeedLimit (tr_variantDictAddDict (dict, KEY_SPEEDLIMIT_UP, 0), tor, TR_UP);
}

static void
saveRatioLimits (tr_variant * dict, tr_torrent * tor)
{
    tr_variant * d = tr_variantDictAddDict (dict, KEY_RATIOLIMIT, 2);
    tr_variantDictAddReal (d, KEY_RATIOLIMIT_RATIO, tr_torrentGetRatioLimit (tor));
    tr_variantDictAddInt (d, KEY_RATIOLIMIT_MODE, tr_torrentGetRatioMode (tor));
}

static void
saveIdleLimits (tr_variant * dict, tr_torrent * tor)
{
    tr_variant * d = tr_variantDictAddDict (dict, KEY_IDLELIMIT, 2);
    tr_variantDictAddInt (d, KEY_IDLELIMIT_MINS, tr_torrentGetIdleLimit (tor));
    tr_variantDictAddInt (d, KEY_IDLELIMIT_MODE, tr_torrentGetIdleMode (tor));
}

static void
loadSingleSpeedLimit (tr_variant * d, tr_direction dir, tr_torrent * tor)
{
    int64_t i;
    bool boolVal;

    if (tr_variantDictFindInt (d, KEY_SPEED_Bps, &i))
        tr_torrentSetSpeedLimit_Bps (tor, dir, i);
    else if (tr_variantDictFindInt (d, KEY_SPEED_KiBps, &i))
        tr_torrentSetSpeedLimit_Bps (tor, dir, i*1024);

    if (tr_variantDictFindBool (d, KEY_USE_SPEED_LIMIT, &boolVal))
        tr_torrentUseSpeedLimit (tor, dir, boolVal);

    if (tr_variantDictFindBool (d, KEY_USE_GLOBAL_SPEED_LIMIT, &boolVal))
        tr_torrentUseSessionLimits (tor, boolVal);
}

enum old_speed_modes
{
    TR_SPEEDLIMIT_GLOBAL,   /* only follow the overall speed limit */
    TR_SPEEDLIMIT_SINGLE    /* only follow the per-torrent limit */
};

static uint64_t
loadSpeedLimits (tr_variant * dict, tr_torrent * tor)
{
    tr_variant * d;
    uint64_t ret = 0;


    if (tr_variantDictFindDict (dict, KEY_SPEEDLIMIT_UP, &d))
    {
        loadSingleSpeedLimit (d, TR_UP, tor);
        ret = TR_FR_SPEEDLIMIT;
    }
    if (tr_variantDictFindDict (dict, KEY_SPEEDLIMIT_DOWN, &d))
    {
        loadSingleSpeedLimit (d, TR_DOWN, tor);
        ret = TR_FR_SPEEDLIMIT;
    }

    /* older speedlimit structure */
    if (!ret && tr_variantDictFindDict (dict, KEY_SPEEDLIMIT_OLD, &d))
    {

        int64_t i;
        if (tr_variantDictFindInt (d, KEY_SPEEDLIMIT_DOWN_SPEED, &i))
            tr_torrentSetSpeedLimit_Bps (tor, TR_DOWN, i*1024);
        if (tr_variantDictFindInt (d, KEY_SPEEDLIMIT_DOWN_MODE, &i)) {
            tr_torrentUseSpeedLimit (tor, TR_DOWN, i==TR_SPEEDLIMIT_SINGLE);
            tr_torrentUseSessionLimits (tor, i==TR_SPEEDLIMIT_GLOBAL);
         }
        if (tr_variantDictFindInt (d, KEY_SPEEDLIMIT_UP_SPEED, &i))
            tr_torrentSetSpeedLimit_Bps (tor, TR_UP, i*1024);
        if (tr_variantDictFindInt (d, KEY_SPEEDLIMIT_UP_MODE, &i)) {
            tr_torrentUseSpeedLimit (tor, TR_UP, i==TR_SPEEDLIMIT_SINGLE);
            tr_torrentUseSessionLimits (tor, i==TR_SPEEDLIMIT_GLOBAL);
        }
        ret = TR_FR_SPEEDLIMIT;
    }

    return ret;
}

static uint64_t
loadRatioLimits (tr_variant * dict, tr_torrent * tor)
{
    tr_variant * d;
    uint64_t ret = 0;

    if (tr_variantDictFindDict (dict, KEY_RATIOLIMIT, &d))
    {
        int64_t i;
        double dratio;
        if (tr_variantDictFindReal (d, KEY_RATIOLIMIT_RATIO, &dratio))
            tr_torrentSetRatioLimit (tor, dratio);
        if (tr_variantDictFindInt (d, KEY_RATIOLIMIT_MODE, &i))
            tr_torrentSetRatioMode (tor, i);
      ret = TR_FR_RATIOLIMIT;
    }

    return ret;
}

static uint64_t
loadIdleLimits (tr_variant * dict, tr_torrent * tor)
{
    tr_variant * d;
    uint64_t ret = 0;

    if (tr_variantDictFindDict (dict, KEY_IDLELIMIT, &d))
    {
        int64_t i;
        int64_t imin;
        if (tr_variantDictFindInt (d, KEY_IDLELIMIT_MINS, &imin))
            tr_torrentSetIdleLimit (tor, imin);
        if (tr_variantDictFindInt (d, KEY_IDLELIMIT_MODE, &i))
            tr_torrentSetIdleMode (tor, i);
      ret = TR_FR_IDLELIMIT;
    }

    return ret;
}
/***
****
***/

static void
bitfieldToBenc (const tr_bitfield * b, tr_variant * benc)
{
    if (tr_bitfieldHasAll (b))
        tr_variantInitStr (benc, "all", 3);
    else if (tr_bitfieldHasNone (b))
        tr_variantInitStr (benc, "none", 4);
    else {
        size_t byte_count = 0;
        uint8_t * raw = tr_bitfieldGetRaw (b, &byte_count);
        tr_variantInitRaw (benc, raw, byte_count);
        tr_free (raw);
    }
}


static void
saveProgress (tr_variant * dict, tr_torrent * tor)
{
    tr_variant * l;
    tr_variant * prog;
    tr_file_index_t fi;
    const tr_info * inf = tr_torrentInfo (tor);
    const time_t now = tr_time ();

    prog = tr_variantDictAddDict (dict, KEY_PROGRESS, 3);

    /* add the file/piece check timestamps... */
    l = tr_variantDictAddList (prog, KEY_PROGRESS_CHECKTIME, inf->fileCount);
    for (fi=0; fi<inf->fileCount; ++fi)
    {
        const tr_piece * p;
        const tr_piece * pend;
        time_t oldest_nonzero = now;
        time_t newest = 0;
        bool has_zero = false;
        const time_t mtime = tr_torrentGetFileMTime (tor, fi);
        const tr_file * f = &inf->files[fi];

        /* get the oldest and newest nonzero timestamps for pieces in this file */
        for (p=&inf->pieces[f->firstPiece], pend=&inf->pieces[f->lastPiece]; p!=pend; ++p)
        {
            if (!p->timeChecked)
                has_zero = true;
            else if (oldest_nonzero > p->timeChecked)
                oldest_nonzero = p->timeChecked;
            if (newest < p->timeChecked)
                newest = p->timeChecked;
        }

        /* If some of a file's pieces have been checked more recently than
           the file's mtime, and some lest recently, then that file will
           have a list containing timestamps for each piece.

           However, the most common use case is that the file doesn't change
           after it's downloaded. To reduce overhead in the .resume file,
           only a single timestamp is saved for the file if *all* or *none*
           of the pieces were tested more recently than the file's mtime. */

        if (!has_zero && (mtime <= oldest_nonzero)) /* all checked */
            tr_variantListAddInt (l, oldest_nonzero);
        else if (newest < mtime) /* none checked */
            tr_variantListAddInt (l, newest);
        else { /* some are checked, some aren't... so list piece by piece */
            const int offset = oldest_nonzero - 1;
            tr_variant * ll = tr_variantListAddList (l, 2 + f->lastPiece - f->firstPiece);
            tr_variantListAddInt (ll, offset);
            for (p=&inf->pieces[f->firstPiece], pend=&inf->pieces[f->lastPiece]+1; p!=pend; ++p)
                tr_variantListAddInt (ll, p->timeChecked ? p->timeChecked - offset : 0);
        }
    }

    /* add the progress */
    if (tor->completeness == TR_SEED)
        tr_variantDictAddStr (prog, KEY_PROGRESS_HAVE, "all");

    /* add the blocks bitfield */
    bitfieldToBenc (&tor->completion.blockBitfield,
                    tr_variantDictAdd (prog, KEY_PROGRESS_BLOCKS, KEY_PROGRESS_BLOCKS_STRLEN));
}

static uint64_t
loadProgress (tr_variant * dict, tr_torrent * tor)
{
    size_t i, n;
    uint64_t ret = 0;
    tr_variant * prog;
    const tr_info * inf = tr_torrentInfo (tor);

    for (i=0, n=inf->pieceCount; i<n; ++i)
        inf->pieces[i].timeChecked = 0;

    if (tr_variantDictFindDict (dict, KEY_PROGRESS, &prog))
    {
        const char * err;
        const char * str;
        const uint8_t * raw;
        size_t rawlen;
        tr_variant * l;
        tr_variant * b;
        struct tr_bitfield blocks = TR_BITFIELD_INIT;

        if (tr_variantDictFindList (prog, KEY_PROGRESS_CHECKTIME, &l))
        {
            /* per-piece timestamps were added in 2.20.

               If some of a file's pieces have been checked more recently than
               the file's mtime, and some lest recently, then that file will
               have a list containing timestamps for each piece.

               However, the most common use case is that the file doesn't change
               after it's downloaded. To reduce overhead in the .resume file,
               only a single timestamp is saved for the file if *all* or *none*
               of the pieces were tested more recently than the file's mtime. */

            tr_file_index_t fi;

            for (fi=0; fi<inf->fileCount; ++fi)
            {
                tr_variant * b = tr_variantListChild (l, fi);
                const tr_file * f = &inf->files[fi];
                tr_piece * p = &inf->pieces[f->firstPiece];
                const tr_piece * pend = &inf->pieces[f->lastPiece]+1;

                if (tr_variantIsInt (b))
                {
                    int64_t t;
                    tr_variantGetInt (b, &t);
                    for (; p!=pend; ++p)
                        p->timeChecked = (time_t)t;
                }
                else if (tr_variantIsList (b))
                {
                    int i = 0;
                    int64_t offset = 0;
                    const int pieces = f->lastPiece + 1 - f->firstPiece;

                    tr_variantGetInt (tr_variantListChild (b, 0), &offset);

                    for (i=0; i<pieces; ++i)
                    {
                        int64_t t = 0;
                        tr_variantGetInt (tr_variantListChild (b, i+1), &t);
                        inf->pieces[f->firstPiece+i].timeChecked = (time_t)(t ? t + offset : 0);
                    }
                }
            }
        }
        else if (tr_variantDictFindList (prog, KEY_PROGRESS_MTIMES, &l))
        {
            tr_file_index_t fi;

            /* Before 2.20, we stored the files' mtimes in the .resume file.
               When loading the .resume file, a torrent's file would be flagged
               as untested if its stored mtime didn't match its real mtime. */

            for (fi=0; fi<inf->fileCount; ++fi)
            {
                int64_t t;

                if (tr_variantGetInt (tr_variantListChild (l, fi), &t))
                {
                    const tr_file * f = &inf->files[fi];
                    tr_piece * p = &inf->pieces[f->firstPiece];
                    const tr_piece * pend = &inf->pieces[f->lastPiece];
                    const time_t mtime = tr_torrentGetFileMTime (tor, fi);
                    const time_t timeChecked = mtime==t ? mtime : 0;

                    for (; p!=pend; ++p)
                        p->timeChecked = timeChecked;
                }
            }
        }

        err = NULL;
        tr_bitfieldConstruct (&blocks, tor->blockCount);

        if ((b = tr_variantDictFind (prog, KEY_PROGRESS_BLOCKS)))
        {
            size_t buflen;
            const uint8_t * buf;

            if (!tr_variantGetRaw (b, &buf, &buflen))
                err = "Invalid value for \"blocks\"";
            else if ((buflen == 3) && !memcmp (buf, "all", 3))
                tr_bitfieldSetHasAll (&blocks);
            else if ((buflen == 4) && !memcmp (buf, "none", 4))
                tr_bitfieldSetHasNone (&blocks);
            else
                tr_bitfieldSetRaw (&blocks, buf, buflen, true);
        }
        else if (tr_variantDictFindStr (prog, KEY_PROGRESS_HAVE, &str, NULL))
        {
            if (!strcmp (str, "all"))
                tr_bitfieldSetHasAll (&blocks);
            else
                err = "Invalid value for HAVE";
        }
        else if (tr_variantDictFindRaw (prog, KEY_PROGRESS_BITFIELD, &raw, &rawlen))
        {
            tr_bitfieldSetRaw (&blocks, raw, rawlen, true);
        }
        else err = "Couldn't find 'pieces' or 'have' or 'bitfield'";

        if (err != NULL)
            tr_tordbg (tor, "Torrent needs to be verified - %s", err);
        else
            tr_cpBlockInit (&tor->completion, &blocks);

        tr_bitfieldDestruct (&blocks);
        ret = TR_FR_PROGRESS;
    }

    return ret;
}

/***
****
***/

void
tr_torrentSaveResume (tr_torrent * tor)
{
    int err;
    tr_variant top;
    char * filename;

    if (!tr_isTorrent (tor))
        return;

    tr_variantInitDict (&top, 50); /* arbitrary "big enough" number */
    tr_variantDictAddInt (&top, KEY_TIME_SEEDING, tor->secondsSeeding);
    tr_variantDictAddInt (&top, KEY_TIME_DOWNLOADING, tor->secondsDownloading);
    tr_variantDictAddInt (&top, KEY_ACTIVITY_DATE, tor->activityDate);
    tr_variantDictAddInt (&top, KEY_ADDED_DATE, tor->addedDate);
    tr_variantDictAddInt (&top, KEY_CORRUPT, tor->corruptPrev + tor->corruptCur);
    tr_variantDictAddInt (&top, KEY_DONE_DATE, tor->doneDate);
    tr_variantDictAddStr (&top, KEY_DOWNLOAD_DIR, tor->downloadDir);
    if (tor->incompleteDir != NULL)
        tr_variantDictAddStr (&top, KEY_INCOMPLETE_DIR, tor->incompleteDir);
    tr_variantDictAddInt (&top, KEY_DOWNLOADED, tor->downloadedPrev + tor->downloadedCur);
    tr_variantDictAddInt (&top, KEY_UPLOADED, tor->uploadedPrev + tor->uploadedCur);
    tr_variantDictAddInt (&top, KEY_MAX_PEERS, tor->maxConnectedPeers);
    tr_variantDictAddInt (&top, KEY_BANDWIDTH_PRIORITY, tr_torrentGetPriority (tor));
    tr_variantDictAddBool (&top, KEY_PAUSED, !tor->isRunning);
    savePeers (&top, tor);
    if (tr_torrentHasMetadata (tor))
    {
        saveFilePriorities (&top, tor);
        saveDND (&top, tor);
        saveProgress (&top, tor);
    }
    saveSpeedLimits (&top, tor);
    saveRatioLimits (&top, tor);
    saveIdleLimits (&top, tor);

    filename = getResumeFilename (tor);
    if ((err = tr_variantToFile (&top, TR_VARIANT_FMT_BENC, filename)))
        tr_torrentSetLocalError (tor, "Unable to save resume file: %s", tr_strerror (err));
    tr_free (filename);

    tr_variantFree (&top);
}

static uint64_t
loadFromFile (tr_torrent * tor, uint64_t fieldsToLoad)
{
    size_t len;
    int64_t  i;
    const char * str;
    char * filename;
    tr_variant top;
    bool boolVal;
    uint64_t fieldsLoaded = 0;
    const bool wasDirty = tor->isDirty;

    assert (tr_isTorrent (tor));

    filename = getResumeFilename (tor);

    if (tr_variantFromFile (&top, TR_VARIANT_FMT_BENC, filename))
    {
        tr_tordbg (tor, "Couldn't read \"%s\"", filename);

        tr_free (filename);
        return fieldsLoaded;
    }

    tr_tordbg (tor, "Read resume file \"%s\"", filename);

    if ((fieldsToLoad & TR_FR_CORRUPT)
      && tr_variantDictFindInt (&top, KEY_CORRUPT, &i))
    {
        tor->corruptPrev = i;
        fieldsLoaded |= TR_FR_CORRUPT;
    }

    if ((fieldsToLoad & (TR_FR_PROGRESS | TR_FR_DOWNLOAD_DIR))
      && (tr_variantDictFindStr (&top, KEY_DOWNLOAD_DIR, &str, &len))
      && (str && *str))
    {
        tr_free (tor->downloadDir);
        tor->downloadDir = tr_strndup (str, len);
        fieldsLoaded |= TR_FR_DOWNLOAD_DIR;
    }

    if ((fieldsToLoad & (TR_FR_PROGRESS | TR_FR_INCOMPLETE_DIR))
      && (tr_variantDictFindStr (&top, KEY_INCOMPLETE_DIR, &str, &len))
      && (str && *str))
    {
        tr_free (tor->incompleteDir);
        tor->incompleteDir = tr_strndup (str, len);
        fieldsLoaded |= TR_FR_INCOMPLETE_DIR;
    }

    if ((fieldsToLoad & TR_FR_DOWNLOADED)
      && tr_variantDictFindInt (&top, KEY_DOWNLOADED, &i))
    {
        tor->downloadedPrev = i;
        fieldsLoaded |= TR_FR_DOWNLOADED;
    }

    if ((fieldsToLoad & TR_FR_UPLOADED)
      && tr_variantDictFindInt (&top, KEY_UPLOADED, &i))
    {
        tor->uploadedPrev = i;
        fieldsLoaded |= TR_FR_UPLOADED;
    }

    if ((fieldsToLoad & TR_FR_MAX_PEERS)
      && tr_variantDictFindInt (&top, KEY_MAX_PEERS, &i))
    {
        tor->maxConnectedPeers = i;
        fieldsLoaded |= TR_FR_MAX_PEERS;
    }

    if ((fieldsToLoad & TR_FR_RUN)
      && tr_variantDictFindBool (&top, KEY_PAUSED, &boolVal))
    {
        tor->isRunning = !boolVal;
        fieldsLoaded |= TR_FR_RUN;
    }

    if ((fieldsToLoad & TR_FR_ADDED_DATE)
      && tr_variantDictFindInt (&top, KEY_ADDED_DATE, &i))
    {
        tor->addedDate = i;
        fieldsLoaded |= TR_FR_ADDED_DATE;
    }

    if ((fieldsToLoad & TR_FR_DONE_DATE)
      && tr_variantDictFindInt (&top, KEY_DONE_DATE, &i))
    {
        tor->doneDate = i;
        fieldsLoaded |= TR_FR_DONE_DATE;
    }

    if ((fieldsToLoad & TR_FR_ACTIVITY_DATE)
      && tr_variantDictFindInt (&top, KEY_ACTIVITY_DATE, &i))
    {
        tr_torrentSetActivityDate (tor, i);
        fieldsLoaded |= TR_FR_ACTIVITY_DATE;
    }

    if ((fieldsToLoad & TR_FR_TIME_SEEDING)
      && tr_variantDictFindInt (&top, KEY_TIME_SEEDING, &i))
    {
        tor->secondsSeeding = i;
        fieldsLoaded |= TR_FR_TIME_SEEDING;
    }

    if ((fieldsToLoad & TR_FR_TIME_DOWNLOADING)
      && tr_variantDictFindInt (&top, KEY_TIME_DOWNLOADING, &i))
    {
        tor->secondsDownloading = i;
        fieldsLoaded |= TR_FR_TIME_DOWNLOADING;
    }

    if ((fieldsToLoad & TR_FR_BANDWIDTH_PRIORITY)
      && tr_variantDictFindInt (&top, KEY_BANDWIDTH_PRIORITY, &i)
      && tr_isPriority (i))
    {
        tr_torrentSetPriority (tor, i);
        fieldsLoaded |= TR_FR_BANDWIDTH_PRIORITY;
    }

    if (fieldsToLoad & TR_FR_PEERS)
        fieldsLoaded |= loadPeers (&top, tor);

    if (fieldsToLoad & TR_FR_FILE_PRIORITIES)
        fieldsLoaded |= loadFilePriorities (&top, tor);

    if (fieldsToLoad & TR_FR_PROGRESS)
        fieldsLoaded |= loadProgress (&top, tor);

    if (fieldsToLoad & TR_FR_DND)
        fieldsLoaded |= loadDND (&top, tor);

    if (fieldsToLoad & TR_FR_SPEEDLIMIT)
        fieldsLoaded |= loadSpeedLimits (&top, tor);

    if (fieldsToLoad & TR_FR_RATIOLIMIT)
        fieldsLoaded |= loadRatioLimits (&top, tor);

    if (fieldsToLoad & TR_FR_IDLELIMIT)
        fieldsLoaded |= loadIdleLimits (&top, tor);

    /* loading the resume file triggers of a lot of changes,
     * but none of them needs to trigger a re-saving of the
     * same resume information... */
    tor->isDirty = wasDirty;

    tr_variantFree (&top);
    tr_free (filename);
    return fieldsLoaded;
}

static uint64_t
setFromCtor (tr_torrent * tor, uint64_t fields, const tr_ctor * ctor, int mode)
{
    uint64_t ret = 0;

    if (fields & TR_FR_RUN)
    {
        bool isPaused;
        if (!tr_ctorGetPaused (ctor, mode, &isPaused))
        {
            tor->isRunning = !isPaused;
            ret |= TR_FR_RUN;
        }
    }

    if (fields & TR_FR_MAX_PEERS)
        if (!tr_ctorGetPeerLimit (ctor, mode, &tor->maxConnectedPeers))
            ret |= TR_FR_MAX_PEERS;

    if (fields & TR_FR_DOWNLOAD_DIR)
    {
        const char * path;
        if (!tr_ctorGetDownloadDir (ctor, mode, &path) && path && *path)
        {
            ret |= TR_FR_DOWNLOAD_DIR;
            tr_free (tor->downloadDir);
            tor->downloadDir = tr_strdup (path);
        }
    }

    return ret;
}

static uint64_t
useManditoryFields (tr_torrent * tor, uint64_t fields, const tr_ctor * ctor)
{
    return setFromCtor (tor, fields, ctor, TR_FORCE);
}

static uint64_t
useFallbackFields (tr_torrent * tor, uint64_t fields, const tr_ctor * ctor)
{
    return setFromCtor (tor, fields, ctor, TR_FALLBACK);
}

uint64_t
tr_torrentLoadResume (tr_torrent *    tor,
                      uint64_t        fieldsToLoad,
                      const tr_ctor * ctor)
{
    uint64_t ret = 0;

    assert (tr_isTorrent (tor));

    ret |= useManditoryFields (tor, fieldsToLoad, ctor);
    fieldsToLoad &= ~ret;
    ret |= loadFromFile (tor, fieldsToLoad);
    fieldsToLoad &= ~ret;
    ret |= useFallbackFields (tor, fieldsToLoad, ctor);

    return ret;
}

void
tr_torrentRemoveResume (const tr_torrent * tor)
{
    char * filename = getResumeFilename (tor);
    unlink (filename);
    tr_free (filename);
}
