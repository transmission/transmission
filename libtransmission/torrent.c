/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <errno.h> /* EINVAL */
#include <signal.h> /* signal () */

#ifndef _WIN32
 #include <sys/wait.h> /* wait () */
 #include <unistd.h> /* fork (), execvp (), _exit () */
#else
 #include <windows.h> /* CreateProcess (), GetLastError () */
#endif

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <string.h> /* memcmp */
#include <stdlib.h> /* qsort */
#include <limits.h> /* INT_MAX */

#include <event2/util.h> /* evutil_vsnprintf () */

#include "transmission.h"
#include "announcer.h"
#include "bandwidth.h"
#include "cache.h"
#include "completion.h"
#include "crypto-utils.h" /* for tr_sha1 */
#include "error.h"
#include "fdlimit.h" /* tr_fdTorrentClose */
#include "file.h"
#include "inout.h" /* tr_ioTestPiece () */
#include "log.h"
#include "magnet.h"
#include "metainfo.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
#include "peer-mgr.h"
#include "platform.h" /* TR_PATH_DELIMITER_STR */
#include "ptrarray.h"
#include "resume.h"
#include "session.h"
#include "torrent.h"
#include "torrent-magnet.h"
#include "trevent.h" /* tr_runInEventThread () */
#include "utils.h"
#include "variant.h"
#include "verify.h"
#include "version.h"

/***
****
***/

#define tr_deeplog_tor(tor, ...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        tr_logAddDeep (__FILE__, __LINE__, tr_torrentName (tor), __VA_ARGS__); \
    } \
  while (0)

/***
****
***/

const char *
tr_torrentName (const tr_torrent * tor)
{
  return tor ? tor->info.name : "";
}

int
tr_torrentId (const tr_torrent * tor)
{
  return tor ? tor->uniqueId : -1;
}

tr_torrent*
tr_torrentFindFromId (tr_session * session, int id)
{
  tr_torrent * tor = NULL;

  while ((tor = tr_torrentNext (session, tor)))
    if (tor->uniqueId == id)
      return tor;

  return NULL;
}

tr_torrent*
tr_torrentFindFromHashString (tr_session *  session, const char * str)
{
  tr_torrent * tor = NULL;

  while ((tor = tr_torrentNext (session, tor)))
    if (!evutil_ascii_strcasecmp (str, tor->info.hashString))
      return tor;

  return NULL;
}

tr_torrent*
tr_torrentFindFromHash (tr_session * session, const uint8_t * torrentHash)
{
  tr_torrent * tor = NULL;

  while ((tor = tr_torrentNext (session, tor)))
    if (*tor->info.hash == *torrentHash)
      if (!memcmp (tor->info.hash, torrentHash, SHA_DIGEST_LENGTH))
        return tor;

  return NULL;
}

tr_torrent*
tr_torrentFindFromMagnetLink (tr_session * session, const char * magnet)
{
  tr_magnet_info * info;
  tr_torrent * tor = NULL;

  if ((info = tr_magnetParse (magnet)))
    {
      tor = tr_torrentFindFromHash (session, info->hash);
      tr_magnetFree (info);
    }

  return tor;
}

tr_torrent*
tr_torrentFindFromObfuscatedHash (tr_session * session,
                                  const uint8_t * obfuscatedTorrentHash)
{
  tr_torrent * tor = NULL;

  while ((tor = tr_torrentNext (session, tor)))
    if (!memcmp (tor->obfuscatedHash, obfuscatedTorrentHash, SHA_DIGEST_LENGTH))
      return tor;

  return NULL;
}

bool
tr_torrentIsPieceTransferAllowed (const tr_torrent  * tor,
                                  tr_direction        direction)
{
  bool allowed = true;
  unsigned int limit;

  assert (tr_isTorrent (tor));
  assert (tr_isDirection (direction));

  if (tr_torrentUsesSpeedLimit (tor, direction))
    if (tr_torrentGetSpeedLimit_Bps (tor, direction) <= 0)
      allowed = false;

  if (tr_torrentUsesSessionLimits (tor))
    if (tr_sessionGetActiveSpeedLimit_Bps (tor->session, direction, &limit))
      if (limit <= 0)
        allowed = false;

  return allowed;
}

/***
****
***/

static void
tr_torrentUnsetPeerId (tr_torrent * tor)
{
  /* triggers a rebuild next time tr_torrentGetPeerId() is called */
  *tor->peer_id = '\0';
}

static int
peerIdTTL (const tr_torrent * tor)
{
  int ttl;

  if (!tor->peer_id_creation_time)
    ttl = 0;
  else
    ttl = (int)difftime(tor->peer_id_creation_time+(tor->session->peer_id_ttl_hours*3600), tr_time());

  return ttl;
}

const unsigned char *
tr_torrentGetPeerId (tr_torrent * tor)
{
  bool needs_new_peer_id = false;

  if (!*tor->peer_id)
    needs_new_peer_id = true;

  if (!needs_new_peer_id)
    if (!tr_torrentIsPrivate (tor))
      if (peerIdTTL (tor) <= 0)
        needs_new_peer_id = true;

  if (needs_new_peer_id)
    {
      tr_peerIdInit (tor->peer_id);
      tor->peer_id_creation_time = tr_time ();
    }

  return tor->peer_id;
}
/***
****  PER-TORRENT UL / DL SPEEDS
***/

void
tr_torrentSetSpeedLimit_Bps (tr_torrent * tor, tr_direction dir, unsigned int Bps)
{
  assert (tr_isTorrent (tor));
  assert (tr_isDirection (dir));

  if (tr_bandwidthSetDesiredSpeed_Bps (&tor->bandwidth, dir, Bps))
    tr_torrentSetDirty (tor);
}
void
tr_torrentSetSpeedLimit_KBps (tr_torrent * tor, tr_direction dir, unsigned int KBps)
{
  tr_torrentSetSpeedLimit_Bps (tor, dir, toSpeedBytes (KBps));
}

unsigned int
tr_torrentGetSpeedLimit_Bps (const tr_torrent * tor, tr_direction dir)
{
  assert (tr_isTorrent (tor));
  assert (tr_isDirection (dir));

  return tr_bandwidthGetDesiredSpeed_Bps (&tor->bandwidth, dir);
}
unsigned int
tr_torrentGetSpeedLimit_KBps (const tr_torrent * tor, tr_direction dir)
{
  assert (tr_isTorrent (tor));
  assert (tr_isDirection (dir));

  return toSpeedKBps (tr_torrentGetSpeedLimit_Bps (tor, dir));
}

void
tr_torrentUseSpeedLimit (tr_torrent * tor, tr_direction dir, bool do_use)
{
  assert (tr_isTorrent (tor));
  assert (tr_isDirection (dir));

  if (tr_bandwidthSetLimited (&tor->bandwidth, dir, do_use))
    tr_torrentSetDirty (tor);
}

bool
tr_torrentUsesSpeedLimit (const tr_torrent * tor, tr_direction dir)
{
  assert (tr_isTorrent (tor));

  return tr_bandwidthIsLimited (&tor->bandwidth, dir);
}

void
tr_torrentUseSessionLimits (tr_torrent * tor, bool doUse)
{
  bool changed;

  assert (tr_isTorrent (tor));

  changed = tr_bandwidthHonorParentLimits (&tor->bandwidth, TR_UP, doUse);
  changed |= tr_bandwidthHonorParentLimits (&tor->bandwidth, TR_DOWN, doUse);

  if (changed)
    tr_torrentSetDirty (tor);
}

bool
tr_torrentUsesSessionLimits (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tr_bandwidthAreParentLimitsHonored (&tor->bandwidth, TR_UP);
}

/***
****
***/

void
tr_torrentSetRatioMode (tr_torrent *  tor, tr_ratiolimit mode)
{
  assert (tr_isTorrent (tor));
  assert (mode==TR_RATIOLIMIT_GLOBAL || mode==TR_RATIOLIMIT_SINGLE || mode==TR_RATIOLIMIT_UNLIMITED);

  if (mode != tor->ratioLimitMode)
    {
      tor->ratioLimitMode = mode;

      tr_torrentSetDirty (tor);
    }
}

tr_ratiolimit
tr_torrentGetRatioMode (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tor->ratioLimitMode;
}

void
tr_torrentSetRatioLimit (tr_torrent * tor, double desiredRatio)
{
  assert (tr_isTorrent (tor));

  if ((int)(desiredRatio*100.0) != (int)(tor->desiredRatio*100.0))
    {
      tor->desiredRatio = desiredRatio;

      tr_torrentSetDirty (tor);
    }
}

double
tr_torrentGetRatioLimit (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tor->desiredRatio;
}

bool
tr_torrentGetSeedRatio (const tr_torrent * tor, double * ratio)
{
  bool isLimited;

  assert (tr_isTorrent (tor));

  switch (tr_torrentGetRatioMode (tor))
    {
      case TR_RATIOLIMIT_SINGLE:
        isLimited = true;
        if (ratio)
          *ratio = tr_torrentGetRatioLimit (tor);
        break;

      case TR_RATIOLIMIT_GLOBAL:
        isLimited = tr_sessionIsRatioLimited (tor->session);
        if (isLimited && ratio)
          *ratio = tr_sessionGetRatioLimit (tor->session);
        break;

      default: /* TR_RATIOLIMIT_UNLIMITED */
        isLimited = false;
        break;
    }

  return isLimited;
}

/* returns true if the seed ratio applies --
 * it applies if the torrent's a seed AND it has a seed ratio set */
static bool
tr_torrentGetSeedRatioBytes (const tr_torrent  * tor,
                             uint64_t          * setmeLeft,
                             uint64_t          * setmeGoal)
{
  double seedRatio;
  bool seedRatioApplies = false;

  assert (tr_isTorrent (tor));

  if (tr_torrentGetSeedRatio (tor, &seedRatio))
    {
      const uint64_t u = tor->uploadedCur + tor->uploadedPrev;
      const uint64_t d = tor->downloadedCur + tor->downloadedPrev;
      const uint64_t baseline = d ? d : tr_cpSizeWhenDone (&tor->completion);
      const uint64_t goal = baseline * seedRatio;
      if (setmeLeft) *setmeLeft = goal > u ? goal - u : 0;
      if (setmeGoal) *setmeGoal = goal;
      seedRatioApplies = tr_torrentIsSeed (tor);
    }

  return seedRatioApplies;
}

static bool
tr_torrentIsSeedRatioDone (const tr_torrent * tor)
{
  uint64_t bytesLeft;
  return tr_torrentGetSeedRatioBytes (tor, &bytesLeft, NULL) && !bytesLeft;
}

/***
****
***/

void
tr_torrentSetIdleMode (tr_torrent *  tor, tr_idlelimit mode)
{
  assert (tr_isTorrent (tor));
  assert (mode==TR_IDLELIMIT_GLOBAL || mode==TR_IDLELIMIT_SINGLE || mode==TR_IDLELIMIT_UNLIMITED);

  if (mode != tor->idleLimitMode)
    {
      tor->idleLimitMode = mode;

      tr_torrentSetDirty (tor);
    }
}

tr_idlelimit
tr_torrentGetIdleMode (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tor->idleLimitMode;
}

void
tr_torrentSetIdleLimit (tr_torrent * tor, uint16_t idleMinutes)
{
  assert (tr_isTorrent (tor));

  if (idleMinutes > 0)
    {
      tor->idleLimitMinutes = idleMinutes;

      tr_torrentSetDirty (tor);
    }
}

uint16_t
tr_torrentGetIdleLimit (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tor->idleLimitMinutes;
}

bool
tr_torrentGetSeedIdle (const tr_torrent * tor, uint16_t * idleMinutes)
{
  bool isLimited;

  switch (tr_torrentGetIdleMode (tor))
    {
      case TR_IDLELIMIT_SINGLE:
        isLimited = true;
        if (idleMinutes != NULL)
          *idleMinutes = tr_torrentGetIdleLimit (tor);
        break;

      case TR_IDLELIMIT_GLOBAL:
        isLimited = tr_sessionIsIdleLimited (tor->session);
        if (isLimited && idleMinutes)
          *idleMinutes = tr_sessionGetIdleLimit (tor->session);
        break;

      default: /* TR_IDLELIMIT_UNLIMITED */
        isLimited = false;
        break;
    }

  return isLimited;
}

static bool
tr_torrentIsSeedIdleLimitDone (tr_torrent * tor)
{
  uint16_t idleMinutes;
  return tr_torrentGetSeedIdle (tor, &idleMinutes)
    && difftime (tr_time (), MAX (tor->startDate, tor->activityDate)) >= idleMinutes * 60u;
}

/***
****
***/

void
tr_torrentCheckSeedLimit (tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  if (!tor->isRunning || tor->isStopping || !tr_torrentIsSeed (tor))
    return;

  /* if we're seeding and reach our seed ratio limit, stop the torrent */
  if (tr_torrentIsSeedRatioDone (tor))
    {
      tr_logAddTorInfo (tor, "%s", "Seed ratio reached; pausing torrent");

      tor->isStopping = true;

      /* maybe notify the client */
      if (tor->ratio_limit_hit_func != NULL)
        tor->ratio_limit_hit_func (tor, tor->ratio_limit_hit_func_user_data);
    }
  /* if we're seeding and reach our inactiviy limit, stop the torrent */
  else if (tr_torrentIsSeedIdleLimitDone (tor))
    {
      tr_logAddTorInfo (tor, "%s", "Seeding idle limit reached; pausing torrent");

      tor->isStopping = true;
      tor->finishedSeedingByIdle = true;

      /* maybe notify the client */
      if (tor->idle_limit_hit_func != NULL)
        tor->idle_limit_hit_func (tor, tor->idle_limit_hit_func_user_data);
    }
}

/***
****
***/

