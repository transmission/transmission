/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "transmission.h"
#include "magnet.h"
#include "utils.h"

#include "libtransmission-test.h"

static int
test1 (void)
{
    int i;
    const char * uri;
    tr_magnet_info * info;
    const int dec[] = { 210, 53, 64, 16, 163, 202, 74, 222, 91, 116,
                        39, 187, 9, 58, 98, 163, 137, 159, 243, 129 };

    uri = "magnet:?xt=urn:btih:"
          "d2354010a3ca4ade5b7427bb093a62a3899ff381"
          "&dn=Display%20Name"
          "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
          "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
          "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile";
    info = tr_magnetParse (uri);
    check (info != NULL);
    check_int_eq (2, info->trackerCount);
    check_streq (info->trackers[0], "http://tracker.openbittorrent.com/announce");
    check_streq (info->trackers[1], "http://tracker.opentracker.org/announce");
    check_int_eq (1, info->webseedCount);
    check_streq ("http://server.webseed.org/path/to/file", info->webseeds[0]);
    check_streq ("Display Name", info->displayName);
    for (i=0; i<20; ++i)
        check (info->hash[i] == dec[i]);
    tr_magnetFree (info);
    info = NULL;

    /* same thing but in base32 encoding */
    uri = "magnet:?xt=urn:btih:"
          "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"
          "&dn=Display%20Name"
          "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
          "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"
          "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce";
    info = tr_magnetParse (uri);
    check (info != NULL);
    check_int_eq (2, info->trackerCount);
    check_streq ("http://tracker.openbittorrent.com/announce", info->trackers[0]);
    check_streq ("http://tracker.opentracker.org/announce", info->trackers[1]);
    check_int_eq (1, info->webseedCount);
    check_streq ("http://server.webseed.org/path/to/file", info->webseeds[0]);
    check_streq ("Display Name", info->displayName);
    for (i=0; i<20; ++i)
        check (info->hash[i] == dec[i]);
    tr_magnetFree (info);
    info = NULL;

    return 0;
}

MAIN_SINGLE_TEST (test1)

