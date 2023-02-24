// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // size_t
#include <cstdint> // int64_t
#include <string>
#include <string_view>

#include "quark.h"

struct tr_error;

/**
 * @addtogroup tr_variant Variant
 *
 * An object that acts like a union for
 * integers, strings, lists, dictionaries, booleans, and floating-point numbers.
 * The structure is named `tr_variant` due to the historical reason that it was
 * originally tightly coupled with bencoded data. It currently supports
 * being parsed from, and serialized to, both bencoded notation and json notation.
 *
 * @{
 */

enum tr_string_type
{
    TR_STRING_TYPE_QUARK,
    TR_STRING_TYPE_HEAP,
    TR_STRING_TYPE_BUF,
    TR_STRING_TYPE_VIEW
};

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
struct tr_variant_string
{
    tr_string_type type;
    size_t len;
    union
    {
        char buf[16];
        char const* str;
    } str;
};

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
enum
{
    TR_VARIANT_TYPE_INT = 1,
    TR_VARIANT_TYPE_STR = 2,
    TR_VARIANT_TYPE_LIST = 4,
    TR_VARIANT_TYPE_DICT = 8,
    TR_VARIANT_TYPE_BOOL = 16,
    TR_VARIANT_TYPE_REAL = 32
};

/* These are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
struct tr_variant
{
    char type = '\0';

    tr_quark key = TR_KEY_NONE;

    union
    {
        bool b;

        double d;

        int64_t i;

        struct tr_variant_string s;

        struct
        {
            size_t alloc;
            size_t count;
            struct tr_variant* vals;
        } l;
    } val = {};
};

/**
 * @brief Clear the variant to an empty state.
 *
 * `tr_variantIsEmpty()` will return true after this is called.
 *
 * The variant itself is not freed, but any memory used by
 * its *value* -- e.g. a string or child variants -- is freed.
 */
void tr_variantClear(tr_variant* clearme);

// --- Serialization / Deserialization

enum tr_variant_fmt
{
    TR_VARIANT_FMT_BENC,
    TR_VARIANT_FMT_JSON,
    TR_VARIANT_FMT_JSON_LEAN /* saves bandwidth by omitting all whitespace. */
};

int tr_variantToFile(tr_variant const* variant, tr_variant_fmt fmt, std::string_view filename);

[[nodiscard]] std::string tr_variantToStr(tr_variant const* variant, tr_variant_fmt fmt);

enum tr_variant_parse_opts
{
    TR_VARIANT_PARSE_BENC = (1 << 0),
    TR_VARIANT_PARSE_JSON = (1 << 1),
    TR_VARIANT_PARSE_INPLACE = (1 << 2)
};

bool tr_variantFromFile(
    tr_variant* setme,
    tr_variant_parse_opts opts,
    std::string_view filename,
    struct tr_error** error = nullptr);

bool tr_variantFromBuf(
    tr_variant* setme,
    int variant_parse_opts,
    std::string_view buf,
    char const** setme_end = nullptr,
    tr_error** error = nullptr);

template<typename T>
bool tr_variantFromBuf(
    tr_variant* setme,
    int variant_parse_opts,
    T const& buf,
    char const** setme_end = nullptr,
    tr_error** error = nullptr)
{
    return tr_variantFromBuf(
        setme,
        variant_parse_opts,
        std::string_view{ std::data(buf), static_cast<size_t>(std::size(buf)) },
        setme_end,
        error);
}

[[nodiscard]] constexpr bool tr_variantIsType(tr_variant const* b, int type)
{
    return b != nullptr && b->type == type;
}

[[nodiscard]] constexpr bool tr_variantIsEmpty(tr_variant const* b)
{
    return b == nullptr || b->type == '\0';
}

// --- Strings

[[nodiscard]] constexpr bool tr_variantIsString(tr_variant const* b)
{
    return b != nullptr && b->type == TR_VARIANT_TYPE_STR;
}

bool tr_variantGetStrView(tr_variant const* variant, std::string_view* setme);

void tr_variantInitStr(tr_variant* initme, std::string_view value);
void tr_variantInitQuark(tr_variant* initme, tr_quark value);
void tr_variantInitRaw(tr_variant* initme, void const* value, size_t value_len);

constexpr void tr_variantInit(tr_variant* initme, char type)
{
    initme->val = {};
    initme->type = type;
}

constexpr void tr_variantInitStrView(tr_variant* initme, std::string_view in)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_STR);
    initme->val.s.type = TR_STRING_TYPE_VIEW;
    initme->val.s.len = std::size(in);
    initme->val.s.str.str = std::data(in);
}

bool tr_variantGetRaw(tr_variant const* variant, std::byte const** setme_raw, size_t* setme_len);
bool tr_variantGetRaw(tr_variant const* variant, uint8_t const** setme_raw, size_t* setme_len);

// --- Real Numbers

[[nodiscard]] constexpr bool tr_variantIsReal(tr_variant const* v)
{
    return v != nullptr && v->type == TR_VARIANT_TYPE_REAL;
}

bool tr_variantGetReal(tr_variant const* variant, double* value_setme);

constexpr void tr_variantInitReal(tr_variant* initme, double value)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_REAL);
    initme->val.d = value;
}

// --- Booleans