void
tr_torrentSetLocalError (tr_torrent * tor, const char * fmt, ...)
{
  va_list ap;

  assert (tr_isTorrent (tor));

  va_start (ap, fmt);
  tor->error = TR_STAT_LOCAL_ERROR;
  tor->errorTracker[0] = '\0';
  evutil_vsnprintf (tor->errorString, sizeof (tor->errorString), fmt, ap);
  va_end (ap);

  tr_logAddTorErr (tor, "%s", tor->errorString);

  if (tor->isRunning)
    tor->isStopping = true;
}

static void
tr_torrentClearError (tr_torrent * tor)
{
  tor->error = TR_STAT_OK;
  tor->errorString[0] = '\0';
  tor->errorTracker[0] = '\0';
}

static void
onTrackerResponse (tr_torrent * tor, const tr_tracker_event * event, void * unused UNUSED)
{
  switch (event->messageType)
    {
      case TR_TRACKER_PEERS:
        {
          size_t i;
          const int8_t seedProbability = event->seedProbability;
          const bool allAreSeeds = seedProbability == 100;

          if (allAreSeeds)
            tr_logAddTorDbg (tor, "Got %zu seeds from tracker", event->pexCount);
          else
            tr_logAddTorDbg (tor, "Got %zu peers from tracker", event->pexCount);

          for (i = 0; i < event->pexCount; ++i)
            tr_peerMgrAddPex (tor, TR_PEER_FROM_TRACKER, &event->pex[i], seedProbability);

          break;
        }

      case TR_TRACKER_WARNING:
        tr_logAddTorErr (tor, _("Tracker warning: \"%s\""), event->text);
        tor->error = TR_STAT_TRACKER_WARNING;
        tr_strlcpy (tor->errorTracker, event->tracker, sizeof (tor->errorTracker));
        tr_strlcpy (tor->errorString, event->text, sizeof (tor->errorString));
        break;

      case TR_TRACKER_ERROR:
        tr_logAddTorErr (tor, _("Tracker error: \"%s\""), event->text);
        tor->error = TR_STAT_TRACKER_ERROR;
        tr_strlcpy (tor->errorTracker, event->tracker, sizeof (tor->errorTracker));
        tr_strlcpy (tor->errorString, event->text, sizeof (tor->errorString));
        break;

      case TR_TRACKER_ERROR_CLEAR:
        if (tor->error != TR_STAT_LOCAL_ERROR)
          tr_torrentClearError (tor);
        break;
    }
}

/***
****
****  TORRENT INSTANTIATION
****
***/

static tr_piece_index_t
getBytePiece (const tr_info * info, uint64_t byteOffset)
{
  tr_piece_index_t piece;

  assert (info);
  assert (info->pieceSize != 0);

  piece = byteOffset / info->pieceSize;

  /* handle 0-byte files at the end of a torrent */
  if (byteOffset == info->totalSize)
    piece = info->pieceCount - 1;

  return piece;
}

static void
initFilePieces (tr_info *       info,
                tr_file_index_t fileIndex)
{
  tr_file * file;
  uint64_t  firstByte, lastByte;

  assert (info);
  assert (fileIndex < info->fileCount);

  file = &info->files[fileIndex];
  firstByte = file->offset;
  lastByte = firstByte + (file->length ? file->length - 1 : 0);
  file->firstPiece = getBytePiece (info, firstByte);
  file->lastPiece = getBytePiece (info, lastByte);
}

static bool
pieceHasFile (tr_piece_index_t piece,
              const tr_file *  file)
{
  return (file->firstPiece <= piece) && (piece <= file->lastPiece);
}

static tr_priority_t
calculatePiecePriority (const tr_torrent * tor,
                        tr_piece_index_t   piece,
                        int                fileHint)
{
  tr_file_index_t i;
  tr_priority_t priority = TR_PRI_LOW;

  /* find the first file that has data in this piece */
  if (fileHint >= 0)
    {
      i = fileHint;
      while (i > 0 && pieceHasFile (piece, &tor->info.files[i - 1]))
        --i;
    }
  else
    {
      for (i=0; i<tor->info.fileCount; ++i)
        if (pieceHasFile (piece, &tor->info.files[i]))
          break;
    }

  /* the piece's priority is the max of the priorities
   * of all the files in that piece */
  for (; i<tor->info.fileCount; ++i)
    {
      const tr_file * file = &tor->info.files[i];

      if (!pieceHasFile (piece, file))
        break;

      priority = MAX (priority, file->priority);

      /* when dealing with multimedia files, getting the first and
         last pieces can sometimes allow you to preview it a bit
         before it's fully downloaded... */
      if (file->priority >= TR_PRI_NORMAL)
        if (file->firstPiece == piece || file->lastPiece == piece)
          priority = TR_PRI_HIGH;
    }

  return priority;
}

static void
tr_torrentInitFilePieces (tr_torrent * tor)
{
  int * firstFiles;
  tr_file_index_t f;
  tr_piece_index_t p;
  uint64_t offset = 0;
  tr_info * inf = &tor->info;

  /* assign the file offsets */
  for (f=0; f<inf->fileCount; ++f)
    {
      inf->files[f].offset = offset;
      offset += inf->files[f].length;
      initFilePieces (inf, f);
    }

  /* build the array of first-file hints to give calculatePiecePriority */
  firstFiles = tr_new (int, inf->pieceCount);
  for (p=f=0; p<inf->pieceCount; ++p)
    {
      while (inf->files[f].lastPiece < p)
        ++f;
      firstFiles[p] = f;
    }

#if 0
  /* test to confirm the first-file hints are correct */
  for (p=0; p<inf->pieceCount; ++p)
    {
      f = firstFiles[p];
      assert (inf->files[f].firstPiece <= p);
      assert (inf->files[f].lastPiece >= p);
      if (f > 0)
        assert (inf->files[f-1].lastPiece < p);

      for (f=0; f<inf->fileCount; ++f)
        if (pieceHasFile (p, &inf->files[f]))
          break;

      assert ((int)f == firstFiles[p]);
    }
#endif

  for (p=0; p<inf->pieceCount; ++p)
    inf->pieces[p].priority = calculatePiecePriority (tor, p, firstFiles[p]);

  tr_free (firstFiles);
}

static void torrentStart (tr_torrent * tor, bool bypass_queue);

/**
 * Decide on a block size. Constraints:
 * (1) most clients decline requests over 16 KiB
 * (2) pieceSize must be a multiple of block size
 */
uint32_t
tr_getBlockSize (uint32_t pieceSize)
{
  uint32_t b = pieceSize;

  while (b > MAX_BLOCK_SIZE)
    b /= 2u;

  if (!b || (pieceSize % b)) /* not cleanly divisible */
    return 0;

  return b;
}

static void refreshCurrentDir (tr_torrent * tor);

static void
torrentInitFromInfo (tr_torrent * tor)
{
  uint64_t t;
  tr_info * info = &tor->info;

  tor->blockSize = tr_getBlockSize (info->pieceSize);

  if (info->pieceSize)
    tor->lastPieceSize = (uint32_t)(info->totalSize % info->pieceSize);

  if (!tor->lastPieceSize)
    tor->lastPieceSize = info->pieceSize;

  if (tor->blockSize)
    tor->lastBlockSize = info->totalSize % tor->blockSize;

  if (!tor->lastBlockSize)
    tor->lastBlockSize = tor->blockSize;

  tor->blockCount = tor->blockSize
    ? (info->totalSize + tor->blockSize - 1) / tor->blockSize
    : 0;

  tor->blockCountInPiece = tor->blockSize
    ? info->pieceSize / tor->blockSize
    : 0;

  tor->blockCountInLastPiece = tor->blockSize
    ? (tor->lastPieceSize + tor->blockSize - 1) / tor->blockSize
    : 0;

  /* check our work */
  if (tor->blockSize != 0)
    assert ((info->pieceSize % tor->blockSize) == 0);
  t = info->pieceCount - 1;
  t *= info->pieceSize;
  t += tor->lastPieceSize;
  assert (t == info->totalSize);
  t = tor->blockCount - 1;
  t *= tor->blockSize;
  t += tor->lastBlockSize;
  assert (t == info->totalSize);
  t = info->pieceCount - 1;
  t *= tor->blockCountInPiece;
  t += tor->blockCountInLastPiece;
  assert (t == (uint64_t)tor->blockCount);

  tr_cpConstruct (&tor->completion, tor);

  tr_torrentInitFilePieces (tor);

  tor->completeness = tr_cpGetStatus (&tor->completion);
}

static void tr_torrentFireMetadataCompleted (tr_torrent * tor);

void
tr_torrentGotNewInfoDict (tr_torrent * tor)
{
  torrentInitFromInfo (tor);

  tr_peerMgrOnTorrentGotMetainfo (tor);

  tr_torrentFireMetadataCompleted (tor);
}

static bool
hasAnyLocalData (const tr_torrent * tor)
{
  tr_file_index_t i;

  for (i=0; i<tor->info.fileCount; ++i)
    if (tr_torrentFindFile2 (tor, i, NULL, NULL, NULL))
      return true;

  return false;
}

static bool
setLocalErrorIfFilesDisappeared (tr_torrent * tor)
{
  const bool disappeared = (tr_torrentHaveTotal (tor) > 0) && !hasAnyLocalData (tor);

  if (disappeared)
    {
      tr_deeplog_tor (tor, "%s", "[LAZY] uh oh, the files disappeared");
      tr_torrentSetLocalError (tor, "%s", _("No data found! Ensure your drives are connected or use \"Set Location\". To re-download, remove the torrent and re-add it."));
    }

  return disappeared;
}

static void
torrentInit (tr_torrent * tor, const tr_ctor * ctor)
{
  bool doStart;
  uint64_t loaded;
  const char * dir;
  bool isNewTorrent;
  tr_session * session = tr_ctorGetSession (ctor);
  static int nextUniqueId = 1;

  assert (session != NULL);

  tr_sessionLock (session);

  tor->session   = session;
  tor->uniqueId = nextUniqueId++;
  tor->magicNumber = TORRENT_MAGIC_NUMBER;
  tor->queuePosition = session->torrentCount;

  tr_sha1 (tor->obfuscatedHash, "req2", 4,
           tor->info.hash, SHA_DIGEST_LENGTH,
           NULL);

  if (tr_ctorGetDownloadDir (ctor, TR_FORCE, &dir) ||
      tr_ctorGetDownloadDir (ctor, TR_FALLBACK, &dir))
    tor->downloadDir = tr_strdup (dir);

  if (!tr_ctorGetIncompleteDir (ctor, &dir))
    dir = tr_sessionGetIncompleteDir (session);
  if (tr_sessionIsIncompleteDirEnabled (session))
    tor->incompleteDir = tr_strdup (dir);

  tr_bandwidthConstruct (&tor->bandwidth, session, &session->bandwidth);

  tor->bandwidth.priority = tr_ctorGetBandwidthPriority (ctor);

  tor->error = TR_STAT_OK;

  tor->finishedSeedingByIdle = false;

  tr_peerMgrAddTorrent (session->peerMgr, tor);

  assert (!tor->downloadedCur);
  assert (!tor->uploadedCur);

  tr_torrentSetAddedDate (tor, tr_time ()); /* this is a default value to be
                                               overwritten by the resume file */

  torrentInitFromInfo (tor);
  loaded = tr_torrentLoadResume (tor, ~0, ctor);
  tor->completeness = tr_cpGetStatus (&tor->completion);
  setLocalErrorIfFilesDisappeared (tor);

  tr_ctorInitTorrentPriorities (ctor, tor);
  tr_ctorInitTorrentWanted (ctor, tor);

  refreshCurrentDir (tor);

  doStart = tor->isRunning;
  tor->isRunning = false;

  if (!(loaded & TR_FR_SPEEDLIMIT))
    {
      tr_torrentUseSpeedLimit (tor, TR_UP, false);
      tr_torrentSetSpeedLimit_Bps (tor, TR_UP, tr_sessionGetSpeedLimit_Bps (tor->session, TR_UP));
      tr_torrentUseSpeedLimit (tor, TR_DOWN, false);
      tr_torrentSetSpeedLimit_Bps (tor, TR_DOWN, tr_sessionGetSpeedLimit_Bps (tor->session, TR_DOWN));
      tr_torrentUseSessionLimits (tor, true);
    }

  if (!(loaded & TR_FR_RATIOLIMIT))
    {
      tr_torrentSetRatioMode (tor, TR_RATIOLIMIT_GLOBAL);
      tr_torrentSetRatioLimit (tor, tr_sessionGetRatioLimit (tor->session));
    }

  if (!(loaded & TR_FR_IDLELIMIT))
    {
      tr_torrentSetIdleMode (tor, TR_IDLELIMIT_GLOBAL);
      tr_torrentSetIdleLimit (tor, tr_sessionGetIdleLimit (tor->session));
    }

  /* add the torrent to tr_session.torrentList */
  session->torrentCount++;
  if (session->torrentList == NULL)
    {
      session->torrentList = tor;
    }
  else
    {
      tr_torrent * it = session->torrentList;
      while (it->next != NULL)
        it = it->next;
      it->next = tor;
    }

  /* if we don't have a local .torrent file already, assume the torrent is new */
  isNewTorrent = !tr_sys_path_exists (tor->info.torrent, NULL);

  /* maybe save our own copy of the metainfo */
  if (tr_ctorGetSave (ctor))
    {
      const tr_variant * val;
      if (tr_ctorGetMetainfo (ctor, &val))
        {
          const char * path = tor->info.torrent;
          const int err = tr_variantToFile (val, TR_VARIANT_FMT_BENC, path);
          if (err)
            tr_torrentSetLocalError (tor, "Unable to save torrent file: %s", tr_strerror (err));
          tr_sessionSetTorrentFile (tor->session, tor->info.hashString, path);
        }
    }

  tor->tiers = tr_announcerAddTorrent (tor, onTrackerResponse, NULL);

  if (isNewTorrent)
    {
      tor->startAfterVerify = doStart;
      tr_torrentVerify (tor, NULL, NULL);
    }
  else if (doStart)
    {
      tr_torrentStart (tor);
    }

  tr_sessionUnlock (session);
}

