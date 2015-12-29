/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <stdio.h> /* fprintf () */
#include <string.h> /* strlen (), strstr (), strcmp () */
#include <stdlib.h> /* EXIT_FAILURE */

#include <event2/buffer.h>

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>

#define MY_NAME "transmission-edit"

static int fileCount = 0;
static bool showVersion = false;
static const char ** files = NULL;
static const char * add = NULL;
static const char * deleteme = NULL;
static const char * replace[2] = { NULL, NULL };

static tr_option options[] =
{
  { 'a', "add", "Add a tracker's announce URL", "a", 1, "<url>" },
  { 'd', "delete", "Delete a tracker's announce URL", "d", 1, "<url>" },
  { 'r', "replace", "Search and replace a substring in the announce URLs", "r", 1, "<old> <new>" },
  { 'V', "version", "Show version number and exit", "V", 0, NULL },
  { 0, NULL, NULL, NULL, 0, NULL }
};

static const char *
getUsage (void)
{
  return "Usage: " MY_NAME " [options] torrent-file(s)";
}

static int
parseCommandLine (int argc, const char * const * argv)
{
  int c;
  const char * optarg;

  while ((c = tr_getopt (getUsage (), argc, argv, options, &optarg)))
    {
      switch (c)
        {
          case 'a':
            add = optarg;
            break;

          case 'd':
            deleteme = optarg;
            break;

          case 'r':
            replace[0] = optarg;
            c = tr_getopt (getUsage (), argc, argv, options, &optarg);
            if (c != TR_OPT_UNK)
              return 1;
            replace[1] = optarg;
            break;

          case 'V':
            showVersion = true;
            break;

          case TR_OPT_UNK:
            files[fileCount++] = optarg;
            break;

          default:
            return 1;
        }
    }

  return 0;
}

static bool
removeURL (tr_variant * metainfo, const char * url)
{
  const char * str;
  tr_variant * announce_list;
  bool changed = false;

  if (tr_variantDictFindStr (metainfo, TR_KEY_announce, &str, NULL) && !strcmp (str, url))
    {
      printf ("\tRemoved \"%s\" from \"announce\"\n", str);
      tr_variantDictRemove (metainfo, TR_KEY_announce);
      changed = true;
    }

  if (tr_variantDictFindList (metainfo, TR_KEY_announce_list, &announce_list))
    {
      tr_variant * tier;
      int tierIndex = 0;
      while ((tier = tr_variantListChild (announce_list, tierIndex)))
        {
          tr_variant * node;
          int nodeIndex = 0;
          while ((node = tr_variantListChild (tier, nodeIndex)))
            {
              if (tr_variantGetStr (node, &str, NULL) && !strcmp (str, url))
                {
                  printf ("\tRemoved \"%s\" from \"announce-list\" tier #%d\n", str, (tierIndex+1));
                  tr_variantListRemove (tier, nodeIndex);
                  changed = true;
                }
              else ++nodeIndex;
            }

          if (tr_variantListSize (tier) == 0)
            {
              printf ("\tNo URLs left in tier #%d... removing tier\n", (tierIndex+1));
              tr_variantListRemove (announce_list, tierIndex);
            }
          else
            {
              ++tierIndex;
            }
        }

      if (tr_variantListSize (announce_list) == 0)
        {
          printf ("\tNo tiers left... removing announce-list\n");
          tr_variantDictRemove (metainfo, TR_KEY_announce_list);
        }
    }

  /* if we removed the "announce" field and there's still another track left,
   * use it as the "announce" field */
  if (changed && !tr_variantDictFindStr (metainfo, TR_KEY_announce, &str, NULL))
    {
      tr_variant * tier;
      tr_variant * node;

      if ((tier = tr_variantListChild (announce_list, 0)))
        {
          if ((node = tr_variantListChild (tier, 0)))
            {
              if (tr_variantGetStr (node, &str, NULL))
                {
                  tr_variantDictAddStr (metainfo, TR_KEY_announce, str);
                  printf ("\tAdded \"%s\" to announce\n", str);
                }
            }
        }
    }

  return changed;
}

static char*
replaceSubstr (const char * str, const char * in, const char * out)
{
  char * walk;
  struct evbuffer * buf = evbuffer_new ();
  const size_t inlen = strlen (in);
  const size_t outlen = strlen (out);

  while ((walk = strstr (str, in)))
    {
      evbuffer_add (buf, str, walk-str);
      evbuffer_add (buf, out, outlen);
      str = walk + inlen;
    }

  evbuffer_add (buf, str, strlen (str));

  return evbuffer_free_to_str (buf, NULL);
}

