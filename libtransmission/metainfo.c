/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* strlen () */

#include <event2/buffer.h>

#include "transmission.h"
#include "crypto-utils.h" /* tr_sha1 */
#include "file.h"
#include "log.h"
#include "metainfo.h"
#include "platform.h" /* tr_getTorrentDir () */
#include "session.h"
#include "utils.h"
#include "variant.h"

/***
****
***/


#ifdef _WIN32
  #define PATH_DELIMITER_CHARS "/\\"
#else
  #define PATH_DELIMITER_CHARS "/"
#endif

static inline bool
char_is_path_separator (char c)
{
  return strchr(PATH_DELIMITER_CHARS, c) != NULL;
}

char*
tr_metainfoGetBasename (const tr_info * inf)
{
  size_t i;
  const char * name = inf->originalName;
  const size_t name_len = strlen (name);
  char * ret = tr_strdup_printf ("%s.%16.16s", name, inf->hashString);

  for (i=0; i<name_len; ++i)
    if (char_is_path_separator (ret[i]))
      ret[i] = '_';

  return ret;
}

static char*
getTorrentFilename (const tr_session * session, const tr_info * inf)
{
  char * base = tr_metainfoGetBasename (inf);
  char * filename = tr_strdup_printf ("%s" TR_PATH_DELIMITER_STR "%s.torrent",
                                      tr_getTorrentDir (session), base);
  tr_free (base);
  return filename;
}

/***
****
***/

static bool
path_component_is_suspicious (const char * component)
{
  return (component == NULL)
      || (strpbrk (component, PATH_DELIMITER_CHARS) != NULL)
      || (strcmp (component, ".") == 0)
      || (strcmp (component, "..") == 0);
}

static bool
getfile (char ** setme, const char * root, tr_variant * path, struct evbuffer * buf)
{
  bool success = false;
  size_t root_len = 0;

  *setme = NULL;

  /* root's already been checked by caller */
  assert (!path_component_is_suspicious (root));

  if (tr_variantIsList (path))
    {
      int i;
      const int n = tr_variantListSize (path);

      success = true;
      evbuffer_drain (buf, evbuffer_get_length (buf));
      root_len = strlen (root);
      evbuffer_add (buf, root, root_len);

      for (i=0; i<n; i++)
        {
          size_t len;
          const char * str;

          if (!tr_variantGetStr (tr_variantListChild (path, i), &str, &len) ||
              path_component_is_suspicious (str))
            {
              success = false;
              break;
            }

          if (!*str)
            continue;

          evbuffer_add (buf, TR_PATH_DELIMITER_STR, 1);
          evbuffer_add (buf, str, len);
        }
    }

  if (success && (evbuffer_get_length (buf) <= root_len))
    {
      success = false;
    }

  if (success)
    {
      *setme = tr_utf8clean ((char*)evbuffer_pullup (buf, -1), evbuffer_get_length (buf));
      /*fprintf (stderr, "[%s]\n", *setme);*/
    }

  return success;
}

static const char*
parseFiles (tr_info * inf, tr_variant * files, const tr_variant * length)
{
  int64_t len;

  inf->totalSize = 0;

  if (tr_variantIsList (files)) /* multi-file mode */
    {
      tr_file_index_t i;
      struct evbuffer * buf;
      const char * result;

      if (path_component_is_suspicious (inf->name))
        return "path";

      buf = evbuffer_new ();
      result = NULL;

      inf->isFolder = true;
      inf->fileCount = tr_variantListSize (files);
      inf->files = tr_new0 (tr_file, inf->fileCount);

      for (i=0; i<inf->fileCount; i++)
        {
          tr_variant * file;
          tr_variant * path;

          file = tr_variantListChild (files, i);
          if (!tr_variantIsDict (file))
            {
              result = "files";
              break;
            }

          if (!tr_variantDictFindList (file, TR_KEY_path_utf_8, &path))
            if (!tr_variantDictFindList (file, TR_KEY_path, &path))
              {
                result = "path";
                break;
              }

          if (!getfile (&inf->files[i].name, inf->name, path, buf))
            {
              result = "path";
              break;
            }

          if (!tr_variantDictFindInt (file, TR_KEY_length, &len))
            {
              result = "length";
              break;
            }

          inf->files[i].length = len;
          inf->totalSize      += len;
        }

      evbuffer_free (buf);
      return result;
    }
  else if (tr_variantGetInt (length, &len)) /* single-file mode */
    {
      if (path_component_is_suspicious (inf->name))
        return "path";

      inf->isFolder         = false;
      inf->fileCount        = 1;
      inf->files            = tr_new0 (tr_file, 1);
      inf->files[0].name    = tr_strdup (inf->name);
      inf->files[0].length  = len;
      inf->totalSize       += len;
    }
  else
    {
      return "length";
    }

  return NULL;
}

