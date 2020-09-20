/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef LIBTRANSMISSION_VARIANT_MODULE
#error only libtransmission/variant-*.c should #include this header.
#endif

#include "tr-macros.h"

TR_BEGIN_DECLS

typedef void (* VariantWalkFunc)(tr_variant const* val, void* user_data);

struct VariantWalkFuncs
{
    VariantWalkFunc intFunc;
    VariantWalkFunc boolFunc;
    VariantWalkFunc realFunc;
    VariantWalkFunc stringFunc;
    VariantWalkFunc dictBeginFunc;
    VariantWalkFunc listBeginFunc;
    VariantWalkFunc containerEndFunc;
};

void tr_variantWalk(tr_variant const* top, struct VariantWalkFuncs const* walkFuncs, void* user_data, bool sort_dicts);

void tr_variantToBufJson(tr_variant const* top, struct evbuffer* buf, bool lean);

void tr_variantToBufBenc(tr_variant const* top, struct evbuffer* buf);

void tr_variantInit(tr_variant* v, char type);

/* source - such as a filename. Only when logging an error */
int tr_jsonParse(char const* source, void const* vbuf, size_t len, tr_variant* setme_benc, char const** setme_end);

/** @brief Private function that's exposed here only for unit tests */
int tr_bencParseInt(void const* buf, void const* bufend, uint8_t const** setme_end, int64_t* setme_val);

/** @brief Private function that's exposed here only for unit tests */
int tr_bencParseStr(void const* buf, void const* bufend, uint8_t const** setme_end, uint8_t const** setme_str,
    size_t* setme_strlen);

int tr_variantParseBenc(void const* buf, void const* end, tr_variant* top, char const** setme_end);

TR_END_DECLS
