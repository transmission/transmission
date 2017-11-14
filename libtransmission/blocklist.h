/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

struct tr_address;

typedef struct tr_blocklistFile tr_blocklistFile;

tr_blocklistFile* tr_blocklistFileNew(char const* filename, bool isEnabled);

bool tr_blocklistFileExists(tr_blocklistFile const* b);

char const* tr_blocklistFileGetFilename(tr_blocklistFile const* b);

int tr_blocklistFileGetRuleCount(tr_blocklistFile const* b);

void tr_blocklistFileFree(tr_blocklistFile* b);

bool tr_blocklistFileIsEnabled(tr_blocklistFile* b);

void tr_blocklistFileSetEnabled(tr_blocklistFile* b, bool isEnabled);

bool tr_blocklistFileHasAddress(tr_blocklistFile* b, struct tr_address const* addr);

int tr_blocklistFileSetContent(tr_blocklistFile* b, char const* filename);
