// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm>
#include <cstddef> // size_t
#include <cstdint> // int64_t
#include <optional>
#include <string>
#include <string_view>

#include "libtransmission/quark.h"

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
private:
    struct String
    {
        void set_shallow(std::string_view newval)
        {
            clear();

            type_ = Type::View;
            str_.str = std::data(newval);
            len_ = std::size(newval);
        }

        void set(std::string_view newval)
        {
            clear();

            len_ = std::size(newval);

            if (len_ < sizeof(str_.buf))
            {
                type_ = Type::Buf;
                std::copy_n(std::data(newval), len_, str_.buf);
                str_.buf[len_] = '\0';
            }
            else
            {
                char* const newstr = new char[len_ + 1];
                std::copy_n(std::data(newval), len_, newstr);
                newstr[len_] = '\0';

                type_ = Type::Heap;
                str_.str = newstr;
            }
        }

        [[nodiscard]] constexpr std::string_view get() const noexcept
        {
            return { type_ == Type::Buf ? str_.buf : str_.str, len_ };
        }

        void clear()
        {
            if (type_ == Type::Heap)
            {
                delete[] str_.str;
            }

            *this = {};
        }

    private:
        enum class Type
        {
            Heap,
            Buf,
            View
        };

        Type type_ = Type::View;
        size_t len_ = 0U;
        union
        {
            char buf[16];
            char const* str;
        } str_ = {};
    };

public:
    char type = '\0';

    tr_quark key = TR_KEY_NONE;

    union
    {
        bool b;

        double d;

        int64_t i;

        String s;

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
void tr_variantInitStrView(tr_variant* initme, std::string_view val);

constexpr void tr_variantInit(tr_variant* initme, char type)
{
    initme->val = {};
    initme->type = type;
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

// tr_variant serializer / deserializer
class tr_variant_serde
{
public:
    ~tr_variant_serde();

    static tr_variant_serde benc() noexcept
    {
        return tr_variant_serde{ Type::Benc };
    }

    static tr_variant_serde json() noexcept
    {
        return tr_variant_serde{ Type::Json };
    }

    // Serialize data as compactly as possible, e.g.
    // omit pretty-printing JSON whitespace
    constexpr tr_variant_serde& compact() noexcept
    {
        compact_ = true;
        return *this;
    }

    // When set, assumes that the `input` passed to parse() is valid
    // for the lifespan of the variant and we can use string_views of
    // `input` instead of cloning new strings.
    constexpr tr_variant_serde& inplace() noexcept
    {
        parse_inplace_ = true;
        return *this;
    }

    // ---

    [[nodiscard]] std::optional<tr_variant> parse(std::string_view input);

    template<typename CharSpan>
    [[nodiscard]] std::optional<tr_variant> parse(CharSpan const& input)
    {
        return parse(std::string_view{ std::data(input), std::size(input) });
    }

    [[nodiscard]] std::optional<tr_variant> parse_file(std::string_view filename);

    [[nodiscard]] constexpr char const* end() const noexcept
    {
        return end_;
    }

    // ---

    [[nodiscard]] std::string to_string(tr_variant const& var) const;

    bool to_file(tr_variant const& var, std::string_view filename);

    // ---

    // Tracks errors when parsing / saving
    tr_error* error_ = nullptr;

private:
    friend void tr_variantClear(tr_variant* clearme);

    enum class Type
    {
        Benc,
        Json
    };

    struct WalkFuncs
    {
        void (*int_func)(tr_variant const& var, int64_t val, void* user_data);
        void (*bool_func)(tr_variant const& var, bool val, void* user_data);
        void (*double_func)(tr_variant const& var, double val, void* user_data);
        void (*string_func)(tr_variant const& var, std::string_view val, void* user_data);
        void (*dict_begin_func)(tr_variant const& var, void* user_data);
        void (*list_begin_func)(tr_variant const& var, void* user_data);
        void (*container_end_func)(tr_variant const& var, void* user_data);
    };

    tr_variant_serde(Type type)
        : type_{ type }
    {
    }

    [[nodiscard]] std::optional<tr_variant> parse_json(std::string_view input);
    [[nodiscard]] std::optional<tr_variant> parse_benc(std::string_view input);

    [[nodiscard]] std::string to_json_string(tr_variant const& var) const;
    [[nodiscard]] static std::string to_benc_string(tr_variant const& var);

    static void walk(tr_variant const& top, WalkFuncs const& walk_funcs, void* user_data, bool sort_dicts);

    Type type_;

    bool compact_ = false;

    bool parse_inplace_ = false;

    // This is set to the first unparsed character after `parse()`.
    char const* end_ = nullptr;
};

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