static char *
tr_convertAnnounceToScrape (const char * announce)
{
  char * scrape = NULL;
  const char * s;

  /* To derive the scrape URL use the following steps:
   * Begin with the announce URL. Find the last '/' in it.
   * If the text immediately following that '/' isn't 'announce'
   * it will be taken as a sign that that tracker doesn't support
   * the scrape convention. If it does, substitute 'scrape' for
   * 'announce' to find the scrape page. */
  if (((s = strrchr (announce, '/'))) && !strncmp (++s, "announce", 8))
    {
      const char * prefix = announce;
      const size_t prefix_len = s - announce;
      const char * suffix = s + 8;
      const size_t suffix_len = strlen (suffix);
      const size_t alloc_len = prefix_len + 6 + suffix_len + 1;
      char * walk = scrape = tr_new (char, alloc_len);
      memcpy (walk, prefix, prefix_len); walk += prefix_len;
      memcpy (walk, "scrape", 6);        walk += 6;
      memcpy (walk, suffix, suffix_len); walk += suffix_len;
      *walk++ = '\0';
      assert (walk - scrape == (int)alloc_len);
    }
  /* Some torrents with UDP annouce URLs don't have /announce. */
  else if (!strncmp (announce, "udp:", 4))
    {
      scrape = tr_strdup (announce);
    }

  return scrape;
}

static const char*
getannounce (tr_info * inf, tr_variant * meta)
{
  size_t len;
  const char * str;
  tr_tracker_info * trackers = NULL;
  int trackerCount = 0;
  tr_variant * tiers;

  /* Announce-list */
  if (tr_variantDictFindList (meta, TR_KEY_announce_list, &tiers))
    {
      int n;
      int i, j, validTiers;
      const int numTiers = tr_variantListSize (tiers);

      n = 0;
      for (i=0; i<numTiers; i++)
        n += tr_variantListSize (tr_variantListChild (tiers, i));

      trackers = tr_new0 (tr_tracker_info, n);

      for (i=0, validTiers=0; i<numTiers; i++)
        {
          tr_variant * tier = tr_variantListChild (tiers, i);
          const int tierSize = tr_variantListSize (tier);
          bool anyAdded = false;
          for (j=0; j<tierSize; j++)
            {
              if (tr_variantGetStr (tr_variantListChild (tier, j), &str, &len))
                {
                  char * url = tr_strstrip (tr_strndup (str, len));
                  if (!tr_urlIsValidTracker (url))
                    {
                      tr_free (url);
                    }
                  else
                    {
                      tr_tracker_info * t = trackers + trackerCount;
                      t->tier = validTiers;
                      t->announce = url;
                      t->scrape = tr_convertAnnounceToScrape (url);
                      t->id = trackerCount;

                      anyAdded = true;
                      ++trackerCount;
                    }
                }
            }

          if (anyAdded)
            ++validTiers;
        }

      /* did we use any of the tiers? */
      if (!trackerCount)
        {
          tr_free (trackers);
          trackers = NULL;
        }
    }

  /* Regular announce value */
  if (!trackerCount && tr_variantDictFindStr (meta, TR_KEY_announce, &str, &len))
    {
      char * url = tr_strstrip (tr_strndup (str, len));
      if (!tr_urlIsValidTracker (url))
        {
          tr_free (url);
        }
      else
        {
          trackers = tr_new0 (tr_tracker_info, 1);
          trackers[trackerCount].tier = 0;
          trackers[trackerCount].announce = url;
          trackers[trackerCount].scrape = tr_convertAnnounceToScrape (url);
          trackers[trackerCount].id = 0;
          trackerCount++;
          /*fprintf (stderr, "single announce: [%s]\n", url);*/
        }
    }

  inf->trackers = trackers;
  inf->trackerCount = trackerCount;

  return NULL;
}

/**
 * @brief Ensure that the URLs for multfile torrents end in a slash.
 *
 * See http://bittorrent.org/beps/bep_0019.html#metadata-extension
 * for background on how the trailing slash is used for "url-list"
 * fields.
 *
 * This function is to workaround some .torrent generators, such as
 * mktorrent and very old versions of utorrent, that don't add the
 * trailing slash for multifile torrents if omitted by the end user.
 */
