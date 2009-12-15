/*
 * This file Copyright (C) 2009 Mnemosyne LLC
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

#ifndef TR_METAINFO_H
#define TR_METAINFO_H 1

#include "transmission.h"

struct tr_benc;
struct tr_magnet_info;

tr_bool  tr_metainfoParse( const tr_session     * session,
                           tr_info              * setmeInfo,
                           int                  * setmeInfoDictOffset,
                           int                  * setmeInfoDictLength,
                           const struct tr_benc * benc );

void tr_metainfoRemoveSaved( const tr_session * session,
                             const tr_info    * info );

void tr_metainfoMigrate( tr_session * session,
                         tr_info    * inf );

void tr_metainfoSetFromMagnet( tr_info * inf, const struct tr_magnet_info * m );


#endif
