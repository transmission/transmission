/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "libtransmission-test.h"

#include "transmission.h"
#include "crypto-utils.h"
#include "file.h"
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
  check (!builder->isFolder);
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
  libttest_sync ();
  tr_ctorSetMetainfoFromFile (ctor, torrent_file);
  parse_result = tr_torrentParse (ctor, &inf);
  check_int_eq (TR_PARSE_OK, parse_result);

  /* quick check of some of the parsed metainfo */
  check_int_eq (payloadSize, inf.totalSize);
  tmpstr = tr_sys_path_basename (input_file, NULL);
  check_streq (tmpstr, inf.name);
  tr_free (tmpstr);
  check_streq (comment, inf.comment);
  check_int_eq (1, inf.fileCount);
  check_int_eq (isPrivate, inf.isPrivate);
  check (!inf.isFolder);
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


static int
test_single_directory_impl (const tr_tracker_info * trackers,
                            const size_t            trackerCount,
                            const void           ** payloads,
                            const size_t          * payloadSizes,
                            const size_t            payloadCount,
                            const char            * comment,
                            const bool              isPrivate)
{
  char* sandbox;
  char* torrent_file;
  tr_metainfo_builder* builder;
  tr_ctor * ctor;
  tr_parse_result parse_result;
  tr_info inf;
  char * top;
  char ** files;
  size_t totalSize;
  size_t i;
  char* tmpstr;


  /* set up our local test sandbox */
  sandbox = libtest_sandbox_create();

  /* create the top temp directory */
  top = tr_buildPath (sandbox, "folder.XXXXXX", NULL);
  tr_sys_dir_create_temp (top, NULL);

  /* build the payload files that go into the top temp directory */
  files = tr_new (char*, payloadCount);
  totalSize = 0;
  for (i=0; i<payloadCount; i++)
    {
      char tmpl[16];
      tr_snprintf (tmpl, sizeof(tmpl), "file.%04zu%s", i, "XXXXXX");
      files[i] = tr_buildPath (top, tmpl, NULL);
      libtest_create_tmpfile_with_contents (files[i], payloads[i], payloadSizes[i]);
      totalSize += payloadSizes[i];
    }
  libttest_sync ();

  /* init the builder */
  builder = tr_metaInfoBuilderCreate (top);
  check (!builder->abortFlag);
  check_streq (top, builder->top);
  check_int_eq (payloadCount, builder->fileCount);
  check_int_eq (totalSize, builder->totalSize);
  check (builder->isFolder);
  for (i=0; i<builder->fileCount; i++)
    {
      check_streq (files[i], builder->files[i].filename);
      check_int_eq (payloadSizes[i], builder->files[i].size);
    }

  /* call tr_makeMetaInfo() to build the .torrent file */
  torrent_file = tr_strdup_printf ("%s.torrent", top);
  tr_makeMetaInfo (builder, torrent_file, trackers, trackerCount, comment, isPrivate);
  check (isPrivate == builder->isPrivate);
  check_streq (torrent_file, builder->outputFile);
  check_streq (comment, builder->comment);
  check_int_eq (trackerCount, builder->trackerCount);
  while (!builder->isDone)
    tr_wait_msec (100);

  /* now let's check our work: parse the  .torrent file */
  ctor = tr_ctorNew (NULL);
  libttest_sync ();
  tr_ctorSetMetainfoFromFile (ctor, torrent_file);
  parse_result = tr_torrentParse (ctor, &inf);
  check_int_eq (TR_PARSE_OK, parse_result);

  /* quick check of some of the parsed metainfo */
  check_int_eq (totalSize, inf.totalSize);
  tmpstr = tr_sys_path_basename (top, NULL);
  check_streq (tmpstr, inf.name);
  tr_free (tmpstr);
  check_streq (comment, inf.comment);
  check_int_eq (payloadCount, inf.fileCount);
  check_int_eq (isPrivate, inf.isPrivate);
  check_int_eq (builder->isFolder, inf.isFolder);
  check_int_eq (trackerCount, inf.trackerCount);

  /* cleanup */
  tr_free (torrent_file);
  tr_ctorFree (ctor);
  tr_metainfoFree (&inf);
  tr_metaInfoBuilderFree (builder);
  for (i=0; i<payloadCount; i++)
    tr_free (files[i]);
  tr_free (files);
  libtest_sandbox_destroy (sandbox);
  tr_free (sandbox);
  tr_free (top);

  return 0;
}

static int
test_single_directory_random_payload_impl (const tr_tracker_info * trackers,
                                           const size_t            trackerCount,
                                           const size_t            maxFileCount,
                                           const size_t            maxFileSize,
                                           const char            * comment,
                                           const bool              isPrivate)
{
  size_t i;
  void ** payloads;
  size_t * payloadSizes;
  size_t payloadCount;

  /* build random payloads */
  payloadCount = 1 + tr_rand_int_weak (maxFileCount);
  payloads = tr_new0 (void*, payloadCount);
  payloadSizes = tr_new0 (size_t, payloadCount);
  for (i=0; i<payloadCount; i++)
    {
      const size_t n = 1 + tr_rand_int_weak (maxFileSize);
      payloads[i] = tr_new (char, n);
      tr_rand_buffer (payloads[i], n);
      payloadSizes[i] = n;
    }

  /* run the test */
  test_single_directory_impl (trackers,
                              trackerCount,
                              (const void**) payloads,
                              payloadSizes,
                              payloadCount,
                              comment,
                              isPrivate);

  /* cleanup */
  for (i=0; i<payloadCount; i++)
    tr_free (payloads[i]);
  tr_free (payloads);
  tr_free (payloadSizes);

  return 0;
}

#define DEFAULT_MAX_FILE_COUNT 16
#define DEFAULT_MAX_FILE_SIZE 1024

static int
test_single_directory_random_payload (void)
{
  tr_tracker_info trackers[16];
  size_t trackerCount;
  bool isPrivate;
  const char * comment;
  size_t i;

  trackerCount = 0;
  trackers[trackerCount].tier = trackerCount;
  trackers[trackerCount].announce = (char*) "udp://tracker.openbittorrent.com:80";
  ++trackerCount;
  trackers[trackerCount].tier = trackerCount;
  trackers[trackerCount].announce = (char*) "udp://tracker.publicbt.com:80";
  ++trackerCount;
  comment = "This is the comment";
  isPrivate = false;

  for (i=0; i<10; i++)
    {
      test_single_directory_random_payload_impl (trackers,
                                                 trackerCount,
                                                 DEFAULT_MAX_FILE_COUNT,
                                                 DEFAULT_MAX_FILE_SIZE,
                                                 comment,
                                                 isPrivate);
    }

  return 0;
}

int
main (void)
{
  const testFunc tests[] = { test_single_file,
                             test_single_directory_random_payload };

  return runTests (tests, NUM_TESTS (tests));
}
