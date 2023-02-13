// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_VARIANT_MODULE
#error only libtransmission/variant-*.c should #include this header.
#endif

#include <cstdint> // int64_t
#include <optional>
#include <string>
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

void tr_variantWalk(tr_variant const* top, VariantWalkFuncs const* walk_funcs, void* user_data, bool sort_dicts);

[[nodiscard]] std::string tr_variantToStrJson(tr_variant const* top, bool lean);

[[nodiscard]] std::string tr_variantToStrBenc(tr_variant const* top);

/** @brief Private function that's exposed here only for unit tests */
[[nodiscard]] std::optional<int64_t> tr_bencParseInt(std::string_view* benc_inout);

/** @brief Private function that's exposed here only for unit tests */
[[nodiscard]] std::optional<std::string_view> tr_bencParseStr(std::string_view* benc_inout);

bool tr_variantParseBenc(tr_variant& top, int parse_opts, std::string_view benc, char const** setme_end, tr_error** error);

bool tr_variantParseJson(tr_variant& setme, int opts, std::string_view json, char const** setme_end, tr_error** error);
