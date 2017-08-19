/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "magnet.h"
#include "utils.h"

#include "libtransmission-test.h"

static int test1(void)
{
    char const* uri;
    tr_magnet_info* info;
    uint8_t const dec[] =
    {
        210, 53, 64, 16, 163, 202, 74, 222, 91, 116,
        39, 187, 9, 58, 98, 163, 137, 159, 243, 129
    };

    uri =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile";
    info = tr_magnetParse(uri);
    check_ptr(info, !=, NULL);
    check_int(info->trackerCount, ==, 2);
    check_str(info->trackers[0], ==, "http://tracker.openbittorrent.com/announce");
    check_str(info->trackers[1], ==, "http://tracker.opentracker.org/announce");
    check_int(info->webseedCount, ==, 1);
    check_str(info->webseeds[0], ==, "http://server.webseed.org/path/to/file");
    check_str(info->displayName, ==, "Display Name");
    check_mem(info->hash, ==, dec, 20);

    tr_magnetFree(info);
    info = NULL;

    /* same thing but in base32 encoding */
    uri =
        "magnet:?xt=urn:btih:"
        "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce";
    info = tr_magnetParse(uri);
    check(info != NULL);
    check_int(info->trackerCount, ==, 2);
    check_str(info->trackers[0], ==, "http://tracker.openbittorrent.com/announce");
    check_str(info->trackers[1], ==, "http://tracker.opentracker.org/announce");
    check_int(info->webseedCount, ==, 1);
    check_str(info->webseeds[0], ==, "http://server.webseed.org/path/to/file");
    check_str(info->displayName, ==, "Display Name");
    check_mem(info->hash, ==, dec, 20);

    tr_magnetFree(info);
    info = NULL;

    return 0;
}

MAIN_SINGLE_TEST(test1)