[[nodiscard]] constexpr bool tr_variantIsBool(tr_variant const* v)
{
    return v != nullptr && v->type == TR_VARIANT_TYPE_BOOL;
}

bool tr_variantGetBool(tr_variant const* variant, bool* setme);

constexpr void tr_variantInitBool(tr_variant* initme, bool value)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_BOOL);
    initme->val.b = value;
}

// --- Ints

[[nodiscard]] constexpr bool tr_variantIsInt(tr_variant const* v)
{
    return v != nullptr && v->type == TR_VARIANT_TYPE_INT;
}

bool tr_variantGetInt(tr_variant const* val, int64_t* setme);

constexpr void tr_variantInitInt(tr_variant* initme, int64_t value)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_INT);
    initme->val.i = value;
}

// --- Lists

[[nodiscard]] constexpr bool tr_variantIsList(tr_variant const* v)
{
    return v != nullptr && v->type == TR_VARIANT_TYPE_LIST;
}

void tr_variantInitList(tr_variant* initme, size_t reserve_count);
void tr_variantListReserve(tr_variant* list, size_t reserve_count);

tr_variant* tr_variantListAdd(tr_variant* list);
tr_variant* tr_variantListAddBool(tr_variant* list, bool value);
tr_variant* tr_variantListAddInt(tr_variant* list, int64_t value);
tr_variant* tr_variantListAddReal(tr_variant* list, double value);
tr_variant* tr_variantListAddStr(tr_variant* list, std::string_view value);
tr_variant* tr_variantListAddStrView(tr_variant* list, std::string_view value);
tr_variant* tr_variantListAddQuark(tr_variant* list, tr_quark value);
tr_variant* tr_variantListAddRaw(tr_variant* list, void const* value, size_t value_len);
tr_variant* tr_variantListAddList(tr_variant* list, size_t reserve_count);
tr_variant* tr_variantListAddDict(tr_variant* list, size_t reserve_count);
tr_variant* tr_variantListChild(tr_variant* list, size_t pos);

bool tr_variantListRemove(tr_variant* list, size_t pos);

[[nodiscard]] constexpr size_t tr_variantListSize(tr_variant const* list)
{
    return tr_variantIsList(list) ? list->val.l.count : 0;
}

// --- Dictionaries

[[nodiscard]] constexpr bool tr_variantIsDict(tr_variant const* v)
{
    return v != nullptr && v->type == TR_VARIANT_TYPE_DICT;
}

void tr_variantInitDict(tr_variant* initme, size_t reserve_count);
void tr_variantDictReserve(tr_variant* dict, size_t reserve_count);
bool tr_variantDictRemove(tr_variant* dict, tr_quark key);

tr_variant* tr_variantDictAdd(tr_variant* dict, tr_quark key);
tr_variant* tr_variantDictAddReal(tr_variant* dict, tr_quark key, double value);
tr_variant* tr_variantDictAddInt(tr_variant* dict, tr_quark key, int64_t value);
tr_variant* tr_variantDictAddBool(tr_variant* dict, tr_quark key, bool value);
tr_variant* tr_variantDictAddStr(tr_variant* dict, tr_quark key, std::string_view value);
tr_variant* tr_variantDictAddStrView(tr_variant* dict, tr_quark key, std::string_view value);
tr_variant* tr_variantDictAddQuark(tr_variant* dict, tr_quark key, tr_quark val);
tr_variant* tr_variantDictAddList(tr_variant* dict, tr_quark key, size_t reserve_count);
tr_variant* tr_variantDictAddDict(tr_variant* dict, tr_quark key, size_t reserve_count);
tr_variant* tr_variantDictSteal(tr_variant* dict, tr_quark key, tr_variant* value);
tr_variant* tr_variantDictAddRaw(tr_variant* dict, tr_quark key, void const* value, size_t len);

bool tr_variantDictChild(tr_variant* dict, size_t pos, tr_quark* setme_key, tr_variant** setme_value);
tr_variant* tr_variantDictFind(tr_variant* dict, tr_quark key);
bool tr_variantDictFindList(tr_variant* dict, tr_quark key, tr_variant** setme);
bool tr_variantDictFindDict(tr_variant* dict, tr_quark key, tr_variant** setme_value);
bool tr_variantDictFindInt(tr_variant* dict, tr_quark key, int64_t* setme);
bool tr_variantDictFindReal(tr_variant* dict, tr_quark key, double* setme);
bool tr_variantDictFindBool(tr_variant* dict, tr_quark key, bool* setme);
bool tr_variantDictFindStrView(tr_variant* dict, tr_quark key, std::string_view* setme);
bool tr_variantDictFindRaw(tr_variant* dict, tr_quark key, uint8_t const** setme_raw, size_t* setme_len);
bool tr_variantDictFindRaw(tr_variant* dict, tr_quark key, std::byte const** setme_raw, size_t* setme_len);

/* this is only quasi-supported. don't rely on it too heavily outside of libT */
void tr_variantMergeDicts(tr_variant* dict_target, tr_variant const* dict_source);

namespace libtransmission
{

struct VariantConverter
{
public:
    template<typename T>
    static std::optional<T> load(tr_variant* src);

    template<typename T>
    static void save(tr_variant* tgt, T const& val);
};

} // namespace libtransmission

/* @} */
