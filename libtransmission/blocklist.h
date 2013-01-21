/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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

int                tr_blocklistFileExists       (const tr_blocklistFile  * b);

const char *       tr_blocklistFileGetFilename  (const tr_blocklistFile  * b);

int                tr_blocklistFileGetRuleCount (const tr_blocklistFile  * b);

void               tr_blocklistFileFree         (tr_blocklistFile        * b);

int                tr_blocklistFileIsEnabled    (tr_blocklistFile        * b);

void               tr_blocklistFileSetEnabled   (tr_blocklistFile        * b,
                                                 bool                      isEnabled);

int                tr_blocklistFileHasAddress   (tr_blocklistFile        * b,
                                                 const struct tr_address * addr);

int                tr_blocklistFileSetContent   (tr_blocklistFile        * b,
                                                 const char              * filename);

#endif
