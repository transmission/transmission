#include "transmission.h"

#include "libtransmission-test.h"

static int
test1 (void)
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

MAIN_SINGLE_TEST (test1)
