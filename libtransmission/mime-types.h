/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */

#pragma once

#define MIME_TYPE_SUFFIX_MAXLEN 24
#define MIME_TYPE_SUFFIX_COUNT 1201

struct mime_type_suffix
{
    char const* suffix;
    char const* mime_type;
};

extern struct mime_type_suffix const mime_type_suffixes[MIME_TYPE_SUFFIX_COUNT];