static tr_parse_result
torrentParseImpl (const tr_ctor  * ctor,
                  tr_info        * setmeInfo,
                  bool           * setmeHasInfo,
                  size_t         * dictLength,
                  int            * setme_duplicate_id)
{
  bool doFree;
  bool didParse;
  bool hasInfo = false;
  tr_info tmp;
  const tr_variant * metainfo;
  tr_session * session = tr_ctorGetSession (ctor);
  tr_parse_result result = TR_PARSE_OK;

  if (setmeInfo == NULL)
    setmeInfo = &tmp;
  memset (setmeInfo, 0, sizeof (tr_info));

  if (!tr_ctorGetMetainfo (ctor, &metainfo))
    return TR_PARSE_ERR;

  didParse = tr_metainfoParse (session, metainfo, setmeInfo,
                               &hasInfo, dictLength);
  doFree = didParse && (setmeInfo == &tmp);

  if (!didParse)
    result = TR_PARSE_ERR;

  if (didParse && hasInfo && !tr_getBlockSize (setmeInfo->pieceSize))
    result = TR_PARSE_ERR;

  if (didParse && session && (result == TR_PARSE_OK))
    {
      const tr_torrent * const tor = tr_torrentFindFromHash (session, setmeInfo->hash);

      if (tor != NULL)
        {
          result = TR_PARSE_DUPLICATE;

          if (setme_duplicate_id != NULL)
            *setme_duplicate_id = tr_torrentId (tor);
        }
    }

  if (doFree)
    tr_metainfoFree (setmeInfo);

  if (setmeHasInfo != NULL)
    *setmeHasInfo = hasInfo;

  return result;
}

tr_parse_result
tr_torrentParse (const tr_ctor * ctor, tr_info * setmeInfo)
{
  return torrentParseImpl (ctor, setmeInfo, NULL, NULL, NULL);
}

tr_torrent *
tr_torrentNew (const tr_ctor * ctor, int * setme_error, int * setme_duplicate_id)
{
  size_t len;
  bool hasInfo;
  tr_info tmpInfo;
  tr_parse_result r;
  tr_torrent * tor = NULL;

  assert (ctor != NULL);
  assert (tr_isSession (tr_ctorGetSession (ctor)));

  r = torrentParseImpl (ctor, &tmpInfo, &hasInfo, &len, setme_duplicate_id);
  if (r == TR_PARSE_OK)
    {
      tor = tr_new0 (tr_torrent, 1);
      tor->info = tmpInfo;

      if (hasInfo)
        tor->infoDictLength = len;

      torrentInit (tor, ctor);
    }
  else
    {
      if (r == TR_PARSE_DUPLICATE)
        tr_metainfoFree (&tmpInfo);

      if (setme_error != NULL)
        *setme_error = r;
    }

  return tor;
}

/**
***
**/

void
tr_torrentSetDownloadDir (tr_torrent * tor, const char * path)
{
  assert (tr_isTorrent (tor));

  if (!path || !tor->downloadDir || strcmp (path, tor->downloadDir))
    {
      tr_free (tor->downloadDir);
      tor->downloadDir = tr_strdup (path);
      tr_torrentSetDirty (tor);
    }

  refreshCurrentDir (tor);
}

const char*
tr_torrentGetDownloadDir (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tor->downloadDir;
}

const char *
tr_torrentGetCurrentDir (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tor->currentDir;
}


void
tr_torrentChangeMyPort (tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  if (tor->isRunning)
    tr_announcerChangeMyPort (tor);
}

static inline void
tr_torrentManualUpdateImpl (void * vtor)
{
  tr_torrent * tor = vtor;

  assert (tr_isTorrent (tor));

  if (tor->isRunning)
    tr_announcerManualAnnounce (tor);
}

void
tr_torrentManualUpdate (tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  tr_runInEventThread (tor->session, tr_torrentManualUpdateImpl, tor);
}

bool
tr_torrentCanManualUpdate (const tr_torrent * tor)
{
  return (tr_isTorrent (tor))
      && (tor->isRunning)
      && (tr_announcerCanManualAnnounce (tor));
}

const tr_info *
tr_torrentInfo (const tr_torrent * tor)
{
  return tr_isTorrent (tor) ? &tor->info : NULL;
}

const tr_stat *
tr_torrentStatCached (tr_torrent * tor)
{
  const time_t now = tr_time ();

  return tr_isTorrent (tor) && (now == tor->lastStatTime)
       ? &tor->stats
       : tr_torrentStat (tor);
}

void
tr_torrentSetVerifyState (tr_torrent * tor, tr_verify_state state)
{
  assert (tr_isTorrent (tor));
  assert (state==TR_VERIFY_NONE || state==TR_VERIFY_WAIT || state==TR_VERIFY_NOW);

  tor->verifyState = state;
  tor->anyDate = tr_time ();
}

tr_torrent_activity
tr_torrentGetActivity (const tr_torrent * tor)
{
  tr_torrent_activity ret = TR_STATUS_STOPPED;

  const bool is_seed = tr_torrentIsSeed (tor);

  if (tor->verifyState == TR_VERIFY_NOW)
    {
      ret = TR_STATUS_CHECK;
    }
  else if (tor->verifyState == TR_VERIFY_WAIT)
    {
      ret = TR_STATUS_CHECK_WAIT;
    }
  else if (tor->isRunning)
    {
      ret = is_seed ? TR_STATUS_SEED : TR_STATUS_DOWNLOAD;
    }
  else if (tr_torrentIsQueued (tor))
    {
      if (is_seed && tr_sessionGetQueueEnabled (tor->session, TR_UP))
        ret = TR_STATUS_SEED_WAIT;
      else if (!is_seed && tr_sessionGetQueueEnabled (tor->session, TR_DOWN))
        ret = TR_STATUS_DOWNLOAD_WAIT;
    }

  return ret;
}

static time_t
torrentGetIdleSecs (const tr_torrent * tor)
{
  int idle_secs;
  const tr_torrent_activity activity = tr_torrentGetActivity (tor);

  if ((activity == TR_STATUS_DOWNLOAD || activity == TR_STATUS_SEED) && tor->startDate != 0)
    idle_secs = difftime (tr_time (), MAX (tor->startDate, tor->activityDate));
  else
    idle_secs = -1;

  return idle_secs;
}

bool
tr_torrentIsStalled (const tr_torrent * tor)
{
  return tr_sessionGetQueueStalledEnabled (tor->session)
      && (torrentGetIdleSecs (tor) > (tr_sessionGetQueueStalledMinutes (tor->session) * 60));
}


static double
getVerifyProgress (const tr_torrent * tor)
{
  double d = 0;

  if (tr_torrentHasMetadata (tor))
    {
      tr_piece_index_t i, n;
      tr_piece_index_t checked = 0;

      for (i=0, n=tor->info.pieceCount; i!=n; ++i)
        if (tor->info.pieces[i].timeChecked)
          ++checked;

      d = checked / (double)tor->info.pieceCount;
    }

  return d;
}

const tr_stat *
tr_torrentStat (tr_torrent * tor)
{
  tr_stat * s;
  uint64_t seedRatioBytesLeft;
  uint64_t seedRatioBytesGoal;
  bool seedRatioApplies;
  uint16_t seedIdleMinutes;
  const uint64_t now = tr_time_msec ();
  unsigned int pieceUploadSpeed_Bps;
  unsigned int pieceDownloadSpeed_Bps;
  struct tr_swarm_stats swarm_stats;
  int i;

  assert (tr_isTorrent (tor));

  tor->lastStatTime = tr_time ();

  if (tor->swarm != NULL)
    tr_swarmGetStats (tor->swarm, &swarm_stats);
  else
    swarm_stats = TR_SWARM_STATS_INIT;

  s = &tor->stats;
  s->id = tor->uniqueId;
  s->activity = tr_torrentGetActivity (tor);
  s->error = tor->error;
  s->queuePosition = tor->queuePosition;
  s->isStalled = tr_torrentIsStalled (tor);
  tr_strlcpy (s->errorString, tor->errorString, sizeof (s->errorString));

  s->manualAnnounceTime = tr_announcerNextManualAnnounce (tor);
  s->peersConnected      = swarm_stats.peerCount;
  s->peersSendingToUs    = swarm_stats.activePeerCount[TR_DOWN];
  s->peersGettingFromUs  = swarm_stats.activePeerCount[TR_UP];
  s->webseedsSendingToUs = swarm_stats.activeWebseedCount;
  for (i=0; i<TR_PEER_FROM__MAX; i++)
    s->peersFrom[i] = swarm_stats.peerFromCount[i];

  s->rawUploadSpeed_KBps     = toSpeedKBps (tr_bandwidthGetRawSpeed_Bps (&tor->bandwidth, now, TR_UP));
  s->rawDownloadSpeed_KBps   = toSpeedKBps (tr_bandwidthGetRawSpeed_Bps (&tor->bandwidth, now, TR_DOWN));
  pieceUploadSpeed_Bps       = tr_bandwidthGetPieceSpeed_Bps (&tor->bandwidth, now, TR_UP);
  pieceDownloadSpeed_Bps     = tr_bandwidthGetPieceSpeed_Bps (&tor->bandwidth, now, TR_DOWN);
  s->pieceUploadSpeed_KBps   = toSpeedKBps (pieceUploadSpeed_Bps);
  s->pieceDownloadSpeed_KBps = toSpeedKBps (pieceDownloadSpeed_Bps);

  s->percentComplete = tr_cpPercentComplete (&tor->completion);
  s->metadataPercentComplete = tr_torrentGetMetadataPercent (tor);

  s->percentDone         = tr_cpPercentDone (&tor->completion);
  s->leftUntilDone       = tr_torrentGetLeftUntilDone (tor);
  s->sizeWhenDone        = tr_cpSizeWhenDone (&tor->completion);
  s->recheckProgress     = s->activity == TR_STATUS_CHECK ? getVerifyProgress (tor) : 0;
  s->activityDate        = tor->activityDate;
  s->addedDate           = tor->addedDate;
  s->doneDate            = tor->doneDate;
  s->startDate           = tor->startDate;
  s->secondsSeeding      = tor->secondsSeeding;
  s->secondsDownloading  = tor->secondsDownloading;
  s->idleSecs            = torrentGetIdleSecs (tor);

  s->corruptEver      = tor->corruptCur    + tor->corruptPrev;
  s->downloadedEver   = tor->downloadedCur + tor->downloadedPrev;
  s->uploadedEver     = tor->uploadedCur   + tor->uploadedPrev;
  s->haveValid        = tr_cpHaveValid (&tor->completion);
  s->haveUnchecked    = tr_torrentHaveTotal (tor) - s->haveValid;
  s->desiredAvailable = tr_peerMgrGetDesiredAvailable (tor);

  s->ratio = tr_getRatio (s->uploadedEver,
                          s->downloadedEver ? s->downloadedEver : s->haveValid);

  seedRatioApplies = tr_torrentGetSeedRatioBytes (tor, &seedRatioBytesLeft,
                                                       &seedRatioBytesGoal);

  switch (s->activity)
    {
      /* etaXLSpeed exists because if we use the piece speed directly,
       * brief fluctuations cause the ETA to jump all over the place.
       * so, etaXLSpeed is a smoothed-out version of the piece speed
       * to dampen the effect of fluctuations */
      case TR_STATUS_DOWNLOAD:
        if ((tor->etaDLSpeedCalculatedAt + 800) < now)
          {
            tor->etaDLSpeed_Bps = ((tor->etaDLSpeedCalculatedAt + 4000) < now)
              ? pieceDownloadSpeed_Bps /* if no recent previous speed, no need to smooth */
              : ((tor->etaDLSpeed_Bps*4.0) + pieceDownloadSpeed_Bps)/5.0; /* smooth across 5 readings */
            tor->etaDLSpeedCalculatedAt = now;
          }

        if ((s->leftUntilDone > s->desiredAvailable) && (tor->info.webseedCount < 1))
          s->eta = TR_ETA_NOT_AVAIL;
        else if (tor->etaDLSpeed_Bps == 0)
          s->eta = TR_ETA_UNKNOWN;
        else
          s->eta = s->leftUntilDone / tor->etaDLSpeed_Bps;

        s->etaIdle = TR_ETA_NOT_AVAIL;
        break;

      case TR_STATUS_SEED:
        if (!seedRatioApplies)
          {
            s->eta = TR_ETA_NOT_AVAIL;
          }
        else
          {
            if ((tor->etaULSpeedCalculatedAt + 800) < now)
              {
                tor->etaULSpeed_Bps = ((tor->etaULSpeedCalculatedAt + 4000) < now)
                  ? pieceUploadSpeed_Bps /* if no recent previous speed, no need to smooth */
                  : ((tor->etaULSpeed_Bps*4.0) + pieceUploadSpeed_Bps)/5.0; /* smooth across 5 readings */
                tor->etaULSpeedCalculatedAt = now;
              }

            if (tor->etaULSpeed_Bps == 0)
              s->eta = TR_ETA_UNKNOWN;
            else
              s->eta = seedRatioBytesLeft / tor->etaULSpeed_Bps;
          }

        if (tor->etaULSpeed_Bps < 1 && tr_torrentGetSeedIdle (tor, &seedIdleMinutes))
          s->etaIdle = seedIdleMinutes * 60 - s->idleSecs;
        else
          s->etaIdle = TR_ETA_NOT_AVAIL;
        break;

      default:
        s->eta = TR_ETA_NOT_AVAIL;
        s->etaIdle = TR_ETA_NOT_AVAIL;
        break;
    }

  /* s->haveValid is here to make sure a torrent isn't marked 'finished'
   * when the user hits "uncheck all" prior to starting the torrent... */
  s->finished = tor->finishedSeedingByIdle || (seedRatioApplies && !seedRatioBytesLeft && s->haveValid);

  if (!seedRatioApplies || s->finished)
    s->seedRatioPercentDone = 1;
  else if (!seedRatioBytesGoal) /* impossible? safeguard for div by zero */
    s->seedRatioPercentDone = 0;
  else
    s->seedRatioPercentDone = (double)(seedRatioBytesGoal - seedRatioBytesLeft) / seedRatioBytesGoal;

  /* test some of the constraints */
  assert (s->sizeWhenDone <= tor->info.totalSize);
  assert (s->leftUntilDone <= s->sizeWhenDone);
  assert (s->desiredAvailable <= s->leftUntilDone);

  return s;
}

