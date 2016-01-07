/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* memcpy (), memset (), memcmp () */

#include <event2/buffer.h>

#include "transmission.h"
#include "crypto-utils.h" /* tr_sha1 () */
#include "file.h"
#include "log.h"
#include "magnet.h"
#include "metainfo.h"
#include "resume.h"
#include "torrent.h"
#include "torrent-magnet.h"
#include "utils.h"
#include "variant.h"
#include "web.h"

#define dbgmsg(tor, ...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        tr_logAddDeep (__FILE__, __LINE__, tr_torrentName (tor), __VA_ARGS__); \
    } \
  while (0)

/***
****
***/

enum
{
  /* don't ask for the same metadata piece more than this often */
  MIN_REPEAT_INTERVAL_SECS = 3
};

struct metadata_node
{
  time_t requestedAt;
  int piece;
};

struct tr_incomplete_metadata
{
  uint8_t * metadata;
  int metadata_size;
  int pieceCount;

  /** sorted from least to most recently requested */
  struct metadata_node * piecesNeeded;
  int piecesNeededCount;
};

static void
incompleteMetadataFree (struct tr_incomplete_metadata * m)
{
  tr_free (m->metadata);
  tr_free (m->piecesNeeded);
  tr_free (m);
}

bool
tr_torrentSetMetadataSizeHint (tr_torrent * tor, int64_t size)
{
  if (tr_torrentHasMetadata (tor))
    return false;

  if (tor->incompleteMetadata != NULL)
    return false;

  const int n = (size <= 0 || size > INT_MAX) ? -1 :
                size / METADATA_PIECE_SIZE + (size % METADATA_PIECE_SIZE != 0 ? 1 : 0);

  dbgmsg (tor, "metadata is %" PRId64 " bytes in %d pieces", size, n);

  if (n <= 0)
    return false;

  struct tr_incomplete_metadata * m = tr_new (struct tr_incomplete_metadata, 1);
  if (m == NULL)
    return false;

  m->pieceCount = n;
  m->metadata = tr_new (uint8_t, size);
  m->metadata_size = size;
  m->piecesNeededCount = n;
  m->piecesNeeded = tr_new (struct metadata_node, n);

  if (m->metadata == NULL || m->piecesNeeded == NULL)
    {
      incompleteMetadataFree (m);
      return false;
    }

  for (int i = 0; i < n; ++i)
    {
      m->piecesNeeded[i].piece = i;
      m->piecesNeeded[i].requestedAt = 0;
    }

  tor->incompleteMetadata = m;
  return true;
}

static size_t
findInfoDictOffset (const tr_torrent * tor)
{
  size_t fileLen;
  uint8_t * fileContents;
  size_t offset = 0;

  /* load the file, and find the info dict's offset inside the file */
  if ((fileContents = tr_loadFile (tor->info.torrent, &fileLen, NULL)))
    {
      tr_variant top;

      if (!tr_variantFromBenc (&top, fileContents, fileLen))
        {
          tr_variant * infoDict;

          if (tr_variantDictFindDict (&top, TR_KEY_info, &infoDict))
            {
              size_t infoLen;
              char * infoContents = tr_variantToStr (infoDict, TR_VARIANT_FMT_BENC, &infoLen);
              const uint8_t * i = (const uint8_t*) tr_memmem ((char*)fileContents, fileLen, infoContents, infoLen);
              offset = i != NULL ? i - fileContents : 0;
              tr_free (infoContents);
            }

          tr_variantFree (&top);
        }

      tr_free (fileContents);
    }

  return offset;
}

static void
ensureInfoDictOffsetIsCached (tr_torrent * tor)
{
  assert (tr_torrentHasMetadata (tor));

  if (!tor->infoDictOffsetIsCached)
    {
      tor->infoDictOffset = findInfoDictOffset (tor);
      tor->infoDictOffsetIsCached = true;
    }
}

