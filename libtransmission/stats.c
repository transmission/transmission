/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "session.h"
#include "log.h"
#include "platform.h" /* tr_sessionGetConfigDir() */
#include "stats.h"
#include "utils.h" /* tr_buildPath */
#include "variant.h"

/***
****
***/

struct tr_session_stats const TR_SESSION_STATS_INIT =
{
    .ratio = 0.0F,
    .uploadedBytes = 0,
    .downloadedBytes = 0,
    .filesAdded = 0,
    .sessionCount = 0,
    .secondsActive = 0
};

/** @brief Opaque, per-session data structure for bandwidth use statistics */
struct tr_stats_handle
{
    tr_session_stats single;
    tr_session_stats old;
    time_t startTime;
    bool isDirty;
};

static char* getOldFilename(tr_session const* session)
{
    return tr_buildPath(tr_sessionGetConfigDir(session), "stats.benc", NULL);
}

static char* getFilename(tr_session const* session)
{
    return tr_buildPath(tr_sessionGetConfigDir(session), "stats.json", NULL);
}

static void loadCumulativeStats(tr_session const* session, tr_session_stats* setme)
{
    tr_variant top;
    char* filename;
    bool loaded = false;

    filename = getFilename(session);
    loaded = tr_variantFromFile(&top, TR_VARIANT_FMT_JSON, filename, NULL);
    tr_free(filename);

    if (!loaded)
    {
        filename = getOldFilename(session);
        loaded = tr_variantFromFile(&top, TR_VARIANT_FMT_BENC, filename, NULL);
        tr_free(filename);
    }

    if (loaded)
    {
        int64_t i;

        if (tr_variantDictFindInt(&top, TR_KEY_downloaded_bytes, &i))
        {
            setme->downloadedBytes = (uint64_t)i;
        }

        if (tr_variantDictFindInt(&top, TR_KEY_files_added, &i))
        {
            setme->filesAdded = (uint64_t)i;
        }

        if (tr_variantDictFindInt(&top, TR_KEY_seconds_active, &i))
        {
            setme->secondsActive = (uint64_t)i;
        }

        if (tr_variantDictFindInt(&top, TR_KEY_session_count, &i))
        {
            setme->sessionCount = (uint64_t)i;
        }

        if (tr_variantDictFindInt(&top, TR_KEY_uploaded_bytes, &i))
        {
            setme->uploadedBytes = (uint64_t)i;
        }

        tr_variantFree(&top);
    }
}

static void saveCumulativeStats(tr_session const* session, tr_session_stats const* s)
{
    char* filename;
    tr_variant top;

    tr_variantInitDict(&top, 5);
    tr_variantDictAddInt(&top, TR_KEY_downloaded_bytes, s->downloadedBytes);
    tr_variantDictAddInt(&top, TR_KEY_files_added, s->filesAdded);
    tr_variantDictAddInt(&top, TR_KEY_seconds_active, s->secondsActive);
    tr_variantDictAddInt(&top, TR_KEY_session_count, s->sessionCount);
    tr_variantDictAddInt(&top, TR_KEY_uploaded_bytes, s->uploadedBytes);

    filename = getFilename(session);
    tr_logAddDeep(__FILE__, __LINE__, NULL, "Saving stats to \"%s\"", filename);
    tr_variantToFile(&top, TR_VARIANT_FMT_JSON, filename);

    tr_free(filename);
    tr_variantFree(&top);
}

/***
****
***/

void tr_statsInit(tr_session* session)
{
    struct tr_stats_handle* stats = tr_new0(struct tr_stats_handle, 1);

    loadCumulativeStats(session, &stats->old);
    stats->single.sessionCount = 1;
    stats->startTime = tr_time();
    session->sessionStats = stats;
}

static struct tr_stats_handle* getStats(tr_session const* session)
{
    return session != NULL ? session->sessionStats : NULL;
}

void tr_statsSaveDirty(tr_session* session)
{
    struct tr_stats_handle* h = getStats(session);

    if (h != NULL && h->isDirty)
    {
        tr_session_stats cumulative = TR_SESSION_STATS_INIT;
        tr_sessionGetCumulativeStats(session, &cumulative);
        saveCumulativeStats(session, &cumulative);
        h->isDirty = false;
    }
}

void tr_statsClose(tr_session* session)
{
    tr_statsSaveDirty(session);

    tr_free(session->sessionStats);
    session->sessionStats = NULL;
}

/***
****
***/

static void updateRatio(tr_session_stats* setme)
{
    setme->ratio = tr_getRatio(setme->uploadedBytes, setme->downloadedBytes);
}

static void addStats(tr_session_stats* setme, tr_session_stats const* a, tr_session_stats const* b)
{
    setme->uploadedBytes = a->uploadedBytes + b->uploadedBytes;
    setme->downloadedBytes = a->downloadedBytes + b->downloadedBytes;
    setme->filesAdded = a->filesAdded + b->filesAdded;
    setme->sessionCount = a->sessionCount + b->sessionCount;
    setme->secondsActive = a->secondsActive + b->secondsActive;
    updateRatio(setme);
}

void tr_sessionGetStats(tr_session const* session, tr_session_stats* setme)
{
    struct tr_stats_handle const* stats = getStats(session);

    if (stats != NULL)
    {
        *setme = stats->single;
        setme->secondsActive = tr_time() - stats->startTime;
        updateRatio(setme);
    }
}

void tr_sessionGetCumulativeStats(tr_session const* session, tr_session_stats* setme)
{
    struct tr_stats_handle const* stats = getStats(session);
    tr_session_stats current = TR_SESSION_STATS_INIT;

    if (stats != NULL)
    {
        tr_sessionGetStats(session, &current);
        addStats(setme, &stats->old, &current);
    }
}

void tr_sessionClearStats(tr_session* session)
{
    tr_session_stats zero;

    zero.uploadedBytes = 0;
    zero.downloadedBytes = 0;
    zero.ratio = TR_RATIO_NA;
    zero.filesAdded = 0;
    zero.sessionCount = 0;
    zero.secondsActive = 0;

    session->sessionStats->isDirty = true;
    session->sessionStats->single = session->sessionStats->old = zero;
    session->sessionStats->startTime = tr_time();
}

/**
***
**/

void tr_statsAddUploaded(tr_session* session, uint32_t bytes)
{
    struct tr_stats_handle* s;

    if ((s = getStats(session)) != NULL)
    {
        s->single.uploadedBytes += bytes;
        s->isDirty = true;
    }
}

void tr_statsAddDownloaded(tr_session* session, uint32_t bytes)
{
    struct tr_stats_handle* s;

    if ((s = getStats(session)) != NULL)
    {
        s->single.downloadedBytes += bytes;
        s->isDirty = true;
    }
}

void tr_statsFileCreated(tr_session* session)
{
    struct tr_stats_handle* s;

    if ((s = getStats(session)) != NULL)
    {
        s->single.filesAdded++;
    }
}