/***
****
***/

static uint64_t
countFileBytesCompleted (const tr_torrent * tor, tr_file_index_t index)
{
  uint64_t total = 0;
  const tr_file * f = &tor->info.files[index];

  if (f->length)
    {
      tr_block_index_t first;
      tr_block_index_t last;
      tr_torGetFileBlockRange (tor, index, &first, &last);

      if (first == last)
        {
          if (tr_torrentBlockIsComplete (tor, first))
            total = f->length;
        }
      else
        {
          /* the first block */
          if (tr_torrentBlockIsComplete (tor, first))
            total += tor->blockSize - (f->offset % tor->blockSize);

          /* the middle blocks */
          if (first + 1 < last)
            {
              uint64_t u = tr_bitfieldCountRange (&tor->completion.blockBitfield, first+1, last);
              u *= tor->blockSize;
              total += u;
            }

          /* the last block */
          if (tr_torrentBlockIsComplete (tor, last))
            total += (f->offset + f->length) - ((uint64_t)tor->blockSize * last);
        }
    }

  return total;
}

tr_file_stat *
tr_torrentFiles (const tr_torrent * tor,
                 tr_file_index_t  * fileCount)
{
  tr_file_index_t i;
  const tr_file_index_t n = tor->info.fileCount;
  tr_file_stat * files = tr_new0 (tr_file_stat, n);
  tr_file_stat * walk = files;
  const bool isSeed = tor->completeness == TR_SEED;

  assert (tr_isTorrent (tor));

  for (i=0; i<n; ++i, ++walk)
    {
      const uint64_t b = isSeed ? tor->info.files[i].length : countFileBytesCompleted (tor, i);
      walk->bytesCompleted = b;
      walk->progress = tor->info.files[i].length > 0 ? ((float)b / tor->info.files[i].length) : 1.0f;
    }

  if (fileCount != NULL)
    *fileCount = n;

  return files;
}

void
tr_torrentFilesFree (tr_file_stat     * files,
                     tr_file_index_t    fileCount UNUSED)
{
  tr_free (files);
}

/***
****
***/

double*
tr_torrentWebSpeeds_KBps (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tr_peerMgrWebSpeeds_KBps (tor);
}

tr_peer_stat *
tr_torrentPeers (const tr_torrent * tor, int * peerCount)
{
  assert (tr_isTorrent (tor));

  return tr_peerMgrPeerStats (tor, peerCount);
}

void
tr_torrentPeersFree (tr_peer_stat * peers, int peerCount UNUSED)
{
  tr_free (peers);
}

tr_tracker_stat *
tr_torrentTrackers (const tr_torrent * tor, int * setmeTrackerCount)
{
  assert (tr_isTorrent (tor));

  return tr_announcerStats (tor, setmeTrackerCount);
}

void
tr_torrentTrackersFree (tr_tracker_stat * trackers, int trackerCount)
{
  tr_announcerStatsFree (trackers, trackerCount);
}

void
tr_torrentAvailability (const tr_torrent * tor, int8_t * tab, int size)
{
  assert (tr_isTorrent (tor));

  if ((tab != NULL) && (size > 0))
    tr_peerMgrTorrentAvailability (tor, tab, size);
}

void
tr_torrentAmountFinished (const tr_torrent * tor, float * tab, int size)
{
  tr_cpGetAmountDone (&tor->completion, tab, size);
}

static void
tr_torrentResetTransferStats (tr_torrent * tor)
{
  tr_torrentLock (tor);

  tor->downloadedPrev += tor->downloadedCur;
  tor->downloadedCur   = 0;
  tor->uploadedPrev   += tor->uploadedCur;
  tor->uploadedCur     = 0;
  tor->corruptPrev    += tor->corruptCur;
  tor->corruptCur      = 0;

  tr_torrentSetDirty (tor);

  tr_torrentUnlock (tor);
}

void
tr_torrentSetHasPiece (tr_torrent *     tor,
                       tr_piece_index_t pieceIndex,
                       bool             has)
{
  assert (tr_isTorrent (tor));
  assert (pieceIndex < tor->info.pieceCount);

  if (has)
    tr_cpPieceAdd (&tor->completion, pieceIndex);
  else
    tr_cpPieceRem (&tor->completion, pieceIndex);
}

/***
****
***/

#ifndef NDEBUG
static bool queueIsSequenced (tr_session *);
#endif

static void
freeTorrent (tr_torrent * tor)
{
  tr_torrent * t;
  tr_session * session = tor->session;
  tr_info * inf = &tor->info;
  const time_t now = tr_time ();

  assert (!tor->isRunning);

  tr_sessionLock (session);

  tr_peerMgrRemoveTorrent (tor);

  tr_announcerRemoveTorrent (session->announcer, tor);

  tr_cpDestruct (&tor->completion);

  tr_free (tor->downloadDir);
  tr_free (tor->incompleteDir);

  if (tor == session->torrentList)
    {
      session->torrentList = tor->next;
    }
  else for (t = session->torrentList; t != NULL; t = t->next)
    {
      if (t->next == tor)
        {
          t->next = tor->next;
          break;
        }
    }

  /* decrement the torrent count */
  assert (session->torrentCount >= 1);
  session->torrentCount--;

  /* resequence the queue positions */
  t = NULL;
  while ((t = tr_torrentNext (session, t)))
    {
      if (t->queuePosition > tor->queuePosition)
        {
          t->queuePosition--;
          t->anyDate = now;
        }
    }
  assert (queueIsSequenced (session));

  tr_bandwidthDestruct (&tor->bandwidth);

  tr_metainfoFree (inf);
  memset (tor, ~0, sizeof (tr_torrent));
  tr_free (tor);

  tr_sessionUnlock (session);
}

/**
***  Start/Stop Callback
**/

static void torrentSetQueued (tr_torrent * tor, bool queued);

static void
torrentStartImpl (void * vtor)
{
  time_t now;
  tr_torrent * tor = vtor;

  assert (tr_isTorrent (tor));

  tr_sessionLock (tor->session);

  tr_torrentRecheckCompleteness (tor);
  torrentSetQueued (tor, false);

  now = tr_time ();
  tor->isRunning = true;
  tor->completeness = tr_cpGetStatus (&tor->completion);
  tor->startDate = tor->anyDate = now;
  tr_torrentClearError (tor);
  tor->finishedSeedingByIdle = false;

  tr_torrentResetTransferStats (tor);
  tr_announcerTorrentStarted (tor);
  tor->dhtAnnounceAt = now + tr_rand_int_weak (20);
  tor->dhtAnnounce6At = now + tr_rand_int_weak (20);
  tor->lpdAnnounceAt = now;
  tr_peerMgrStartTorrent (tor);

  tr_sessionUnlock (tor->session);
}

uint64_t
tr_torrentGetCurrentSizeOnDisk (const tr_torrent * tor)
{
  tr_file_index_t i;
  uint64_t byte_count = 0;
  const tr_file_index_t n = tor->info.fileCount;

  for (i=0; i<n; ++i)
    {
      tr_sys_path_info info;
      char * filename = tr_torrentFindFile (tor, i);

      if (filename != NULL && tr_sys_path_get_info (filename, 0, &info, NULL))
        byte_count += info.size;

      tr_free (filename);
    }

  return byte_count;
}

static bool
torrentShouldQueue (const tr_torrent * tor)
{
  const tr_direction dir = tr_torrentGetQueueDirection (tor);

  return tr_sessionCountQueueFreeSlots (tor->session, dir) == 0;
}

static void
torrentStart (tr_torrent * tor, bool bypass_queue)
{
  switch (tr_torrentGetActivity (tor))
    {
      case TR_STATUS_SEED:
      case TR_STATUS_DOWNLOAD:
        return; /* already started */
        break;

      case TR_STATUS_SEED_WAIT:
      case TR_STATUS_DOWNLOAD_WAIT:
        if (!bypass_queue)
          return; /* already queued */
        break;

      case TR_STATUS_CHECK:
      case TR_STATUS_CHECK_WAIT:
        /* verifying right now... wait until that's done so
         * we'll know what completeness to use/announce */
        tor->startAfterVerify = true;
        return;
        break;

      case TR_STATUS_STOPPED:
        if (!bypass_queue && torrentShouldQueue (tor))
          {
            torrentSetQueued (tor, true);
            return;
          }
        break;
    }

  /* don't allow the torrent to be started if the files disappeared */
  if (setLocalErrorIfFilesDisappeared (tor))
    return;

  /* otherwise, start it now... */
  tr_sessionLock (tor->session);

  /* allow finished torrents to be resumed */
  if (tr_torrentIsSeedRatioDone (tor))
    {
      tr_logAddTorInfo (tor, "%s", _("Restarted manually -- disabling its seed ratio"));
      tr_torrentSetRatioMode (tor, TR_RATIOLIMIT_UNLIMITED);
    }

  /* corresponds to the peer_id sent as a tracker request parameter.
   * one tracker admin says: "When the same torrent is opened and
   * closed and opened again without quitting Transmission ...
   * change the peerid. It would help sometimes if a stopped event
   * was missed to ensure that we didn't think someone was cheating. */
  tr_torrentUnsetPeerId (tor);
  tor->isRunning = true;
  tr_torrentSetDirty (tor);
  tr_runInEventThread (tor->session, torrentStartImpl, tor);

  tr_sessionUnlock (tor->session);
}

void
tr_torrentStart (tr_torrent * tor)
{
  if (tr_isTorrent (tor))
    torrentStart (tor, false);
}

void
tr_torrentStartNow (tr_torrent * tor)
{
  if (tr_isTorrent (tor))
    torrentStart (tor, true);
}

struct verify_data
{
  bool aborted;
  tr_torrent * tor;
  tr_verify_done_func callback_func;
  void * callback_data;
};

static void
onVerifyDoneThreadFunc (void * vdata)
{
  struct verify_data * data = vdata;
  tr_torrent * tor = data->tor;

  if (!data->aborted)
    tr_torrentRecheckCompleteness (tor);

  if (data->callback_func != NULL)
    (*data->callback_func)(tor, data->aborted, data->callback_data);

  if (!data->aborted && tor->startAfterVerify)
    {
      tor->startAfterVerify = false;
      torrentStart (tor, false);
    }

  tr_free (data);
}

static void
onVerifyDone (tr_torrent * tor, bool aborted, void * vdata)
{
  struct verify_data * data = vdata;
  assert (data->tor == tor);
  data->aborted = aborted;
  tr_runInEventThread (tor->session, onVerifyDoneThreadFunc, data);
}

static void
verifyTorrent (void * vdata)
{
  bool startAfter;
  struct verify_data * data = vdata;
  tr_torrent * tor = data->tor;
  tr_sessionLock (tor->session);

  /* if the torrent's already being verified, stop it */
  tr_verifyRemove (tor);

  startAfter = (tor->isRunning || tor->startAfterVerify) && !tor->isStopping;
  if (tor->isRunning)
    tr_torrentStop (tor);
  tor->startAfterVerify = startAfter;

  if (setLocalErrorIfFilesDisappeared (tor))
    tor->startAfterVerify = false;
  else
    tr_verifyAdd (tor, onVerifyDone, data);

  tr_sessionUnlock (tor->session);
}

void
tr_torrentVerify (tr_torrent           * tor,
                  tr_verify_done_func    callback_func,
                  void                 * callback_data)
{
  struct verify_data * data;

  data = tr_new (struct verify_data, 1);
  data->tor = tor;
  data->aborted = false;
  data->callback_func = callback_func;
  data->callback_data = callback_data;
  tr_runInEventThread (tor->session, verifyTorrent, data);
}

void
tr_torrentSave (tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  if (tor->isDirty)
    {
      tor->isDirty = false;
      tr_torrentSaveResume (tor);
    }
}

static void
stopTorrent (void * vtor)
{
  tr_torrent * tor = vtor;
  tr_logAddTorInfo (tor, "%s", "Pausing");

  assert (tr_isTorrent (tor));

  tr_torrentLock (tor);

  tr_verifyRemove (tor);
  tr_peerMgrStopTorrent (tor);
  tr_announcerTorrentStopped (tor);
  tr_cacheFlushTorrent (tor->session->cache, tor);

  tr_fdTorrentClose (tor->session, tor->uniqueId);

  if (!tor->isDeleting)
    tr_torrentSave (tor);

  torrentSetQueued (tor, false);

  tr_torrentUnlock (tor);

  if (tor->magnetVerify)
    {
      tor->magnetVerify = false;
      tr_logAddTorInfo (tor, "%s", "Magnet Verify");
      refreshCurrentDir (tor);
      tr_torrentVerify (tor, NULL, NULL);
    }
}

