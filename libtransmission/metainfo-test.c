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

#include <errno.h>

static int
test_magnet_link (void)
{
  tr_info inf;
  tr_ctor * ctor;
  const char * magnet_link;
  tr_parse_result parse_result;

  /* background info @ http://wiki.theory.org/BitTorrent_Magnet-URI_Webseeding */
  magnet_link = "magnet:?"
                "xt=urn:btih:14FFE5DD23188FD5CB53A1D47F1289DB70ABF31E"
                "&dn=ubuntu+12+04+1+desktop+32+bit"
                "&tr=http%3A%2F%2Ftracker.publicbt.com%2Fannounce"
                "&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
                "&ws=http://transmissionbt.com ";
  ctor = tr_ctorNew (NULL);
  tr_ctorSetMetainfoFromMagnetLink (ctor, magnet_link);
  parse_result = tr_torrentParse (ctor, &inf);
  check_int_eq (inf.fileCount, 0); /* cos it's a magnet link */
  check_int_eq (parse_result, TR_PARSE_OK);
  check_int_eq (inf.trackerCount, 2);
  check_streq ("http://tracker.publicbt.com/announce", inf.trackers[0].announce);
  check_streq ("udp://tracker.publicbt.com:80", inf.trackers[1].announce);
  check_int_eq (inf.webseedCount, 1);
  check_streq ("http://transmissionbt.com", inf.webseeds[0]);

  /* cleanup */
  tr_metainfoFree (&inf);
  tr_ctorFree (ctor);
  return 0;
}

#define BEFORE_PATH "d10:created by25:Transmission/2.82 (14160)13:creation datei1402280218e8:encoding5:UTF-84:infod5:filesld6:lengthi2e4:pathl"
#define AFTER_PATH "eed6:lengthi2e4:pathl5:b.txteee4:name3:foo12:piece lengthi32768e6:pieces20:ÞÉ`âMs¡Å;Ëº¬.åÂà7:privatei0eee"

static int
test_metainfo (void)
{
  size_t i;
  const struct {
    int expected_benc_err;
    int expected_parse_result;
    const void * benc;
  } metainfo[] = {
    { 0,      TR_PARSE_OK,  BEFORE_PATH "5:a.txt"     AFTER_PATH },

    /* allow empty components, but not =all= empty components, see bug #5517 */
    { 0,      TR_PARSE_OK,  BEFORE_PATH "0:5:a.txt"   AFTER_PATH },
    { 0,      TR_PARSE_ERR, BEFORE_PATH "0:0:"        AFTER_PATH },

    /* don't allow path components in a filename */
    { 0,      TR_PARSE_ERR, BEFORE_PATH "7:a/a.txt"   AFTER_PATH },

    /* fail on "." components */
    { 0,      TR_PARSE_ERR, BEFORE_PATH "1:.5:a.txt"  AFTER_PATH },
    { 0,      TR_PARSE_ERR, BEFORE_PATH "5:a.txt1:."  AFTER_PATH },

    /* fail on ".." components */
    { 0,      TR_PARSE_ERR, BEFORE_PATH "2:..5:a.txt" AFTER_PATH },
    { 0,      TR_PARSE_ERR, BEFORE_PATH "5:a.txt2:.." AFTER_PATH },

    /* fail on empty string */
    { EILSEQ, TR_PARSE_ERR, "" }
  };

  tr_logSetLevel(0); /* yes, we already know these will generate errors, thank you... */


  for (i=0; i<(sizeof(metainfo) / sizeof(metainfo[0])); i++)
    {
      tr_ctor * ctor = tr_ctorNew (NULL);
      const int err = tr_ctorSetMetainfo (ctor, metainfo[i].benc, strlen(metainfo[i].benc));
      check_int_eq (metainfo[i].expected_benc_err, err);
      if (!err)
        {
          const tr_parse_result parse_result = tr_torrentParse (ctor, NULL);
          check_int_eq (metainfo[i].expected_parse_result, parse_result);
        }
      tr_ctorFree (ctor);
    }

  return 0;
}

int
main (void)
{
  const testFunc tests[] = { test_magnet_link,
                             test_metainfo };

  return runTests (tests, NUM_TESTS (tests));
}

