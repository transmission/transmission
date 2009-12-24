/*
 * This file Copyright (C) 2008-2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_RESUME_H
#define TR_RESUME_H

enum
{
    TR_FR_DOWNLOADED          = ( 1 << 0 ),
    TR_FR_UPLOADED            = ( 1 << 1 ),
    TR_FR_CORRUPT             = ( 1 << 2 ),
    TR_FR_PEERS               = ( 1 << 3 ),
    TR_FR_PROGRESS            = ( 1 << 4 ),
    TR_FR_DND                 = ( 1 << 5 ),
    TR_FR_FILE_PRIORITIES     = ( 1 << 6 ),
    TR_FR_BANDWIDTH_PRIORITY  = ( 1 << 7 ),
    TR_FR_SPEEDLIMIT          = ( 1 << 8 ),
    TR_FR_RUN                 = ( 1 << 9 ),
    TR_FR_DOWNLOAD_DIR        = ( 1 << 10 ),
    TR_FR_INCOMPLETE_DIR      = ( 1 << 11 ),
    TR_FR_MAX_PEERS           = ( 1 << 12 ),
    TR_FR_ADDED_DATE          = ( 1 << 13 ),
    TR_FR_DONE_DATE           = ( 1 << 14 ),
    TR_FR_ACTIVITY_DATE       = ( 1 << 15 ),
    TR_FR_RATIOLIMIT          = ( 1 << 16 ),
    TR_FR_STREAMING           = ( 1 << 17 )
};

/**
 * Returns a bitwise-or'ed set of the loaded resume data
 */
uint64_t tr_torrentLoadResume( tr_torrent *    tor,
                               uint64_t        fieldsToLoad,
                               const tr_ctor * ctor );

void     tr_torrentSaveResume( const tr_torrent * tor );

void     tr_torrentRemoveResume( const tr_torrent * tor );

#endif