void
tr_torrentStop (tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  if (tr_isTorrent (tor))
    {
      tr_sessionLock (tor->session);

      tor->isRunning = false;
      tor->isStopping = false;
      tr_torrentSetDirty (tor);
      tr_runInEventThread (tor->session, stopTorrent, tor);

      tr_sessionUnlock (tor->session);
    }
}

static void
closeTorrent (void * vtor)
{
  tr_variant * d;
  tr_torrent * tor = vtor;

  assert (tr_isTorrent (tor));

  d = tr_variantListAddDict (&tor->session->removedTorrents, 2);
  tr_variantDictAddInt (d, TR_KEY_id, tor->uniqueId);
  tr_variantDictAddInt (d, TR_KEY_date, tr_time ());

  tr_logAddTorInfo (tor, "%s", _("Removing torrent"));

  tor->magnetVerify = false;
  stopTorrent (tor);

  if (tor->isDeleting)
    {
      tr_metainfoRemoveSaved (tor->session, &tor->info);
      tr_torrentRemoveResume (tor);
    }

  tor->isRunning = false;
  freeTorrent (tor);
}

void
tr_torrentFree (tr_torrent * tor)
{
  if (tr_isTorrent (tor))
    {
      tr_session * session = tor->session;
      assert (tr_isSession (session));
      tr_sessionLock (session);

      tr_torrentClearCompletenessCallback (tor);
      tr_runInEventThread (session, closeTorrent, tor);

      tr_sessionUnlock (session);
    }
}

struct remove_data
{
    tr_torrent  * tor;
    bool          deleteFlag;
    tr_fileFunc   deleteFunc;
};

static void tr_torrentDeleteLocalData (tr_torrent *, tr_fileFunc);

static void
removeTorrent (void * vdata)
{
  struct remove_data * data = vdata;
  tr_session * session = data->tor->session;
  tr_sessionLock (session);

  if (data->deleteFlag)
    tr_torrentDeleteLocalData (data->tor, data->deleteFunc);

  tr_torrentClearCompletenessCallback (data->tor);
  closeTorrent (data->tor);
  tr_free (data);

  tr_sessionUnlock (session);
}

void
tr_torrentRemove (tr_torrent   * tor,
                  bool           deleteFlag,
                  tr_fileFunc    deleteFunc)
{
  struct remove_data * data;

  assert (tr_isTorrent (tor));
  tor->isDeleting = true;

  data = tr_new0 (struct remove_data, 1);
  data->tor = tor;
  data->deleteFlag = deleteFlag;
  data->deleteFunc = deleteFunc;
  tr_runInEventThread (tor->session, removeTorrent, data);
}

/**
***  Completeness
**/

static const char *
getCompletionString (int type)
{
  switch (type)
    {
      /* Translators: this is a minor point that's safe to skip over, but FYI:
         "Complete" and "Done" are specific, different terms in Transmission:
         "Complete" means we've downloaded every file in the torrent.
         "Done" means we're done downloading the files we wanted, but NOT all
         that exist */
      case TR_PARTIAL_SEED:
        return _("Done");

      case TR_SEED:
        return _("Complete");

      default:
        return _("Incomplete");
    }
}

static void
fireCompletenessChange (tr_torrent       * tor,
                        tr_completeness    status,
                        bool               wasRunning)
{
  assert ((status == TR_LEECH)
       || (status == TR_SEED)
       || (status == TR_PARTIAL_SEED));

  if (tor->completeness_func)
    tor->completeness_func (tor, status, wasRunning,
                            tor->completeness_func_user_data);
}

void
tr_torrentSetCompletenessCallback (tr_torrent                    * tor,
                                   tr_torrent_completeness_func    func,
                                   void                          * user_data)
{
  assert (tr_isTorrent (tor));

  tor->completeness_func = func;
  tor->completeness_func_user_data = user_data;
}

void
tr_torrentClearCompletenessCallback (tr_torrent * torrent)
{
  tr_torrentSetCompletenessCallback (torrent, NULL, NULL);
}

void
tr_torrentSetRatioLimitHitCallback (tr_torrent                     * tor,
                                    tr_torrent_ratio_limit_hit_func  func,
                                    void                           * user_data)
{
  assert (tr_isTorrent (tor));

  tor->ratio_limit_hit_func = func;
  tor->ratio_limit_hit_func_user_data = user_data;
}

void
tr_torrentClearRatioLimitHitCallback (tr_torrent * torrent)
{
  tr_torrentSetRatioLimitHitCallback (torrent, NULL, NULL);
}

void
tr_torrentSetIdleLimitHitCallback (tr_torrent                    * tor,
                                   tr_torrent_idle_limit_hit_func  func,
                                   void                          * user_data)
{
  assert (tr_isTorrent (tor));

  tor->idle_limit_hit_func = func;
  tor->idle_limit_hit_func_user_data = user_data;
}

void
tr_torrentClearIdleLimitHitCallback (tr_torrent * torrent)
{
  tr_torrentSetIdleLimitHitCallback (torrent, NULL, NULL);
}

#ifndef _WIN32

static void
onSigCHLD (int i UNUSED)
{
  int rc;
  do
    rc = waitpid (-1, NULL, WNOHANG);
  while (rc>0 || (rc==-1 && errno==EINTR));
}

#endif

static void
torrentCallScript (const tr_torrent * tor, const char * script)
{
  char timeStr[128], * newlinePos;
  const time_t now = tr_time ();

  tr_strlcpy (timeStr, ctime (&now), sizeof (timeStr));

  /* ctime () includes '\n', but it's better to be safe */
  newlinePos = strchr (timeStr, '\n');
  if (newlinePos != NULL)
    *newlinePos = '\0';

  if (script && *script)
    {
      size_t i;
      char * cmd[] = { tr_strdup (script), NULL };
      char * env[] = {
        tr_strdup_printf ("TR_APP_VERSION=%s", SHORT_VERSION_STRING),
        tr_strdup_printf ("TR_TIME_LOCALTIME=%s", timeStr),
        tr_strdup_printf ("TR_TORRENT_DIR=%s", tor->currentDir),
        tr_strdup_printf ("TR_TORRENT_HASH=%s", tor->info.hashString),
        tr_strdup_printf ("TR_TORRENT_ID=%d", tr_torrentId (tor)),
        tr_strdup_printf ("TR_TORRENT_NAME=%s", tr_torrentName (tor)),
        NULL };

      tr_logAddTorInfo (tor, "Calling script \"%s\"", script);

#ifndef NDEBUG
      /* Win32 environment block strings should be sorted alphabetically */
      for (i = 1; env[i] != NULL; ++i)
        assert (strcmp (env[i - 1], env[i]) < 0);
#endif

#ifdef _WIN32

      wchar_t * wide_script = tr_win32_utf8_to_native (script, -1);

      size_t env_block_size = 0;
      char * env_block = NULL;
      for (i = 0; env[i] != NULL; ++i)
        {
          const size_t len = strlen (env[i]) + 1;
          env_block = tr_renew (char, env_block, env_block_size + len + 1);
          memcpy (env_block + env_block_size, env[i], len + 1);
          env_block_size += len;
        }

      wchar_t * wide_env_block = NULL;
      if (env_block != NULL)
        {
          env_block[env_block_size] = '\0';
          wide_env_block = tr_win32_utf8_to_native (env_block, env_block_size + 1);
          tr_free (env_block);
        }

      STARTUPINFOW si = { 0, };
      si.cb = sizeof (si);
      si.dwFlags = STARTF_USESHOWWINDOW;
      si.wShowWindow = SW_HIDE;

      PROCESS_INFORMATION pi;

      if (CreateProcessW (wide_script, NULL, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS |
                          CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE |
                          DETACHED_PROCESS, wide_env_block, L"\\", &si, &pi))
        {
          CloseHandle (pi.hThread);
          CloseHandle (pi.hProcess);
        }
      else
        {
          char * const message = tr_win32_format_message (GetLastError ());
          tr_logAddTorErr (tor, "error executing script \"%s\": %s", script, message);
          tr_free (message);
        }

      tr_free (wide_env_block);
      tr_free (wide_script);

#else /* _WIN32 */

      signal (SIGCHLD, onSigCHLD);

      if (!fork ())
        {
          for (i = 0; env[i] != NULL; ++i)
            putenv (env[i]);

          if (chdir ("/") == -1)
            {
              /* ignore (nice to have but not that critical) */
            }

          if (execvp (script, cmd) == -1)
            tr_logAddTorErr (tor, "error executing script \"%s\": %s", script, tr_strerror (errno));

          _exit (0);
        }

#endif /* _WIN32 */

      for (i = 0; cmd[i] != NULL; ++i)
        tr_free (cmd[i]);
      for (i = 0; env[i] != NULL; ++i)
        tr_free (env[i]);
    }
}

void
tr_torrentRecheckCompleteness (tr_torrent * tor)
{
  tr_completeness completeness;

  tr_torrentLock (tor);

  completeness = tr_cpGetStatus (&tor->completion);
  if (completeness != tor->completeness)
    {
      const bool recentChange = tor->downloadedCur != 0;
      const bool wasLeeching = !tr_torrentIsSeed (tor);
      const bool wasRunning = tor->isRunning;

      if (recentChange)
        tr_logAddTorInfo (tor, _("State changed from \"%1$s\" to \"%2$s\""),
                          getCompletionString (tor->completeness),
                          getCompletionString (completeness));

      tor->completeness = completeness;
      tr_fdTorrentClose (tor->session, tor->uniqueId);

      if (tr_torrentIsSeed (tor))
        {
          if (recentChange)
            {
              tr_announcerTorrentCompleted (tor);
              tor->doneDate = tor->anyDate = tr_time ();
            }

          if (wasLeeching && wasRunning)
            {
              /* clear interested flag on all peers */
              tr_peerMgrClearInterest (tor);
            }

          if (tor->currentDir == tor->incompleteDir)
            tr_torrentSetLocation (tor, tor->downloadDir, true, NULL, NULL);
        }

      fireCompletenessChange (tor, completeness, wasRunning);

      if (tr_torrentIsSeed (tor))
        {
          if (wasLeeching && wasRunning)
            {
              /* if completeness was TR_LEECH then the seed limit check will have been skipped in bandwidthPulse */
              tr_torrentCheckSeedLimit (tor);
            }

          if (tr_sessionIsTorrentDoneScriptEnabled (tor->session))
            torrentCallScript (tor, tr_sessionGetTorrentDoneScript (tor->session));
        }

      tr_torrentSetDirty (tor);
    }

  tr_torrentUnlock (tor);
}

/***
****
***/

static void
tr_torrentFireMetadataCompleted (tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  if (tor->metadata_func != NULL)
    tor->metadata_func (tor, tor->metadata_func_user_data);
}

void
tr_torrentSetMetadataCallback (tr_torrent                * tor,
                               tr_torrent_metadata_func    func,
                               void                      * user_data)
{
  assert (tr_isTorrent (tor));

  tor->metadata_func = func;
  tor->metadata_func_user_data = user_data;
}


/**
***  File priorities
**/

void
tr_torrentInitFilePriority (tr_torrent *    tor,
                            tr_file_index_t fileIndex,
                            tr_priority_t   priority)
{
  tr_file * file;
  tr_piece_index_t i;

  assert (tr_isTorrent (tor));
  assert (fileIndex < tor->info.fileCount);
  assert (tr_isPriority (priority));

  file = &tor->info.files[fileIndex];
  file->priority = priority;
  for (i=file->firstPiece; i<=file->lastPiece; ++i)
    tor->info.pieces[i].priority = calculatePiecePriority (tor, i, fileIndex);
}

void
tr_torrentSetFilePriorities (tr_torrent             * tor,
                             const tr_file_index_t  * files,
                             tr_file_index_t          fileCount,
                             tr_priority_t            priority)
{
  tr_file_index_t i;
  assert (tr_isTorrent (tor));
  tr_torrentLock (tor);

  for (i=0; i<fileCount; ++i)
    if (files[i] < tor->info.fileCount)
      tr_torrentInitFilePriority (tor, files[i], priority);
  tr_torrentSetDirty (tor);
  tr_peerMgrRebuildRequests (tor);

  tr_torrentUnlock (tor);
}

tr_priority_t*
tr_torrentGetFilePriorities (const tr_torrent * tor)
{
  tr_file_index_t i;
  tr_priority_t * p;

  assert (tr_isTorrent (tor));

  p = tr_new0 (tr_priority_t, tor->info.fileCount);

  for (i=0; i<tor->info.fileCount; ++i)
    p[i] = tor->info.files[i].priority;

  return p;
}

/**
***  File DND
**/

static void
setFileDND (tr_torrent * tor, tr_file_index_t fileIndex, int doDownload)
{
  const int8_t dnd = !doDownload;
  tr_piece_index_t firstPiece;
  int8_t firstPieceDND;
  tr_piece_index_t lastPiece;
  int8_t lastPieceDND;
  tr_file_index_t  i;
  tr_file * file = &tor->info.files[fileIndex];

  file->dnd = dnd;
  firstPiece = file->firstPiece;
  lastPiece = file->lastPiece;

  /* can't set the first piece to DND unless
     every file using that piece is DND */
  firstPieceDND = dnd;
  if (fileIndex > 0)
    {
      for (i=fileIndex-1; firstPieceDND; --i)
        {
          if (tor->info.files[i].lastPiece != firstPiece)
            break;

          firstPieceDND = tor->info.files[i].dnd;
          if (!i)
            break;
        }
    }

  /* can't set the last piece to DND unless
     every file using that piece is DND */
  lastPieceDND = dnd;
  for (i=fileIndex+1; lastPieceDND && i<tor->info.fileCount; ++i)
    {
      if (tor->info.files[i].firstPiece != lastPiece)
        break;
      lastPieceDND = tor->info.files[i].dnd;
    }

  if (firstPiece == lastPiece)
    {
      tor->info.pieces[firstPiece].dnd = firstPieceDND && lastPieceDND;
    }
  else
    {
      tr_piece_index_t pp;
      tor->info.pieces[firstPiece].dnd = firstPieceDND;
      tor->info.pieces[lastPiece].dnd = lastPieceDND;
      for (pp=firstPiece+1; pp<lastPiece; ++pp)
        tor->info.pieces[pp].dnd = dnd;
    }
}