static char*
fix_webseed_url (const tr_info * inf, const char * url_in)
{
  size_t len;
  char * url;
  char * ret = NULL;

  url = tr_strdup (url_in);
  tr_strstrip (url);
  len = strlen (url);

  if (tr_urlIsValid (url, len))
    {
      if ((inf->fileCount > 1) && (len > 0) && (url[len-1] != '/'))
        ret = tr_strdup_printf ("%*.*s/", (int)len, (int)len, url);
      else
        ret = tr_strndup (url, len);
    }

  tr_free (url);
  return ret;
}

static void
geturllist (tr_info * inf, tr_variant * meta)
{
  tr_variant * urls;
  const char * url;

  if (tr_variantDictFindList (meta, TR_KEY_url_list, &urls))
    {
      int i;
      const int n = tr_variantListSize (urls);

      inf->webseedCount = 0;
      inf->webseeds = tr_new0 (char*, n);

      for (i=0; i<n; i++)
        {
          if (tr_variantGetStr (tr_variantListChild (urls, i), &url, NULL))
            {
              char * fixed_url = fix_webseed_url (inf, url);

              if (fixed_url != NULL)
                inf->webseeds[inf->webseedCount++] = fixed_url;
            }
        }
    }
  else if (tr_variantDictFindStr (meta, TR_KEY_url_list, &url, NULL)) /* handle single items in webseeds */
    {
      char * fixed_url = fix_webseed_url (inf, url);

      if (fixed_url != NULL)
        {
          inf->webseedCount = 1;
          inf->webseeds = tr_new0 (char*, 1);
          inf->webseeds[0] = fixed_url;
        }
    }
}

