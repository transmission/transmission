/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <stdio.h> /* fprintf() */
#include <stdlib.h> /* strtoul(), EXIT_FAILURE */

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "units.h"

#define MY_NAME "transmission-create"

#define MAX_TRACKERS 128
static const uint32_t KiB = 1024;
static tr_tracker_info trackers[MAX_TRACKERS];
static int trackerCount = 0;
static bool isPrivate = false;
static bool showVersion = false;
static const char * comment = NULL;
static const char * outfile = NULL;
static const char * infile = NULL;
static uint32_t piecesize_kib = 0;

static tr_option options[] =
{
  { 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", 0, NULL },
  { 'o', "outfile", "Save the generated .torrent to this filename", "o", 1, "<file>" },
  { 's', "piecesize", "Set how many KiB each piece should be, overriding the preferred default", "s", 1, "<size in KiB>" },
  { 'c', "comment", "Add a comment", "c", 1, "<comment>" },
  { 't', "tracker", "Add a tracker's announce URL", "t", 1, "<url>" },
  { 'V', "version", "Show version number and exit", "V", 0, NULL },
  { 0, NULL, NULL, NULL, 0, NULL }
};

static const char *
getUsage (void)
{
  return "Usage: " MY_NAME " [options] <file|directory>";
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
          case 'V':
            showVersion = true;
            break;

          case 'p':
            isPrivate = true;
            break;

          case 'o':
            outfile = optarg;
            break;

          case 'c':
            comment = optarg;
            break;

          case 't':
            if (trackerCount + 1 < MAX_TRACKERS)
              {
                trackers[trackerCount].tier = trackerCount;
                trackers[trackerCount].announce = (char*) optarg;
                  ++trackerCount;
              }
            break;

          case 's':
            if (optarg)
              {
                char * endptr = NULL;
                piecesize_kib = strtoul (optarg, &endptr, 10);
                if (endptr && *endptr=='M')
                  piecesize_kib *= KiB;
              }
            break;

          case TR_OPT_UNK:
            infile = optarg;
            break;

          default:
            return 1;
        }
    }

  return 0;
}

static char*
tr_getcwd (void)
{
  char * result;
  tr_error * error = NULL;

  result = tr_sys_dir_get_current (&error);

  if (result == NULL)
    {
      fprintf (stderr, "getcwd error: \"%s\"", error->message);
      tr_error_free (error);
      result = tr_strdup ("");
    }

  return result;
}

int
tr_main (int    argc,
         char * argv[])
{
  char * out2 = NULL;
  tr_metainfo_builder * b = NULL;

  tr_logSetLevel (TR_LOG_ERROR);
  tr_formatter_mem_init (MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
  tr_formatter_size_init (DISK_K, DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
  tr_formatter_speed_init (SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);

  if (parseCommandLine (argc, (const char* const *)argv))
    return EXIT_FAILURE;

  if (showVersion)
    {
      fprintf (stderr, MY_NAME" "LONG_VERSION_STRING"\n");
      return EXIT_SUCCESS;
    }

  if (!infile)
    {
      fprintf (stderr, "ERROR: No input file or directory specified.\n");
      tr_getopt_usage (MY_NAME, getUsage (), options);
      fprintf (stderr, "\n");
      return EXIT_FAILURE;
    }

  if (outfile == NULL)
    {
      char * base = tr_sys_path_basename (infile, NULL);
      char * end = tr_strdup_printf ("%s.torrent", base);
      char * cwd = tr_getcwd ();
      outfile = out2 = tr_buildPath (cwd, end, NULL);
      tr_free (cwd);
      tr_free (end);
      tr_free (base);
    }

  if (!trackerCount)
    {
      if (isPrivate)
        {
          fprintf (stderr, "ERROR: no trackers specified for a private torrent\n");
          return EXIT_FAILURE;
        }
        else
        {
          printf ("WARNING: no trackers specified\n");
        }
    }

  printf ("Creating torrent \"%s\" ...", outfile);
  fflush (stdout);

  b = tr_metaInfoBuilderCreate (infile);

  if (piecesize_kib != 0)
    tr_metaInfoBuilderSetPieceSize (b, piecesize_kib * KiB);

  tr_makeMetaInfo (b, outfile, trackers, trackerCount, comment, isPrivate);
  while (!b->isDone)
    {
      tr_wait_msec (500);
      putc ('.', stdout);
      fflush (stdout);
    }

  putc (' ', stdout);
  switch (b->result)
    {
      case TR_MAKEMETA_OK:
        printf ("done!");
        break;

      case TR_MAKEMETA_URL:
        printf ("bad announce URL: \"%s\"", b->errfile);
        break;

      case TR_MAKEMETA_IO_READ:
        printf ("error reading \"%s\": %s", b->errfile, tr_strerror (b->my_errno));
        break;

      case TR_MAKEMETA_IO_WRITE:
        printf ("error writing \"%s\": %s", b->errfile, tr_strerror (b->my_errno));
        break;

      case TR_MAKEMETA_CANCELLED:
        printf ("cancelled");
        break;
    }
  putc ('\n', stdout);

  tr_metaInfoBuilderFree (b);
  tr_free (out2);
  return EXIT_SUCCESS;
}