void
tr_torrentInitFileDLs (tr_torrent             * tor,
                       const tr_file_index_t  * files,
                       tr_file_index_t          fileCount,
                       bool                     doDownload)
{
  tr_file_index_t i;

  assert (tr_isTorrent (tor));

  tr_torrentLock (tor);

  for (i=0; i<fileCount; ++i)
    if (files[i] < tor->info.fileCount)
      setFileDND (tor, files[i], doDownload);

  tr_cpInvalidateDND (&tor->completion);

  tr_torrentUnlock (tor);
}

void
tr_torrentSetFileDLs (tr_torrent             * tor,
                      const tr_file_index_t  * files,
                      tr_file_index_t          fileCount,
                      bool                     doDownload)
{
  assert (tr_isTorrent (tor));
  tr_torrentLock (tor);

  tr_torrentInitFileDLs (tor, files, fileCount, doDownload);
  tr_torrentSetDirty (tor);
  tr_torrentRecheckCompleteness (tor);
  tr_peerMgrRebuildRequests (tor);

  tr_torrentUnlock (tor);
}

/***
****
***/

tr_priority_t
tr_torrentGetPriority (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tor->bandwidth.priority;
}

void
tr_torrentSetPriority (tr_torrent * tor, tr_priority_t priority)
{
  assert (tr_isTorrent (tor));
  assert (tr_isPriority (priority));

  if (tor->bandwidth.priority != priority)
    {
      tor->bandwidth.priority = priority;

      tr_torrentSetDirty (tor);
    }
}

/***
****
***/

void
tr_torrentSetPeerLimit (tr_torrent * tor,
                        uint16_t     maxConnectedPeers)
{
  assert (tr_isTorrent (tor));

  if (tor->maxConnectedPeers != maxConnectedPeers)
    {
      tor->maxConnectedPeers = maxConnectedPeers;

      tr_torrentSetDirty (tor);
    }
}

uint16_t
tr_torrentGetPeerLimit (const tr_torrent * tor)
{
  assert (tr_isTorrent (tor));

  return tor->maxConnectedPeers;
}

/***
****
***/

void
tr_torrentGetBlockLocation (const tr_torrent * tor,
                            tr_block_index_t   block,
                            tr_piece_index_t * piece,
                            uint32_t         * offset,
                            uint32_t         * length)
{
  uint64_t pos = block;
  pos *= tor->blockSize;
  *piece = pos / tor->info.pieceSize;
  *offset = pos - (*piece * tor->info.pieceSize);
  *length = tr_torBlockCountBytes (tor, block);
}


tr_block_index_t
_tr_block (const tr_torrent * tor,
           tr_piece_index_t   index,
           uint32_t           offset)
{
  tr_block_index_t ret;

  assert (tr_isTorrent (tor));

  ret = index;
  ret *= (tor->info.pieceSize / tor->blockSize);
  ret += offset / tor->blockSize;
  return ret;
}

bool
tr_torrentReqIsValid (const tr_torrent * tor,
                      tr_piece_index_t   index,
                      uint32_t           offset,
                      uint32_t           length)
{
  int err = 0;

  assert (tr_isTorrent (tor));

  if (index >= tor->info.pieceCount)
    err = 1;
  else if (length < 1)
    err = 2;
  else if ((offset + length) > tr_torPieceCountBytes (tor, index))
    err = 3;
  else if (length > MAX_BLOCK_SIZE)
    err = 4;
  else if (tr_pieceOffset (tor, index, offset, length) > tor->info.totalSize)
    err = 5;

  if (err)
    tr_logAddTorDbg (tor, "index %lu offset %lu length %lu err %d\n",
                     (unsigned long)index,
                     (unsigned long)offset,
                     (unsigned long)length,
                     err);

  return !err;
}

uint64_t
tr_pieceOffset (const tr_torrent * tor,
                tr_piece_index_t   index,
                uint32_t           offset,
                uint32_t           length)
{
  uint64_t ret;

  assert (tr_isTorrent (tor));

  ret = tor->info.pieceSize;
  ret *= index;
  ret += offset;
  ret += length;
  return ret;
}

void
tr_torGetFileBlockRange (const tr_torrent        * tor,
                         const tr_file_index_t     file,
                         tr_block_index_t        * first,
                         tr_block_index_t        * last)
{
  const tr_file * f = &tor->info.files[file];
  uint64_t offset = f->offset;

  *first = offset / tor->blockSize;

  if (!f->length)
    {
      *last = *first;
    }
  else
    {
      offset += f->length - 1;
      *last = offset / tor->blockSize;
    }
}

void
tr_torGetPieceBlockRange (const tr_torrent        * tor,
                          const tr_piece_index_t    piece,
                          tr_block_index_t        * first,
                          tr_block_index_t        * last)
{
  uint64_t offset = tor->info.pieceSize;
  offset *= piece;
  *first = offset / tor->blockSize;
  offset += (tr_torPieceCountBytes (tor, piece) - 1);
  *last = offset / tor->blockSize;
}


/***
****
***/

void
tr_torrentSetPieceChecked (tr_torrent * tor, tr_piece_index_t pieceIndex)
{
  assert (tr_isTorrent (tor));
  assert (pieceIndex < tor->info.pieceCount);

  tor->info.pieces[pieceIndex].timeChecked = tr_time ();
}

void
tr_torrentSetChecked (tr_torrent * tor, time_t when)
{
  tr_piece_index_t i, n;

  assert (tr_isTorrent (tor));

  for (i=0, n=tor->info.pieceCount; i!=n; ++i)
    tor->info.pieces[i].timeChecked = when;
}

bool
tr_torrentCheckPiece (tr_torrent * tor, tr_piece_index_t pieceIndex)
{
  const bool pass = tr_ioTestPiece (tor, pieceIndex);

  tr_deeplog_tor (tor, "[LAZY] tr_torrentCheckPiece tested piece %zu, pass==%d", (size_t)pieceIndex, (int)pass);
  tr_torrentSetHasPiece (tor, pieceIndex, pass);
  tr_torrentSetPieceChecked (tor, pieceIndex);
  tor->anyDate = tr_time ();
  tr_torrentSetDirty (tor);

  return pass;
}

time_t
tr_torrentGetFileMTime (const tr_torrent * tor, tr_file_index_t i)
{
  time_t mtime = 0;

  if (!tr_fdFileGetCachedMTime (tor->session, tor->uniqueId, i, &mtime))
    tr_torrentFindFile2 (tor, i, NULL, NULL, &mtime);

  return mtime;
}

bool
tr_torrentPieceNeedsCheck (const tr_torrent * tor, tr_piece_index_t p)
{
  uint64_t unused;
  tr_file_index_t f;
  const tr_info * inf = tr_torrentInfo (tor);

  /* if we've never checked this piece, then it needs to be checked */
  if (!inf->pieces[p].timeChecked)
    return true;

  /* If we think we've completed one of the files in this piece,
   * but it's been modified since we last checked it,
   * then it needs to be rechecked */
  tr_ioFindFileLocation (tor, p, 0, &f, &unused);
  for (; f < inf->fileCount && pieceHasFile (p, &inf->files[f]); ++f)
    if (tr_cpFileIsComplete (&tor->completion, f))
      if (tr_torrentGetFileMTime (tor, f) > inf->pieces[p].timeChecked)
        return true;

  return false;
}

/***
****
***/

static int
compareTrackerByTier (const void * va, const void * vb)
{
  const tr_tracker_info * a = va;
  const tr_tracker_info * b = vb;

  /* sort by tier */
  if (a->tier != b->tier)
    return a->tier - b->tier;

  /* get the effects of a stable sort by comparing the two elements' addresses */
  return a - b;
}

bool
tr_torrentSetAnnounceList (tr_torrent             * tor,
                           const tr_tracker_info  * trackers_in,
                           int                      trackerCount)
{
  int i;
  tr_variant metainfo;
  bool ok = true;
  tr_tracker_info * trackers;

  tr_torrentLock (tor);

  assert (tr_isTorrent (tor));

  /* ensure the trackers' tiers are in ascending order */
  trackers = tr_memdup (trackers_in, sizeof (tr_tracker_info) * trackerCount);
  qsort (trackers, trackerCount, sizeof (tr_tracker_info), compareTrackerByTier);

  /* look for bad URLs */
  for (i=0; ok && i<trackerCount; ++i)
    if (!tr_urlIsValidTracker (trackers[i].announce))
      ok = false;

  /* save to the .torrent file */
  if (ok && tr_variantFromFile (&metainfo, TR_VARIANT_FMT_BENC, tor->info.torrent, NULL))
    {
      bool hasInfo;
      tr_info tmpInfo;

      /* remove the old fields */
      tr_variantDictRemove (&metainfo, TR_KEY_announce);
      tr_variantDictRemove (&metainfo, TR_KEY_announce_list);

      /* add the new fields */
      if (trackerCount > 0)
        {
          tr_variantDictAddStr (&metainfo, TR_KEY_announce, trackers[0].announce);
        }
      if (trackerCount > 1)
        {
          int i;
          int prevTier = -1;
          tr_variant * tier = NULL;
          tr_variant * announceList = tr_variantDictAddList (&metainfo, TR_KEY_announce_list, 0);

          for (i=0; i<trackerCount; ++i)
            {
              if (prevTier != trackers[i].tier)
                {
                  prevTier = trackers[i].tier;
                  tier = tr_variantListAddList (announceList, 0);
                }

              tr_variantListAddStr (tier, trackers[i].announce);
            }
        }

      /* try to parse it back again, to make sure it's good */
      memset (&tmpInfo, 0, sizeof (tr_info));
      if (tr_metainfoParse (tor->session, &metainfo, &tmpInfo,
                            &hasInfo, &tor->infoDictLength))
        {
          /* it's good, so keep these new trackers and free the old ones */

          tr_info swap;
          swap.trackers = tor->info.trackers;
          swap.trackerCount = tor->info.trackerCount;
          tor->info.trackers = tmpInfo.trackers;
          tor->info.trackerCount = tmpInfo.trackerCount;
          tmpInfo.trackers = swap.trackers;
          tmpInfo.trackerCount = swap.trackerCount;

          tr_metainfoFree (&tmpInfo);
          tr_variantToFile (&metainfo, TR_VARIANT_FMT_BENC, tor->info.torrent);
        }

      /* cleanup */
      tr_variantFree (&metainfo);

      /* if we had a tracker-related error on this torrent,
       * and that tracker's been removed,
       * then clear the error */
      if ((tor->error == TR_STAT_TRACKER_WARNING)
          || (tor->error == TR_STAT_TRACKER_ERROR))
        {
          bool clear = true;

          for (i=0; clear && i<trackerCount; ++i)
            if (!strcmp (trackers[i].announce, tor->errorTracker))
              clear = false;

          if (clear)
            tr_torrentClearError (tor);
        }

      /* tell the announcer to reload this torrent's tracker list */
      tr_announcerResetTorrent (tor->session->announcer, tor);
    }

  tr_torrentUnlock (tor);

  tr_free (trackers);
  return ok;
}

/**
***
**/

void
tr_torrentSetAddedDate (tr_torrent * tor,
                        time_t       t)
{
  assert (tr_isTorrent (tor));

  tor->addedDate = t;
  tor->anyDate = MAX (tor->anyDate, tor->addedDate);
}

void
tr_torrentSetActivityDate (tr_torrent * tor, time_t t)
{
  assert (tr_isTorrent (tor));

  tor->activityDate = t;
  tor->anyDate = MAX (tor->anyDate, tor->activityDate);
}

void
tr_torrentSetDoneDate (tr_torrent * tor,
                       time_t       t)
{
  assert (tr_isTorrent (tor));

  tor->doneDate = t;
  tor->anyDate = MAX (tor->anyDate, tor->doneDate);
}

/**
***
**/

uint64_t
tr_torrentGetBytesLeftToAllocate (const tr_torrent * tor)
{
  tr_file_index_t i;
  uint64_t bytesLeft = 0;

  assert (tr_isTorrent (tor));

  for (i=0; i<tor->info.fileCount; ++i)
    {
      if (!tor->info.files[i].dnd)
        {
          tr_sys_path_info info;
          const uint64_t length = tor->info.files[i].length;
          char * path = tr_torrentFindFile (tor, i);

          bytesLeft += length;

          if (path != NULL &&
              tr_sys_path_get_info (path, 0, &info, NULL) &&
              info.type == TR_SYS_PATH_IS_FILE &&
              info.size <= length)
            bytesLeft -= info.size;

          tr_free (path);
        }
    }

  return bytesLeft;
}

/****
*****  Removing the torrent's local data
****/

static bool
isJunkFile (const char * base)
{
  int i;
  static const char * files[] = { ".DS_Store", "desktop.ini", "Thumbs.db" };
  static const int file_count = sizeof (files) / sizeof (files[0]);

  for (i=0; i<file_count; ++i)
    if (!strcmp (base, files[i]))
      return true;

#ifdef __APPLE__
  /* check for resource forks. <http://support.apple.com/kb/TA20578> */
  if (!memcmp (base, "._", 2))
    return true;
#endif

  return false;
}

