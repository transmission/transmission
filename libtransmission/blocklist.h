/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_BLOCKLIST_H
#define TR_BLOCKLIST_H

struct tr_address;

typedef struct tr_blocklistFile tr_blocklistFile;

tr_blocklistFile * tr_blocklistFileNew          (const char              * filename,
                                                 bool                      isEnabled);

bool               tr_blocklistFileExists       (const tr_blocklistFile  * b);

const char *       tr_blocklistFileGetFilename  (const tr_blocklistFile  * b);

int                tr_blocklistFileGetRuleCount (const tr_blocklistFile  * b);

void               tr_blocklistFileFree         (tr_blocklistFile        * b);

bool               tr_blocklistFileIsEnabled    (tr_blocklistFile        * b);

void               tr_blocklistFileSetEnabled   (tr_blocklistFile        * b,
                                                 bool                      isEnabled);

bool               tr_blocklistFileHasAddress   (tr_blocklistFile        * b,
                                                 const struct tr_address * addr);

int                tr_blocklistFileSetContent   (tr_blocklistFile        * b,
                                                 const char              * filename);

#endif