void*
tr_torrentGetMetadataPiece (tr_torrent * tor, int piece, size_t * len)
{
  char * ret = NULL;

  assert (tr_isTorrent (tor));
  assert (piece >= 0);
  assert (len != NULL);

  if (tr_torrentHasMetadata (tor))
    {
      tr_sys_file_t fd;

      ensureInfoDictOffsetIsCached (tor);

      assert (tor->infoDictLength > 0);

      fd = tr_sys_file_open (tor->info.torrent, TR_SYS_FILE_READ, 0, NULL);
      if (fd != TR_BAD_SYS_FILE)
        {
          const size_t o = piece * METADATA_PIECE_SIZE;

          if (tr_sys_file_seek (fd, tor->infoDictOffset + o, TR_SEEK_SET, NULL, NULL))
            {
              const size_t l = o + METADATA_PIECE_SIZE <= tor->infoDictLength
                             ? METADATA_PIECE_SIZE
                             : tor->infoDictLength - o;

              if (0<l && l<=METADATA_PIECE_SIZE)
                {
                  char * buf = tr_new (char, l);
                  uint64_t n;
                  if (tr_sys_file_read (fd, buf, l, &n, NULL) && n == l)
                    {
                      *len = l;
                      ret = buf;
                      buf = NULL;
                    }

                  tr_free (buf);
                }
            }

          tr_sys_file_close (fd, NULL);
        }
    }

  assert (ret == NULL || *len > 0);

  return ret;
}

void
tr_torrentSetMetadataPiece (tr_torrent  * tor, int piece, const void  * data, int len)
{
  int i;
  struct tr_incomplete_metadata * m;
  const int offset = piece * METADATA_PIECE_SIZE;

  assert (tr_isTorrent (tor));
  assert (data != NULL);
  assert (len >= 0);

  dbgmsg (tor, "got metadata piece %d of %d bytes", piece, len);

  /* are we set up to download metadata? */
  m = tor->incompleteMetadata;
  if (m == NULL)
    return;

  /* does this data pass the smell test? */
  if (piece < 0 || piece >= m->pieceCount)
    return;
  if (piece < m->pieceCount - 1 ? len != METADATA_PIECE_SIZE : len > METADATA_PIECE_SIZE)
    return;

  assert (offset <= m->metadata_size);

  if (len == 0 || len > m->metadata_size - offset)
    return;

  /* do we need this piece? */
  for (i=0; i<m->piecesNeededCount; ++i)
    if (m->piecesNeeded[i].piece == piece)
      break;
  if (i==m->piecesNeededCount)
    return;

  memcpy (m->metadata + offset, data, len);

  tr_removeElementFromArray (m->piecesNeeded, i,
                             sizeof (struct metadata_node),
                             m->piecesNeededCount--);

  dbgmsg (tor, "saving metainfo piece %d... %d remain", piece, m->piecesNeededCount);

  /* are we done? */
  if (m->piecesNeededCount == 0)
    {
      bool success = false;
      bool checksumPassed = false;
      bool metainfoParsed = false;
      uint8_t sha1[SHA_DIGEST_LENGTH];

      /* we've got a complete set of metainfo... see if it passes the checksum test */
      dbgmsg (tor, "metainfo piece %d was the last one", piece);
      tr_sha1 (sha1, m->metadata, m->metadata_size, NULL);
      if ((checksumPassed = !memcmp (sha1, tor->info.hash, SHA_DIGEST_LENGTH)))
        {
          /* checksum passed; now try to parse it as benc */
          tr_variant infoDict;
          const int err = tr_variantFromBenc (&infoDict, m->metadata, m->metadata_size);
          dbgmsg (tor, "err is %d", err);
          if ((metainfoParsed = !err))
            {
              /* yay we have bencoded metainfo... merge it into our .torrent file */
              tr_variant newMetainfo;
              char * path = tr_strdup (tor->info.torrent);

              if (tr_variantFromFile (&newMetainfo, TR_VARIANT_FMT_BENC, path, NULL))
                {
                  bool hasInfo;
                  tr_info info;
                  size_t infoDictLength;

                  /* remove any old .torrent and .resume files */
                  tr_sys_path_remove (path, NULL);
                  tr_torrentRemoveResume (tor);

                  dbgmsg (tor, "Saving completed metadata to \"%s\"", path);
                  tr_variantMergeDicts (tr_variantDictAddDict (&newMetainfo, TR_KEY_info, 0), &infoDict);

                  memset (&info, 0, sizeof (tr_info));
                  success = tr_metainfoParse (tor->session, &newMetainfo, &info, &hasInfo, &infoDictLength);

                  if (success && !tr_getBlockSize (info.pieceSize))
                    {
                      tr_torrentSetLocalError (tor, "%s", _("Magnet torrent's metadata is not usable"));
                      success = false;
                    }

                  if (success)
                    {
                      /* keep the new info */
                      tor->info = info;
                      tor->infoDictLength = infoDictLength;

                      /* save the new .torrent file */
                      tr_variantToFile (&newMetainfo, TR_VARIANT_FMT_BENC, tor->info.torrent);
                      tr_sessionSetTorrentFile (tor->session, tor->info.hashString, tor->info.torrent);
                      tr_torrentGotNewInfoDict (tor);
                      tr_torrentSetDirty (tor);
                    }

                  tr_variantFree (&newMetainfo);
                }

              tr_variantFree (&infoDict);
              tr_free (path);
            }
        }

      if (success)
        {
          incompleteMetadataFree (tor->incompleteMetadata);
          tor->incompleteMetadata = NULL;
          tor->isStopping = true;
          tor->magnetVerify = true;
          tor->startAfterVerify = true;
        }
        else /* drat. */
        {
          const int n = m->pieceCount;
          for (i=0; i<n; ++i)
            {
              m->piecesNeeded[i].piece = i;
              m->piecesNeeded[i].requestedAt = 0;
            }
          m->piecesNeededCount = n;
          dbgmsg (tor, "metadata error; trying again. %d pieces left", n);

          tr_logAddError ("magnet status: checksum passed %d, metainfo parsed %d",
                  (int)checksumPassed, (int)metainfoParsed);
        }
    }
}