static void
removeEmptyFoldersAndJunkFiles (const char * folder)
{
  tr_sys_dir_t odir;

  if ((odir = tr_sys_dir_open (folder, NULL)) != TR_BAD_SYS_DIR)
    {
      const char * name;
      while ((name = tr_sys_dir_read_name (odir, NULL)) != NULL)
        {
          if (strcmp (name, ".") != 0 && strcmp (name, "..") != 0)
            {
              tr_sys_path_info info;
              char * filename = tr_buildPath (folder, name, NULL);

              if (tr_sys_path_get_info (filename, 0, &info, NULL) &&
                  info.type == TR_SYS_PATH_IS_DIRECTORY)
                removeEmptyFoldersAndJunkFiles (filename);
              else if (isJunkFile (name))
                tr_sys_path_remove (filename, NULL);

              tr_free (filename);
            }
        }

      tr_sys_path_remove (folder, NULL);
      tr_sys_dir_close (odir, NULL);
    }
}

/**
 * This convoluted code does something (seemingly) simple:
 * remove the torrent's local files.
 *
 * Fun complications:
 * 1. Try to preserve the directory hierarchy in the recycle bin.
 * 2. If there are nontorrent files, don't delete them...
 * 3. ...unless the other files are "junk", such as .DS_Store
 */
static void
deleteLocalData (tr_torrent * tor, tr_fileFunc func)
{
  int i, n;
  tr_file_index_t f;
  char * base;
  tr_sys_dir_t odir;
  char * tmpdir = NULL;
  tr_ptrArray files = TR_PTR_ARRAY_INIT;
  tr_ptrArray folders = TR_PTR_ARRAY_INIT;
  PtrArrayCompareFunc vstrcmp = (PtrArrayCompareFunc)strcmp;
  const char * const top = tor->currentDir;

  /* don't try to delete local data if the directory's gone missing */
  if (!tr_sys_path_exists (top, NULL))
    return;

  /* if it's a magnet link, there's nothing to move... */
  if (!tr_torrentHasMetadata (tor))
    return;

  /***
  ****  Move the local data to a new tmpdir
  ***/

  base = tr_strdup_printf ("%s__XXXXXX", tr_torrentName (tor));
  tmpdir = tr_buildPath (top, base, NULL);
  tr_sys_dir_create_temp (tmpdir, NULL);
  tr_free (base);

  for (f=0; f<tor->info.fileCount; ++f)
    {
      char * filename;

      /* try to find the file, looking in the partial and download dirs */
      filename = tr_buildPath (top, tor->info.files[f].name, NULL);
      if (!tr_sys_path_exists (filename, NULL))
        {
          char * partial = tr_torrentBuildPartial (tor, f);
          tr_free (filename);
          filename = tr_buildPath (top, partial, NULL);
          tr_free (partial);
          if (!tr_sys_path_exists (filename, NULL))
            {
              tr_free (filename);
              filename = NULL;
            }
        }

      /* if we found the file, move it */
      if (filename != NULL)
        {
          char * target = tr_buildPath (tmpdir, tor->info.files[f].name, NULL);
          tr_moveFile (filename, target, NULL);
          tr_ptrArrayAppend (&files, target);
          tr_free (filename);
        }
    }

  /***
  ****  Remove tmpdir.
  ****
  ****  Try deleting the top-level files & folders to preserve
  ****  the directory hierarchy in the recycle bin.
  ****  If case that fails -- for example, rmdir () doesn't
  ****  delete nonempty folders -- go from the bottom up too.
  ***/

  /* try deleting the local data's top-level files & folders */
  if ((odir = tr_sys_dir_open (tmpdir, NULL)) != TR_BAD_SYS_DIR)
    {
      const char * name;
      while ((name = tr_sys_dir_read_name (odir, NULL)) != NULL)
        {
          if (strcmp (name, ".") != 0 && strcmp (name, "..") != 0)
            {
              char * file = tr_buildPath (tmpdir, name, NULL);
              func (file, NULL);
              tr_free (file);
            }
        }
      tr_sys_dir_close (odir, NULL);
    }

  /* go from the bottom up */
  for (i=0, n=tr_ptrArraySize (&files); i<n; ++i)
    {
      char * walk = tr_strdup (tr_ptrArrayNth (&files, i));
      while (tr_sys_path_exists (walk, NULL) && !tr_sys_path_is_same (tmpdir, walk, NULL))
        {
          char * tmp = tr_sys_path_dirname (walk, NULL);
          func (walk, NULL);
          tr_free (walk);
          walk = tmp;
        }
      tr_free (walk);
    }

  /***
  ****  The local data has been removed.
  ****  What's left in top are empty folders, junk, and user-generated files.
  ****  Remove the first two categories and leave the third.
  ***/

  /* build a list of 'top's child directories that belong to this torrent */
  for (f=0; f<tor->info.fileCount; ++f)
    {
      char * dir;
      char * filename;

      /* get the directory that this file goes in... */
      filename = tr_buildPath (top, tor->info.files[f].name, NULL);
      dir = tr_sys_path_dirname (filename, NULL);
      tr_free (filename);

      /* walk up the directory tree until we reach 'top' */
      if (!tr_sys_path_is_same (top, dir, NULL) && strcmp (top, dir) != 0)
        {
          for (;;)
            {
              char * parent = tr_sys_path_dirname (dir, NULL);
              if (tr_sys_path_is_same (top, parent, NULL) || strcmp (top, parent) == 0)
                {
                  if (tr_ptrArrayFindSorted (&folders, dir, vstrcmp) == NULL)
                    tr_ptrArrayInsertSorted (&folders, tr_strdup(dir), vstrcmp);
                  tr_free (parent);
                  break;
                }

              /* walk upwards to parent */
              tr_free (dir);
              dir = parent;
            }
        }

      tr_free (dir);
    }

  for (i=0, n=tr_ptrArraySize (&folders); i<n; ++i)
    removeEmptyFoldersAndJunkFiles (tr_ptrArrayNth (&folders, i));

  /* cleanup */
  tr_sys_path_remove (tmpdir, NULL);
  tr_free (tmpdir);
  tr_ptrArrayDestruct (&folders, tr_free);
  tr_ptrArrayDestruct (&files, tr_free);
}

static void
tr_torrentDeleteLocalData (tr_torrent * tor, tr_fileFunc func)
{
  assert (tr_isTorrent (tor));

  if (func == NULL)
    func = tr_sys_path_remove;

  /* close all the files because we're about to delete them */
  tr_cacheFlushTorrent (tor->session->cache, tor);
  tr_fdTorrentClose (tor->session, tor->uniqueId);

  deleteLocalData (tor, func);
}

/***
****
***/

struct LocationData
{
  bool move_from_old_location;
  volatile int * setme_state;
  volatile double * setme_progress;
  char * location;
  tr_torrent * tor;
};

static void
setLocation (void * vdata)
{
  bool err = false;
  struct LocationData * data = vdata;
  tr_torrent * tor = data->tor;
  const bool do_move = data->move_from_old_location;
  const char * location = data->location;
  double bytesHandled = 0;
  tr_torrentLock (tor);

  assert (tr_isTorrent (tor));

  tr_logAddDebug ("Moving \"%s\" location from currentDir \"%s\" to \"%s\"",
                  tr_torrentName (tor), tor->currentDir, location);

  tr_sys_dir_create (location, TR_SYS_DIR_CREATE_PARENTS, 0777, NULL);

  if (!tr_sys_path_is_same (location, tor->currentDir, NULL))
    {
      tr_file_index_t i;

      /* bad idea to move files while they're being verified... */
      tr_verifyRemove (tor);

      /* try to move the files.
       * FIXME: there are still all kinds of nasty cases, like what
       * if the target directory runs out of space halfway through... */
      for (i=0; !err && i<tor->info.fileCount; ++i)
        {
          char * sub;
          const char * oldbase;
          const tr_file * f = &tor->info.files[i];

          if (tr_torrentFindFile2 (tor, i, &oldbase, &sub, NULL))
            {
              char * oldpath = tr_buildPath (oldbase, sub, NULL);
              char * newpath = tr_buildPath (location, sub, NULL);

              tr_logAddDebug ("Found file #%d: %s", (int)i, oldpath);

              if (do_move && !tr_sys_path_is_same (oldpath, newpath, NULL))
                {
                  tr_error * error = NULL;

                  tr_logAddTorInfo (tor, "moving \"%s\" to \"%s\"", oldpath, newpath);
                  if (!tr_moveFile (oldpath, newpath, &error))
                    {
                      err = true;
                      tr_logAddTorErr (tor, "error moving \"%s\" to \"%s\": %s",
                                       oldpath, newpath, error->message);
                      tr_error_free (error);
                    }
                }

              tr_free (newpath);
              tr_free (oldpath);
              tr_free (sub);
            }

          if (data->setme_progress != NULL)
            {
              bytesHandled += f->length;
              *data->setme_progress = bytesHandled / tor->info.totalSize;
            }
        }

      if (!err)
        {
          /* blow away the leftover subdirectories in the old location */
          if (do_move)
            tr_torrentDeleteLocalData (tor, tr_sys_path_remove);

          /* set the new location and reverify */
          tr_torrentSetDownloadDir (tor, location);
        }
    }

  if (!err && do_move)
    {
      tr_free (tor->incompleteDir);
      tor->incompleteDir = NULL;
      tor->currentDir = tor->downloadDir;
    }

  if (data->setme_state != NULL)
    *data->setme_state = err ? TR_LOC_ERROR : TR_LOC_DONE;

  /* cleanup */
  tr_torrentUnlock (tor);
  tr_free (data->location);
  tr_free (data);
}

void
tr_torrentSetLocation (tr_torrent       * tor,
                       const char       * location,
                       bool               move_from_old_location,
                       volatile double  * setme_progress,
                       volatile int     * setme_state)
{
  struct LocationData * data;

  assert (tr_isTorrent (tor));

  if (setme_state != NULL)
    *setme_state = TR_LOC_MOVING;

  if (setme_progress != NULL)
    *setme_progress = 0;

  /* run this in the libtransmission thread */
  data = tr_new (struct LocationData, 1);
  data->tor = tor;
  data->location = tr_strdup (location);
  data->move_from_old_location = move_from_old_location;
  data->setme_state = setme_state;
  data->setme_progress = setme_progress;
  tr_runInEventThread (tor->session, setLocation, data);
}

/***
****
***/

static void
tr_torrentFileCompleted (tr_torrent * tor, tr_file_index_t fileIndex)
{
  char * sub;
  const char * base;
  const tr_info * inf = &tor->info;
  const tr_file * f = &inf->files[fileIndex];
  tr_piece * p;
  const tr_piece * pend;
  const time_t now = tr_time ();

  /* close the file so that we can reopen in read-only mode as needed */
  tr_cacheFlushFile (tor->session->cache, tor, fileIndex);
  tr_fdFileClose (tor->session, tor, fileIndex);

  /* now that the file is complete and closed, we can start watching its
   * mtime timestamp for changes to know if we need to reverify pieces */
  for (p=&inf->pieces[f->firstPiece], pend=&inf->pieces[f->lastPiece]; p!=pend; ++p)
    p->timeChecked = now;

  /* if the torrent's current filename isn't the same as the one in the
   * metadata -- for example, if it had the ".part" suffix appended to
   * it until now -- then rename it to match the one in the metadata */
  if (tr_torrentFindFile2 (tor, fileIndex, &base, &sub, NULL))
    {
      if (strcmp (sub, f->name))
        {
          char * oldpath = tr_buildPath (base, sub, NULL);
          char * newpath = tr_buildPath (base, f->name, NULL);
          tr_error * error = NULL;

          if (!tr_sys_path_rename (oldpath, newpath, &error))
            {
              tr_logAddTorErr (tor, "Error moving \"%s\" to \"%s\": %s", oldpath, newpath, error->message);
              tr_error_free (error);
            }

          tr_free (newpath);
          tr_free (oldpath);
        }

      tr_free (sub);
    }
}

static void
tr_torrentPieceCompleted (tr_torrent * tor, tr_piece_index_t pieceIndex)
{
  tr_file_index_t i;

  tr_peerMgrPieceCompleted (tor, pieceIndex);

  /* if this piece completes any file, invoke the fileCompleted func for it */
  for (i=0; i<tor->info.fileCount; ++i)
    {
      const tr_file * file = &tor->info.files[i];

      if ((file->firstPiece <= pieceIndex) && (pieceIndex <= file->lastPiece))
        if (tr_cpFileIsComplete (&tor->completion, i))
          tr_torrentFileCompleted (tor, i);
    }
}

void
tr_torrentGotBlock (tr_torrent * tor, tr_block_index_t block)
{
  const bool block_is_new = !tr_torrentBlockIsComplete (tor, block);

  assert (tr_isTorrent (tor));
  assert (tr_amInEventThread (tor->session));

  if (block_is_new)
    {
      tr_piece_index_t p;

      tr_cpBlockAdd (&tor->completion, block);
      tr_torrentSetDirty (tor);

      p = tr_torBlockPiece (tor, block);
      if (tr_torrentPieceIsComplete (tor, p))
        {
          tr_logAddTorDbg (tor, "[LAZY] checking just-completed piece %zu", (size_t)p);

          if (tr_torrentCheckPiece (tor, p))
            {
              tr_torrentPieceCompleted (tor, p);
            }
          else
            {
              const uint32_t n = tr_torPieceCountBytes (tor, p);
              tr_logAddTorErr (tor, _("Piece %"PRIu32", which was just downloaded, failed its checksum test"), p);
              tor->corruptCur += n;
              tor->downloadedCur -= MIN (tor->downloadedCur, n);
              tr_peerMgrGotBadPiece (tor, p);
            }
        }
    }
  else
    {
      const uint32_t n = tr_torBlockCountBytes (tor, block);
      tor->downloadedCur -= MIN (tor->downloadedCur, n);
      tr_logAddTorDbg (tor, "we have this block already...");
    }
}

/***
****
***/

