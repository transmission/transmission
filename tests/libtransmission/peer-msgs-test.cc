// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring>

#include "transmission.h"
#include "peer-msgs.h"

#include "gtest/gtest.h"

TEST(PeerMsgs, placeholder)
{
#if 0

    auto infohash = tr_sha1_digest_t{};
    tr_piece_index_t pieceCount = 1313;
    size_t numwant;
    size_t numgot;
    tr_piece_index_t pieces[] = { 1059, 431, 808, 1217, 287, 376, 1188, 353, 508 };
    tr_piece_index_t buf[16];

    memset(std::data(infohash), 0xaa, std::size(infohash));

    auto const addr = tr_address::fromString("80.4.4.200");

    numwant = 7;
    numgot = tr_generateAllowedSet(buf, numwant, pieceCount, infohash, &addr);
    check_uint(numgot, ==, numwant);
    check_mem(buf, ==, pieces, numgot);

    numwant = 9;
    numgot = tr_generateAllowedSet(buf, numwant, pieceCount, infohash, &addr);
    check_uint(numgot, ==, numwant);
    check_mem(buf, ==, pieces, numgot);

#endif
}