bool
tr_torrentGetNextMetadataRequest (tr_torrent * tor, time_t now, int * setme_piece)
{
  bool have_request = false;
  struct tr_incomplete_metadata * m;

  assert (tr_isTorrent (tor));

  m = tor->incompleteMetadata;

  if ((m != NULL) && (m->piecesNeededCount > 0)
                  && (m->piecesNeeded[0].requestedAt + MIN_REPEAT_INTERVAL_SECS < now))
    {
      int i;
      const int piece = m->piecesNeeded[0].piece;

      tr_removeElementFromArray (m->piecesNeeded, 0,
                                 sizeof (struct metadata_node),
                                 m->piecesNeededCount--);

      i = m->piecesNeededCount++;
      m->piecesNeeded[i].piece = piece;
      m->piecesNeeded[i].requestedAt = now;

      dbgmsg (tor, "next piece to request: %d", piece);
      *setme_piece = piece;
      have_request = true;
    }

  return have_request;
}

double
tr_torrentGetMetadataPercent (const tr_torrent * tor)
{
  double ret;

  if (tr_torrentHasMetadata (tor))
    {
      ret = 1.0;
    }
  else
    {
      const struct tr_incomplete_metadata * m = tor->incompleteMetadata;

      if (!m || !m->pieceCount)
        ret = 0.0;
      else
        ret = (m->pieceCount - m->piecesNeededCount) / (double)m->pieceCount;
    }

  return ret;
}

/* FIXME: this should be renamed tr_metainfoGetMagnetLink() and moved to metainfo.c for consistency */
char *
tr_torrentInfoGetMagnetLink (const tr_info * inf)
{
  unsigned int i;
  const char * name;
  struct evbuffer * s = evbuffer_new ();

  evbuffer_add_printf (s, "magnet:?xt=urn:btih:%s", inf->hashString);

  name = inf->name;
  if (name && *name)
    {
      evbuffer_add_printf (s, "%s", "&dn=");
      tr_http_escape (s, name, TR_BAD_SIZE, true);
    }

  for (i=0; i<inf->trackerCount; ++i)
    {
      evbuffer_add_printf (s, "%s", "&tr=");
      tr_http_escape (s, inf->trackers[i].announce, TR_BAD_SIZE, true);
    }

  for (i=0; i<inf->webseedCount; i++)
    {
      evbuffer_add_printf (s, "%s", "&ws=");
      tr_http_escape (s, inf->webseeds[i], TR_BAD_SIZE, true);
    }

  return evbuffer_free_to_str (s, NULL);
}