bool
tr_torrentFindFile2 (const tr_torrent * tor, tr_file_index_t fileNum,
                     const char ** base, char ** subpath, time_t * mtime)
{
  char * part = NULL;
  const tr_file * file;
  const char * b = NULL;
  const char * s = NULL;
  tr_sys_path_info file_info;

  assert (tr_isTorrent (tor));
  assert (fileNum < tor->info.fileCount);

  file = &tor->info.files[fileNum];

  /* look in the download dir... */
  if (b == NULL)
    {
      char * filename = tr_buildPath (tor->downloadDir, file->name, NULL);
      if (tr_sys_path_get_info (filename, 0, &file_info, NULL))
        {
          b = tor->downloadDir;
          s = file->name;
        }
      tr_free (filename);
    }

  /* look in the incomplete dir... */
  if ((b == NULL) && (tor->incompleteDir != NULL))
    {
      char * filename = tr_buildPath (tor->incompleteDir, file->name, NULL);
      if (tr_sys_path_get_info (filename, 0, &file_info, NULL))
        {
          b = tor->incompleteDir;
          s = file->name;
        }
      tr_free (filename);
    }

  if (b == NULL)
    part = tr_torrentBuildPartial (tor, fileNum);

  /* look for a .part file in the incomplete dir... */
  if ((b == NULL) && (tor->incompleteDir != NULL))
    {
      char * filename = tr_buildPath (tor->incompleteDir, part, NULL);
      if (tr_sys_path_get_info (filename, 0, &file_info, NULL))
        {
          b = tor->incompleteDir;
          s = part;
        }
      tr_free (filename);
    }

  /* look for a .part file in the download dir... */
  if (b == NULL)
    {
      char * filename = tr_buildPath (tor->downloadDir, part, NULL);
      if (tr_sys_path_get_info (filename, 0, &file_info, NULL))
        {
          b = tor->downloadDir;
          s = part;
        }
      tr_free (filename);
    }

  /* return the results */
  if (base != NULL)
    *base = b;
  if (subpath != NULL)
    *subpath = tr_strdup (s);
  if (mtime != NULL)
    *mtime = file_info.last_modified_at;


  /* cleanup */
  tr_free (part);
  return b != NULL;
}

char*
tr_torrentFindFile (const tr_torrent * tor, tr_file_index_t fileNum)
{
  char * subpath;
  char * ret = NULL;
  const char * base;

  if (tr_torrentFindFile2 (tor, fileNum, &base, &subpath, NULL))
    {
      ret = tr_buildPath (base, subpath, NULL);
      tr_free (subpath);
    }

    return ret;
}

/* Decide whether we should be looking for files in downloadDir or incompleteDir. */
static void
refreshCurrentDir (tr_torrent * tor)
{
  const char * dir = NULL;

  if (tor->incompleteDir == NULL)
    dir = tor->downloadDir;
  else if (!tr_torrentHasMetadata (tor)) /* no files to find */
    dir = tor->incompleteDir;
  else if (!tr_torrentFindFile2 (tor, 0, &dir, NULL, NULL))
    dir = tor->incompleteDir;

  assert (dir != NULL);
  assert ((dir == tor->downloadDir) || (dir == tor->incompleteDir));
  tor->currentDir = dir;
}

char*
tr_torrentBuildPartial (const tr_torrent * tor, tr_file_index_t fileNum)
{
  return tr_strdup_printf ("%s.part", tor->info.files[fileNum].name);
}

/***
****
***/

static int
compareTorrentByQueuePosition (const void * va, const void * vb)
{
  const tr_torrent * a = * (const tr_torrent * const *) va;
  const tr_torrent * b = * (const tr_torrent * const *) vb;

  return a->queuePosition - b->queuePosition;
}

#ifndef NDEBUG
static bool
queueIsSequenced (tr_session * session)
{
  int i;
  int n;
  bool is_sequenced;
  tr_torrent ** torrents;

  n = 0;
  torrents = tr_sessionGetTorrents (session, &n);
  qsort (torrents, n, sizeof (tr_torrent *), compareTorrentByQueuePosition);

#if 0
  fprintf (stderr, "%s", "queue: ");
  for (i=0; i<n; ++i)
    fprintf (stderr, "%d ", tmp[i]->queuePosition);
  fputc ('\n', stderr);
#endif

  /* test them */
  is_sequenced = true;
  for (i=0; is_sequenced && i<n; ++i)
    if (torrents[i]->queuePosition != i)
      is_sequenced = false;

  tr_free (torrents);
  return is_sequenced;
}
#endif

int
tr_torrentGetQueuePosition (const tr_torrent * tor)
{
  return tor->queuePosition;
}

void
tr_torrentSetQueuePosition (tr_torrent * tor, int pos)
{
  int back = -1;
  tr_torrent * walk;
  const int old_pos = tor->queuePosition;
  const time_t now = tr_time ();

  if (pos < 0)
    pos = 0;

  tor->queuePosition = -1;

  walk = NULL;
  while ((walk = tr_torrentNext (tor->session, walk)))
    {
      if (old_pos < pos)
        {
          if ((old_pos <= walk->queuePosition) && (walk->queuePosition <= pos))
            {
              walk->queuePosition--;
              walk->anyDate = now;
            }
        }

      if (old_pos > pos)
        {
          if ((pos <= walk->queuePosition) && (walk->queuePosition < old_pos))
            {
              walk->queuePosition++;
              walk->anyDate = now;
            }
        }

      if (back < walk->queuePosition)
        {
          back = walk->queuePosition;
        }
    }

  tor->queuePosition = MIN (pos, (back+1));
  tor->anyDate = now;

  assert (queueIsSequenced (tor->session));
}

void
tr_torrentsQueueMoveTop (tr_torrent ** torrents_in, int n)
{
  int i;
  tr_torrent ** torrents = tr_memdup (torrents_in, sizeof (tr_torrent *) * n);
  qsort (torrents, n, sizeof (tr_torrent *), compareTorrentByQueuePosition);
  for (i=n-1; i>=0; --i)
    tr_torrentSetQueuePosition (torrents[i], 0);
  tr_free (torrents);
}

void
tr_torrentsQueueMoveUp (tr_torrent ** torrents_in, int n)
{
  int i;
  tr_torrent ** torrents;

  torrents = tr_memdup (torrents_in, sizeof (tr_torrent *) * n);
  qsort (torrents, n, sizeof (tr_torrent *), compareTorrentByQueuePosition);
  for (i=0; i<n; ++i)
    tr_torrentSetQueuePosition (torrents[i], torrents[i]->queuePosition - 1);

  tr_free (torrents);
}

void
tr_torrentsQueueMoveDown (tr_torrent ** torrents_in, int n)
{
  int i;
  tr_torrent ** torrents;

  torrents = tr_memdup (torrents_in, sizeof (tr_torrent *) * n);
  qsort (torrents, n, sizeof (tr_torrent *), compareTorrentByQueuePosition);
  for (i=n-1; i>=0; --i)
    tr_torrentSetQueuePosition (torrents[i], torrents[i]->queuePosition + 1);

  tr_free (torrents);
}

void
tr_torrentsQueueMoveBottom (tr_torrent ** torrents_in, int n)
{
  int i;
  tr_torrent ** torrents;

  torrents = tr_memdup (torrents_in, sizeof (tr_torrent *) * n);
  qsort (torrents, n, sizeof (tr_torrent *), compareTorrentByQueuePosition);
  for (i=0; i<n; ++i)
    tr_torrentSetQueuePosition (torrents[i], INT_MAX);

  tr_free (torrents);
}

static void
torrentSetQueued (tr_torrent * tor, bool queued)
{
  assert (tr_isTorrent (tor));
  assert (tr_isBool (queued));

  if (tr_torrentIsQueued (tor) != queued)
    {
      tor->isQueued = queued;
      tor->anyDate = tr_time ();
      tr_torrentSetDirty (tor);
    }
}

void
tr_torrentSetQueueStartCallback (tr_torrent * torrent, void (*callback)(tr_torrent *, void *), void * user_data)
{
  torrent->queue_started_callback = callback;
  torrent->queue_started_user_data = user_data;
}


/***
****
****  RENAME
****
***/

static bool
renameArgsAreValid (const char * oldpath, const char * newname)
{
  return (oldpath && *oldpath)
      && (newname && *newname)
      && (strcmp (newname, "."))
      && (strcmp (newname, ".."))
      && (strchr (newname, TR_PATH_DELIMITER) == NULL);
}

static tr_file_index_t *
renameFindAffectedFiles (tr_torrent * tor, const char * oldpath, size_t * setme_n)
{
  size_t n;
  size_t oldpath_len;
  tr_file_index_t i;
  tr_file_index_t * indices = tr_new0 (tr_file_index_t, tor->info.fileCount);

  n = 0;
  oldpath_len = strlen (oldpath);
  for (i=0; i!=tor->info.fileCount; ++i)
    {
      const char * name = tor->info.files[i].name;
      const size_t len = strlen (name);
      if ((len == oldpath_len || (len > oldpath_len && name[oldpath_len] == '/')) &&
          !memcmp (oldpath, name, oldpath_len))
        indices[n++] = i;
    }

  *setme_n = n;
  return indices;
}

static int
renamePath (tr_torrent  * tor,
            const char  * oldpath,
            const char  * newname)
{
  char * src;
  const char * base;
  int err = 0;

  if (!tr_torrentIsSeed(tor) && (tor->incompleteDir != NULL))
    base = tor->incompleteDir;
  else
    base = tor->downloadDir;

  src = tr_buildPath (base, oldpath, NULL);
  if (!tr_sys_path_exists (src, NULL)) /* check for it as a partial */
    {
      char * tmp = tr_strdup_printf ("%s.part", src);
      tr_free (src);
      src = tmp;
    }

  if (tr_sys_path_exists (src, NULL))
    {
      int tmp;
      bool tgt_exists;
      char * parent = tr_sys_path_dirname (src, NULL);
      char * tgt;

      if (tr_str_has_suffix (src, ".part"))
        tgt = tr_strdup_printf ("%s" TR_PATH_DELIMITER_STR "%s.part", parent, newname);
      else
        tgt = tr_buildPath (parent, newname, NULL);

      tmp = errno;
      tgt_exists = tr_sys_path_exists (tgt, NULL);
      errno = tmp;

      if (!tgt_exists)
        {
          tr_error * error = NULL;

          tmp = errno;
          if (!tr_sys_path_rename (src, tgt, &error))
            {
              err = error->code;
              tr_error_free (error);
            }
          errno = tmp;
        }

      tr_free (tgt);
      tr_free (parent);
    }

  tr_free (src);

  return err;
}

static void
renameTorrentFileString (tr_torrent       * tor,
                         const char       * oldpath,
                         const char       * newname,
                         tr_file_index_t    fileIndex)
{
  char * name;
  tr_file * file = &tor->info.files[fileIndex];
  const size_t oldpath_len = strlen (oldpath);

  if (strchr (oldpath, TR_PATH_DELIMITER) == NULL)
    {
      if (oldpath_len >= strlen(file->name))
        name = tr_buildPath (newname, NULL);
      else
        name = tr_buildPath (newname, file->name + oldpath_len + 1, NULL);
    }
  else
    {
      char * tmp = tr_sys_path_dirname (oldpath, NULL);

      if (oldpath_len >= strlen(file->name))
        name = tr_buildPath (tmp, newname, NULL);
      else
        name = tr_buildPath (tmp, newname, file->name + oldpath_len + 1, NULL);

      tr_free (tmp);
    }

  if (!strcmp (file->name, name))
    {
      tr_free (name);
    }
  else
    {
      tr_free (file->name);
      file->name = name;
      file->is_renamed = true;
    }
}

struct rename_data
{
  tr_torrent * tor;
  char * oldpath;
  char * newname;
  tr_torrent_rename_done_func callback;
  void * callback_user_data;
};

static void
torrentRenamePath (void * vdata)
{
  int error = 0;
  struct rename_data * data = vdata;
  tr_torrent * const tor = data->tor;
  const char * const oldpath = data->oldpath;
  const char * const newname = data->newname;

  /***
  ****
  ***/

  assert (tr_isTorrent (tor));

  if (!renameArgsAreValid (oldpath, newname))
    {
      error = EINVAL;
    }
  else
    {
      size_t n;
      tr_file_index_t * file_indices;

      file_indices = renameFindAffectedFiles (tor, oldpath, &n);
      if (n == 0)
        {
          error = EINVAL;
        }
      else
        {
          size_t i;

          error = renamePath (tor, oldpath, newname);

          if (!error)
            {
              /* update tr_info.files */
              for (i=0; i<n; ++i)
                renameTorrentFileString(tor, oldpath, newname, file_indices[i]);

              /* update tr_info.name if user changed the toplevel */
              if ((n == tor->info.fileCount) && (strchr(oldpath,'/')==NULL))
                {
                  tr_free (tor->info.name);
                  tor->info.name = tr_strdup (newname);
                }

              tr_torrentSetDirty (tor);
            }
        }

      tr_free (file_indices);
    }


  /***
  ****
  ***/

  tor->anyDate = tr_time ();

  /* callback */
  if (data->callback != NULL)
    (*data->callback)(tor, data->oldpath, data->newname, error, data->callback_user_data);

  /* cleanup */
  tr_free (data->oldpath);
  tr_free (data->newname);
  tr_free (data);
}

void
tr_torrentRenamePath (tr_torrent                  * tor,
                      const char                  * oldpath,
                      const char                  * newname,
                      tr_torrent_rename_done_func   callback,
                      void                        * callback_user_data)
{
  struct rename_data * data;

  data = tr_new0 (struct rename_data, 1);
  data->tor = tor;
  data->oldpath = tr_strdup (oldpath);
  data->newname = tr_strdup (newname);
  data->callback = callback;
  data->callback_user_data = callback_user_data;

  tr_runInEventThread (tor->session, torrentRenamePath, data);
}