static bool
replaceURL (tr_variant * metainfo, const char * in, const char * out)
{
  const char * str;
  tr_variant * announce_list;
  bool changed = false;

  if (tr_variantDictFindStr (metainfo, TR_KEY_announce, &str, NULL) && strstr (str, in))
    {
      char * newstr = replaceSubstr (str, in, out);
      printf ("\tReplaced in \"announce\": \"%s\" --> \"%s\"\n", str, newstr);
      tr_variantDictAddStr (metainfo, TR_KEY_announce, newstr);
      tr_free (newstr);
      changed = true;
    }

  if (tr_variantDictFindList (metainfo, TR_KEY_announce_list, &announce_list))
    {
      tr_variant * tier;
      int tierCount = 0;
      while ((tier = tr_variantListChild (announce_list, tierCount++)))
        {
          tr_variant * node;
          int nodeCount = 0;
          while ((node = tr_variantListChild (tier, nodeCount++)))
            {
              if (tr_variantGetStr (node, &str, NULL) && strstr (str, in))
                {
                  char * newstr = replaceSubstr (str, in, out);
                  printf ("\tReplaced in \"announce-list\" tier %d: \"%s\" --> \"%s\"\n", tierCount, str, newstr);
                  tr_variantFree (node);
                  tr_variantInitStr (node, newstr, TR_BAD_SIZE);
                  tr_free (newstr);
                  changed = true;
                }
            }
        }
    }

  return changed;
}

static bool
announce_list_has_url (tr_variant * announce_list, const char * url)
{
  tr_variant * tier;
  int tierCount = 0;
  while ((tier = tr_variantListChild (announce_list, tierCount++)))
    {
      tr_variant * node;
      const char * str;
      int nodeCount = 0;
      while ((node = tr_variantListChild (tier, nodeCount++)))
        if (tr_variantGetStr (node, &str, NULL) && !strcmp (str, url))
          return true;
    }

  return false;
}

static bool
addURL (tr_variant * metainfo, const char * url)
{
  const char * announce = NULL;
  tr_variant * announce_list = NULL;
  bool changed = false;
  const bool had_announce = tr_variantDictFindStr (metainfo, TR_KEY_announce, &announce, NULL);
  const bool had_announce_list = tr_variantDictFindList (metainfo, TR_KEY_announce_list, &announce_list);

  if (!had_announce && !had_announce_list)
    {
      /* this new tracker is the only one, so add it to "announce"... */
      printf ("\tAdded \"%s\" in \"announce\"\n", url);
      tr_variantDictAddStr (metainfo, TR_KEY_announce, url);
      changed = true;
    }
  else
    {
      if (!had_announce_list)
        {
          announce_list = tr_variantDictAddList (metainfo, TR_KEY_announce_list, 2);

          if (had_announce)
            {
              /* we're moving from an 'announce' to an 'announce-list',
               * so copy the old announce URL to the list */
              tr_variant * tier = tr_variantListAddList (announce_list, 1);
              tr_variantListAddStr (tier, announce);
              changed = true;
            }
        }

      /* If the user-specified URL isn't in the announce list yet, add it */
      if (!announce_list_has_url (announce_list, url))
        {
          tr_variant * tier = tr_variantListAddList (announce_list, 1);
          tr_variantListAddStr (tier, url);
          printf ("\tAdded \"%s\" to \"announce-list\" tier %zu\n", url, tr_variantListSize (announce_list));
          changed = true;
        }
    }

  return changed;
}

int
tr_main (int    argc,
         char * argv[])
{
  int i;
  int changedCount = 0;

  files = tr_new0 (const char*, argc);

  tr_logSetLevel (TR_LOG_ERROR);

  if (parseCommandLine (argc, (const char* const *)argv))
    return EXIT_FAILURE;

  if (showVersion)
    {
      fprintf (stderr, MY_NAME" "LONG_VERSION_STRING"\n");
      return EXIT_SUCCESS;
    }

  if (fileCount < 1)
    {
      fprintf (stderr, "ERROR: No torrent files specified.\n");
      tr_getopt_usage (MY_NAME, getUsage (), options);
      fprintf (stderr, "\n");
      return EXIT_FAILURE;
    }

  if (!add && !deleteme && !replace[0])
    {
      fprintf (stderr, "ERROR: Must specify -a, -d or -r\n");
      tr_getopt_usage (MY_NAME, getUsage (), options);
      fprintf (stderr, "\n");
      return EXIT_FAILURE;
    }

  for (i=0; i<fileCount; ++i)
    {
      tr_variant top;
      bool changed = false;
      const char * filename = files[i];
      tr_error * error = NULL;

      printf ("%s\n", filename);

      if (!tr_variantFromFile (&top, TR_VARIANT_FMT_BENC, filename, &error))
        {
          printf ("\tError reading file: %s\n", error->message);
          tr_error_free (error);
          continue;
        }

      if (deleteme != NULL)
        changed |= removeURL (&top, deleteme);

      if (add != NULL)
        changed = addURL (&top, add);

      if (replace[0] && replace[1])
        changed |= replaceURL (&top, replace[0], replace[1]);

      if (changed)
        {
          ++changedCount;
          tr_variantToFile (&top, TR_VARIANT_FMT_BENC, filename);
        }

      tr_variantFree (&top);
    }

  printf ("Changed %d files\n", changedCount);

  tr_free (files);
  return EXIT_SUCCESS;
}
