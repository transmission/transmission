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

#include <optional>
#include <string_view>

#include "transmission.h"

#include "variant.h"

using VariantWalkFunc = void (*)(tr_variant const* val, void* user_data);

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

/** @brief Private function that's exposed here only for unit tests */
std::optional<int64_t> tr_bencParseInt(std::string_view* benc_inout);

/** @brief Private function that's exposed here only for unit tests */
std::optional<std::string_view> tr_bencParseStr(std::string_view* benc_inout);

int tr_variantParseBenc(tr_variant& setme, int opts, std::string_view benc, char const** setme_end);

int tr_variantParseJson(tr_variant& setme, int opts, std::string_view benc, char const** setme_end);