static const char*
tr_metainfoParseImpl (const tr_session  * session,
                      tr_info           * inf,
                      bool              * hasInfoDict,
                      size_t            * infoDictLength,
                      const tr_variant  * meta_in)
{
  int64_t i;
  size_t len;
  const char * str;
  const uint8_t * raw;
  tr_variant * d;
  tr_variant * infoDict = NULL;
  tr_variant * meta = (tr_variant *) meta_in;
  bool b;
  bool isMagnet = false;

  /* info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
   * from the Metainfo file. Note that the value will be a bencoded
   * dictionary, given the definition of the info key above. */
  b = tr_variantDictFindDict (meta, TR_KEY_info, &infoDict);
  if (hasInfoDict != NULL)
    *hasInfoDict = b;

  if (!b)
    {
      /* no info dictionary... is this a magnet link? */
      if (tr_variantDictFindDict (meta, TR_KEY_magnet_info, &d))
        {
          isMagnet = true;

          /* get the info-hash */
          if (!tr_variantDictFindRaw (d, TR_KEY_info_hash, &raw, &len))
            return "info_hash";
          if (len != SHA_DIGEST_LENGTH)
            return "info_hash";
          memcpy (inf->hash, raw, len);
          tr_sha1_to_hex (inf->hashString, inf->hash);

          /* maybe get the display name */
          if (tr_variantDictFindStr (d, TR_KEY_display_name, &str, &len))
            {
              tr_free (inf->name);
              tr_free (inf->originalName);
              inf->name = tr_strndup (str, len);
              inf->originalName = tr_strndup (str, len);
            }

          if (!inf->name)
              inf->name = tr_strdup (inf->hashString);
          if (!inf->originalName)
              inf->originalName = tr_strdup (inf->hashString);
        }
      else /* not a magnet link and has no info dict... */
        {
          return "info";
        }
    }
  else
    {
      size_t len;
      char * bstr = tr_variantToStr (infoDict, TR_VARIANT_FMT_BENC, &len);
      tr_sha1 (inf->hash, bstr, len, NULL);
      tr_sha1_to_hex (inf->hashString, inf->hash);

      if (infoDictLength != NULL)
        *infoDictLength = len;

      tr_free (bstr);
    }

  /* name */
  if (!isMagnet)
    {
      len = 0;
      if (!tr_variantDictFindStr (infoDict, TR_KEY_name_utf_8, &str, &len))
        if (!tr_variantDictFindStr (infoDict, TR_KEY_name, &str, &len))
          str = "";
      if (!str || !*str)
        return "name";
      tr_free (inf->name);
      tr_free (inf->originalName);
      inf->name = tr_utf8clean (str, len);
      inf->originalName = tr_strdup (inf->name);
    }

  /* comment */
  len = 0;
  if (!tr_variantDictFindStr (meta, TR_KEY_comment_utf_8, &str, &len))
    if (!tr_variantDictFindStr (meta, TR_KEY_comment, &str, &len))
      str = "";
  tr_free (inf->comment);
  inf->comment = tr_utf8clean (str, len);

  /* created by */
  len = 0;
  if (!tr_variantDictFindStr (meta, TR_KEY_created_by_utf_8, &str, &len))
    if (!tr_variantDictFindStr (meta, TR_KEY_created_by, &str, &len))
      str = "";
  tr_free (inf->creator);
  inf->creator = tr_utf8clean (str, len);

  /* creation date */
  if (!tr_variantDictFindInt (meta, TR_KEY_creation_date, &i))
    i = 0;
  inf->dateCreated = i;

  /* private */
  if (!tr_variantDictFindInt (infoDict, TR_KEY_private, &i))
    if (!tr_variantDictFindInt (meta, TR_KEY_private, &i))
      i = 0;
  inf->isPrivate = i != 0;

  /* piece length */
  if (!isMagnet)
    {
      if (!tr_variantDictFindInt (infoDict, TR_KEY_piece_length, &i) || (i < 1))
        return "piece length";
      inf->pieceSize = i;
    }

  /* pieces */
  if (!isMagnet)
    {
      if (!tr_variantDictFindRaw (infoDict, TR_KEY_pieces, &raw, &len))
        return "pieces";
      if (len % SHA_DIGEST_LENGTH)
        return "pieces";

      inf->pieceCount = len / SHA_DIGEST_LENGTH;
      inf->pieces = tr_new0 (tr_piece, inf->pieceCount);
      for (i=0; i<inf->pieceCount; i++)
        memcpy (inf->pieces[i].hash, &raw[i * SHA_DIGEST_LENGTH], SHA_DIGEST_LENGTH);
    }

  /* files */
  if (!isMagnet)
    {
      if ((str = parseFiles (inf, tr_variantDictFind (infoDict, TR_KEY_files),
                                  tr_variantDictFind (infoDict, TR_KEY_length))))
        return str;

      if (!inf->fileCount || !inf->totalSize)
        return "files";

      if ((uint64_t) inf->pieceCount != (inf->totalSize + inf->pieceSize - 1) / inf->pieceSize)
        return "files";
    }

  /* get announce or announce-list */
  if ((str = getannounce (inf, meta)))
    return str;

  /* get the url-list */
  geturllist (inf, meta);

  /* filename of Transmission's copy */
  tr_free (inf->torrent);
  inf->torrent = session ?  getTorrentFilename (session, inf) : NULL;

  return NULL;
}

bool
tr_metainfoParse (const tr_session * session,
                  const tr_variant * meta_in,
                  tr_info          * inf,
                  bool             * hasInfoDict,
                  size_t           * infoDictLength)
{
  const char * badTag = tr_metainfoParseImpl (session,
                                              inf,
                                              hasInfoDict,
                                              infoDictLength,
                                              meta_in);
  const bool success = badTag == NULL;

  if (badTag)
    {
      tr_logAddNamedError (inf->name, _("Invalid metadata entry \"%s\""), badTag);
      tr_metainfoFree (inf);
    }

  return success;
}

void
tr_metainfoFree (tr_info * inf)
{
  unsigned int i;
  tr_file_index_t ff;

  for (i=0; i<inf->webseedCount; i++)
    tr_free (inf->webseeds[i]);

  for (ff=0; ff<inf->fileCount; ff++)
      tr_free (inf->files[ff].name);

  tr_free (inf->webseeds);
  tr_free (inf->pieces);
  tr_free (inf->files);
  tr_free (inf->comment);
  tr_free (inf->creator);
  tr_free (inf->torrent);
  tr_free (inf->originalName);
  tr_free (inf->name);

  for (i=0; i<inf->trackerCount; i++)
    {
      tr_free (inf->trackers[i].announce);
      tr_free (inf->trackers[i].scrape);
    }
  tr_free (inf->trackers);

  memset (inf, '\0', sizeof (tr_info));
}

void
tr_metainfoRemoveSaved (const tr_session * session, const tr_info * inf)
{
  char * filename;

  filename = getTorrentFilename (session, inf);
  tr_sys_path_remove (filename, NULL);
  tr_free (filename);
}

