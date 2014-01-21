/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_MAGNET_H
#define TR_MAGNET_H 1

#include "transmission.h"
#include "variant.h"

typedef struct tr_magnet_info
{
  uint8_t hash[20];

  char * displayName;

  int trackerCount;
  char ** trackers;

  int webseedCount;
  char ** webseeds;
}
tr_magnet_info;

tr_magnet_info * tr_magnetParse (const char * uri);

struct tr_variant;

void tr_magnetCreateMetainfo (const tr_magnet_info *, tr_variant *);

void tr_magnetFree (tr_magnet_info * info);

#endif
