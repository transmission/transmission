/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id: utils-test.c 14266 2014-04-27 23:10:01Z jordan $
 */

#include "libtransmission-test.h"

#include "transmission.h"
#include "makemeta.h"

#include <stdlib.h> /* mktemp() */
#include <string.h> /* strlen() */

static int
test_single_file_impl (const tr_tracker_info * trackers,
                       const size_t            trackerCount,
                       const void            * payload,
                       const size_t            payloadSize,
                       const char            * comment,
                       bool                    isPrivate)
{
  char* sandbox;
  char* input_file;
  char* torrent_file;
  tr_metainfo_builder* builder;
  tr_ctor * ctor;
  tr_parse_result parse_result;
  tr_info inf;
  char * tmpstr;

  /* set up our local test sandbox */
  sandbox = libtest_sandbox_create();

  /* create a single input file */
  input_file = tr_buildPath (sandbox, "test.XXXXXX", NULL);
  libtest_create_tmpfile_with_contents (input_file, payload, payloadSize);
  builder = tr_metaInfoBuilderCreate (input_file);
  check_streq (input_file, builder->top);
  check_int_eq (1, builder->fileCount);
  check_streq (input_file, builder->files[0].filename);
  check_int_eq (payloadSize, builder->files[0].size);
  check_int_eq (payloadSize, builder->totalSize);
  check (builder->isSingleFile);
  check (!builder->abortFlag);

  /* have tr_makeMetaInfo() build the .torrent file */
  torrent_file = tr_strdup_printf ("%s.torrent", input_file);
  tr_makeMetaInfo (builder, torrent_file, trackers, trackerCount, comment, isPrivate);
  check (isPrivate == builder->isPrivate);
  check_streq (torrent_file, builder->outputFile);
  check_streq (comment, builder->comment);
  check_int_eq (trackerCount, builder->trackerCount);
  while (!builder->isDone)
    tr_wait_msec (100);

  /* now let's check our work: parse the  .torrent file */
  ctor = tr_ctorNew (NULL);
  tr_ctorSetMetainfoFromFile (ctor, torrent_file);
  parse_result = tr_torrentParse (ctor, &inf);
  check_int_eq (TR_PARSE_OK, parse_result);

  /* quick check of some of the parsed metainfo */
  check_int_eq (payloadSize, inf.totalSize);
  tmpstr = tr_basename(input_file);
  check_streq (tmpstr, inf.name);
  tr_free (tmpstr);
  check_streq (comment, inf.comment);
  check_int_eq (1, inf.fileCount);
  check_int_eq (isPrivate, inf.isPrivate);
  check (!inf.isMultifile);
  check_int_eq (trackerCount, inf.trackerCount);

  /* cleanup */
  tr_free (torrent_file);
  tr_free (input_file);
  tr_ctorFree (ctor);
  tr_metainfoFree (&inf);
  tr_metaInfoBuilderFree (builder);
  libtest_sandbox_destroy (sandbox);
  tr_free (sandbox);
  return 0;
}

static int
test_single_file (void)
{
  tr_tracker_info trackers[16];
  size_t trackerCount;
  bool isPrivate;
  const char * comment;
  const char * payload;
  size_t payloadSize;

  trackerCount = 0;
  trackers[trackerCount].tier = trackerCount;
  trackers[trackerCount].announce = (char*) "udp://tracker.openbittorrent.com:80";
  ++trackerCount;
  trackers[trackerCount].tier = trackerCount;
  trackers[trackerCount].announce = (char*) "udp://tracker.publicbt.com:80";
  ++trackerCount;
  payload = "Hello, World!\n";
  payloadSize = strlen(payload);
  comment = "This is the comment";
  isPrivate = false;
  test_single_file_impl (trackers, trackerCount, payload, payloadSize, comment, isPrivate);

  return 0;
}

int
main (void)
{
  const testFunc tests[] = { test_single_file };

  return runTests (tests, NUM_TESTS (tests));
}

