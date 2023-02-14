// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::sort
#include <cstring>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <share.h>
#endif

#include <fmt/core.h>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "transmission.h"

#include "error.h"
#include "file.h"
#include "log.h"
#include "quark.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant-common.h"
#include "variant.h"

using namespace std::literals;

namespace
{
constexpr bool tr_variantIsContainer(tr_variant const* v)
{
    return tr_variantIsList(v) || tr_variantIsDict(v);
}

// ---

auto constexpr StringInit = tr_variant_string{
    TR_STRING_TYPE_QUARK,
    0,
    {},
};

void tr_variant_string_clear(struct tr_variant_string* str)
{
    if (str->type == TR_STRING_TYPE_HEAP)
    {
        delete[] const_cast<char*>(str->str.str);
    }

    *str = StringInit;
}

/* returns a const pointer to the variant's string */
constexpr char const* tr_variant_string_get_string(struct tr_variant_string const* str)
{
    switch (str->type)
    {
    case TR_STRING_TYPE_BUF:
        return str->str.buf;

    case TR_STRING_TYPE_HEAP:
    case TR_STRING_TYPE_QUARK:
    case TR_STRING_TYPE_VIEW:
        return str->str.str;

    default:
        return nullptr;
    }
}

void tr_variant_string_set_quark(struct tr_variant_string* str, tr_quark quark)
{
    tr_variant_string_clear(str);

    str->type = TR_STRING_TYPE_QUARK;
    auto const sv = tr_quark_get_string_view(quark);
    str->str.str = std::data(sv);
    str->len = std::size(sv);
}

void tr_variant_string_set_string(struct tr_variant_string* str, std::string_view in)
{
    tr_variant_string_clear(str);

    auto const* const bytes = std::data(in);
    auto const len = std::size(in);

    if (len < sizeof(str->str.buf))
    {
        str->type = TR_STRING_TYPE_BUF;
        if (len > 0)
        {
            std::copy_n(bytes, len, str->str.buf);
        }

        str->str.buf[len] = '\0';
        str->len = len;
    }
    else
    {
        auto* tmp = new char[len + 1];
        std::copy_n(bytes, len, tmp);
        tmp[len] = '\0';
        str->type = TR_STRING_TYPE_HEAP;
        str->str.str = tmp;
        str->len = len;
    }
}

// ---

constexpr char const* getStr(tr_variant const* v)
{
    TR_ASSERT(tr_variantIsString(v));

    return tr_variant_string_get_string(&v->val.s);
}

constexpr int dictIndexOf(tr_variant const* dict, tr_quark key)
{
    if (tr_variantIsDict(dict))
    {
        for (size_t i = 0; i < dict->val.l.count; ++i)
        {
            if (dict->val.l.vals[i].key == key)
            {
                return (int)i;
            }
        }
    }

    return -1;
}

bool dictFindType(tr_variant* dict, tr_quark key, int type, tr_variant** setme)
{
    *setme = tr_variantDictFind(dict, key);
    return tr_variantIsType(*setme, type);
}

tr_variant* containerReserve(tr_variant* v, size_t count)
{
    TR_ASSERT(tr_variantIsContainer(v));

    if (size_t const needed = v->val.l.count + count; needed > v->val.l.alloc)
    {
        /* scale the alloc size in powers-of-2 */
        size_t n = v->val.l.alloc != 0 ? v->val.l.alloc : 8;

        while (n < needed)
        {
            n *= 2U;
        }

        auto* vals = new tr_variant[n];
        std::copy_n(v->val.l.vals, v->val.l.count, vals);
        delete[] v->val.l.vals;
        v->val.l.vals = vals;
        v->val.l.alloc = n;
    }

    return v->val.l.vals + v->val.l.count;
}

tr_variant* dictFindOrAdd(tr_variant* dict, tr_quark key, int type)
{
    /* see if it already exists, and if so, try to reuse it */
    tr_variant* child = tr_variantDictFind(dict, key);
    if (child != nullptr)
    {
        if (!tr_variantIsType(child, type))
        {
            tr_variantDictRemove(dict, key);
            child = nullptr;
        }
        else if (child->type == TR_VARIANT_TYPE_STR)
        {
            tr_variant_string_clear(&child->val.s);
        }
    }

    /* if it doesn't exist, create it */
    if (child == nullptr)
    {
        child = tr_variantDictAdd(dict, key);
    }

    return child;
}

} // namespace

tr_variant* tr_variantDictFind(tr_variant* dict, tr_quark key)
{
    auto const i = dictIndexOf(dict, key);

    return i < 0 ? nullptr : dict->val.l.vals + i;
}

tr_variant* tr_variantListChild(tr_variant* list, size_t pos)
{
    if (tr_variantIsList(list) && pos < list->val.l.count)
    {
        return list->val.l.vals + pos;
    }

    return {};
}

bool tr_variantListRemove(tr_variant* list, size_t pos)
{
    if (!tr_variantIsList(list) && pos < list->val.l.count)
    {
        return false;
    }

    auto& vals = list->val.l.vals;
    auto& count = list->val.l.count;

    tr_variantClear(&vals[pos]);
    std::move(vals + pos + 1, vals + count, vals + pos);
    --count;
    vals[count] = {};

    return true;
}

bool tr_variantGetInt(tr_variant const* v, int64_t* setme)
{
    bool success = false;

    if (tr_variantIsInt(v))
    {
        if (setme != nullptr)
        {
            *setme = v->val.i;
        }

        success = true;
    }

    if (!success && tr_variantIsBool(v))
    {
        if (setme != nullptr)
        {
            *setme = v->val.b ? 1 : 0;
        }

        success = true;
    }

    return success;
}

bool tr_variantGetStrView(tr_variant const* v, std::string_view* setme)
{
    if (!tr_variantIsString(v))
    {
        return false;
    }

    char const* const str = tr_variant_string_get_string(&v->val.s);
    size_t const len = v->val.s.len;
    *setme = std::string_view{ str, len };
    return true;
}

bool tr_variantGetRaw(tr_variant const* v, std::byte const** setme_raw, size_t* setme_len)
{
    bool const success = tr_variantIsString(v);

    if (success)
    {
        *setme_raw = reinterpret_cast<std::byte const*>(getStr(v));
        *setme_len = v->val.s.len;
    }

    return success;
}

bool tr_variantGetRaw(tr_variant const* v, uint8_t const** setme_raw, size_t* setme_len)
{
    bool const success = tr_variantIsString(v);

    if (success)
    {
        *setme_raw = reinterpret_cast<uint8_t const*>(getStr(v));
        *setme_len = v->val.s.len;
    }

    return success;
}

bool tr_variantGetBool(tr_variant const* v, bool* setme)
{
    if (tr_variantIsBool(v))
    {
        *setme = v->val.b;
        return true;
    }

    if (tr_variantIsInt(v) && (v->val.i == 0 || v->val.i == 1))
    {
        *setme = v->val.i != 0;
        return true;
    }

    if (auto sv = std::string_view{}; tr_variantGetStrView(v, &sv))
    {
        if (sv == "true"sv)
        {
            *setme = true;
            return true;
        }

        if (sv == "false"sv)
        {
            *setme = false;
            return true;
        }
    }

    return false;
}

bool tr_variantGetReal(tr_variant const* v, double* setme)
{
    bool success = false;

    if (tr_variantIsReal(v))
    {
        *setme = v->val.d;
        success = true;
    }

    if (!success && tr_variantIsInt(v))
    {
        *setme = (double)v->val.i;
        success = true;
    }

    if (!success && tr_variantIsString(v))
    {
        if (auto sv = std::string_view{}; tr_variantGetStrView(v, &sv))
        {
            if (auto d = tr_parseNum<double>(sv); d)
            {
                *setme = *d;
                success = true;
            }
        }
    }

    return success;
}

bool tr_variantDictFindInt(tr_variant* dict, tr_quark key, int64_t* setme)
{
    tr_variant const* child = tr_variantDictFind(dict, key);
    return tr_variantGetInt(child, setme);
}

bool tr_variantDictFindBool(tr_variant* dict, tr_quark key, bool* setme)
{
    tr_variant const* child = tr_variantDictFind(dict, key);
    return tr_variantGetBool(child, setme);
}

bool tr_variantDictFindReal(tr_variant* dict, tr_quark key, double* setme)
{
    tr_variant const* child = tr_variantDictFind(dict, key);
    return tr_variantGetReal(child, setme);
}

bool tr_variantDictFindStrView(tr_variant* dict, tr_quark key, std::string_view* setme)
{
    tr_variant const* const child = tr_variantDictFind(dict, key);
    return tr_variantGetStrView(child, setme);
}

bool tr_variantDictFindList(tr_variant* dict, tr_quark key, tr_variant** setme)
{
    return dictFindType(dict, key, TR_VARIANT_TYPE_LIST, setme);
}

bool tr_variantDictFindDict(tr_variant* dict, tr_quark key, tr_variant** setme)
{
    return dictFindType(dict, key, TR_VARIANT_TYPE_DICT, setme);
}

bool tr_variantDictFindRaw(tr_variant* dict, tr_quark key, uint8_t const** setme_raw, size_t* setme_len)
{
    auto const* child = tr_variantDictFind(dict, key);
    return tr_variantGetRaw(child, setme_raw, setme_len);
}

bool tr_variantDictFindRaw(tr_variant* dict, tr_quark key, std::byte const** setme_raw, size_t* setme_len)
{
    auto const* child = tr_variantDictFind(dict, key);
    return tr_variantGetRaw(child, setme_raw, setme_len);
}

// ---

void tr_variantInitRaw(tr_variant* initme, void const* value, size_t value_len)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_STR);
    tr_variant_string_set_string(&initme->val.s, { static_cast<char const*>(value), value_len });
}

void tr_variantInitQuark(tr_variant* initme, tr_quark value)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_STR);
    tr_variant_string_set_quark(&initme->val.s, value);
}

void tr_variantInitStr(tr_variant* initme, std::string_view value)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_STR);
    tr_variant_string_set_string(&initme->val.s, value);
}

void tr_variantInitList(tr_variant* initme, size_t reserve_count)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_LIST);
    tr_variantListReserve(initme, reserve_count);
}

void tr_variantListReserve(tr_variant* list, size_t count)
{
    TR_ASSERT(tr_variantIsList(list));

    containerReserve(list, count);
}

void tr_variantInitDict(tr_variant* initme, size_t reserve_count)
{
    tr_variantInit(initme, TR_VARIANT_TYPE_DICT);
    tr_variantDictReserve(initme, reserve_count);
}

void tr_variantDictReserve(tr_variant* dict, size_t reserve_count)
{
    TR_ASSERT(tr_variantIsDict(dict));

    containerReserve(dict, reserve_count);
}

tr_variant* tr_variantListAdd(tr_variant* list)
{
    TR_ASSERT(tr_variantIsList(list));

    tr_variant* child = containerReserve(list, 1);
    ++list->val.l.count;
    child->key = 0;
    tr_variantInit(child, TR_VARIANT_TYPE_INT);

    return child;
}

tr_variant* tr_variantListAddInt(tr_variant* list, int64_t value)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitInt(child, value);
    return child;
}

tr_variant* tr_variantListAddReal(tr_variant* list, double value)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitReal(child, value);
    return child;
}

tr_variant* tr_variantListAddBool(tr_variant* list, bool value)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitBool(child, value);
    return child;
}

tr_variant* tr_variantListAddStr(tr_variant* list, std::string_view value)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitStr(child, value);
    return child;
}

tr_variant* tr_variantListAddStrView(tr_variant* list, std::string_view value)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitStrView(child, value);
    return child;
}

tr_variant* tr_variantListAddQuark(tr_variant* list, tr_quark value)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitQuark(child, value);
    return child;
}

tr_variant* tr_variantListAddRaw(tr_variant* list, void const* value, size_t value_len)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitRaw(child, value, value_len);
    return child;
}

tr_variant* tr_variantListAddList(tr_variant* list, size_t reserve_count)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitList(child, reserve_count);
    return child;
}

tr_variant* tr_variantListAddDict(tr_variant* list, size_t reserve_count)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitDict(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictAdd(tr_variant* dict, tr_quark key)
{
    TR_ASSERT(tr_variantIsDict(dict));

    tr_variant* val = containerReserve(dict, 1);
    ++dict->val.l.count;
    val->key = key;
    tr_variantInit(val, TR_VARIANT_TYPE_INT);

    return val;
}

tr_variant* tr_variantDictAddInt(tr_variant* dict, tr_quark key, int64_t val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_INT);
    tr_variantInitInt(child, val);
    return child;
}

tr_variant* tr_variantDictAddBool(tr_variant* dict, tr_quark key, bool val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_BOOL);
    tr_variantInitBool(child, val);
    return child;
}

tr_variant* tr_variantDictAddReal(tr_variant* dict, tr_quark key, double val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_REAL);
    tr_variantInitReal(child, val);
    return child;
}

tr_variant* tr_variantDictAddQuark(tr_variant* dict, tr_quark key, tr_quark const val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_STR);
    tr_variantInitQuark(child, val);
    return child;
}

tr_variant* tr_variantDictAddStr(tr_variant* dict, tr_quark key, std::string_view val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_STR);
    tr_variantInitStr(child, val);
    return child;
}

tr_variant* tr_variantDictAddStrView(tr_variant* dict, tr_quark key, std::string_view val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_STR);
    tr_variantInitStrView(child, val);
    return child;
}

tr_variant* tr_variantDictAddRaw(tr_variant* dict, tr_quark key, void const* value, size_t len)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_STR);
    tr_variantInitRaw(child, value, len);
    return child;
}

tr_variant* tr_variantDictAddList(tr_variant* dict, tr_quark key, size_t reserve_count)
{
    tr_variant* child = tr_variantDictAdd(dict, key);
    tr_variantInitList(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictAddDict(tr_variant* dict, tr_quark key, size_t reserve_count)
{
    tr_variant* child = tr_variantDictAdd(dict, key);
    tr_variantInitDict(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictSteal(tr_variant* dict, tr_quark key, tr_variant* value)
{
    tr_variant* child = tr_variantDictAdd(dict, key);
    *child = *value;
    child->key = key;
    tr_variantInit(value, value->type);
    return child;
}

bool tr_variantDictRemove(tr_variant* dict, tr_quark key)
{
    bool removed = false;

    if (int const i = dictIndexOf(dict, key); i >= 0)
    {
        int const last = (int)dict->val.l.count - 1;

        tr_variantClear(&dict->val.l.vals[i]);

        if (i != last)
        {
            dict->val.l.vals[i] = dict->val.l.vals[last];
        }

        --dict->val.l.count;

        removed = true;
    }

    return removed;
}

// --- BENC WALKING

class WalkNode
{
public:
    explicit WalkNode(tr_variant const* v_in)
    {
        assign(v_in);
    }

    tr_variant const* nextChild()
    {
        if (!tr_variantIsContainer(&v) || (child_index >= v.val.l.count))
        {
            return nullptr;
        }

        auto idx = child_index++;
        if (!sorted.empty())
        {
            idx = sorted[idx];
        }

        return v.val.l.vals + idx;
    }

    bool is_visited = false;

    // shallow bitwise copy of the variant passed to the constructor
    tr_variant v = {};

protected:
    friend class VariantWalker;

    void assign(tr_variant const* v_in)
    {
        is_visited = false;
        v = *v_in;
        child_index = 0;
        sorted.clear();
    }

    struct ByKey
    {
        std::string_view key;
        size_t idx = {};
    };

    void sort(std::vector<ByKey>& sortbuf)
    {
        if (!tr_variantIsDict(&v))
        {
            return;
        }

        auto const n = v.val.l.count;
        auto const* children = v.val.l.vals;

        sortbuf.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            sortbuf[i] = { tr_quark_get_string_view(children[i].key), i };
        }

        std::sort(std::begin(sortbuf), std::end(sortbuf), [](ByKey const& a, ByKey const& b) { return a.key < b.key; });

        //  keep the sorted indices

        sorted.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            sorted[i] = sortbuf[i].idx;
        }
    }

private:
    // When walking `v`'s children, this is the index of the next child
    size_t child_index = 0;

    // When `v` is a dict, this is its children's indices sorted by key.
    // Bencoded dicts must be sorted, so this is useful when writing benc.
    std::vector<size_t> sorted;
};

class VariantWalker
{
public:
    void emplace(tr_variant const* v_in, bool sort_dicts)
    {
        if (size == std::size(stack))
        {
            stack.emplace_back(v_in);
        }
        else
        {
            stack[size].assign(v_in);
        }

        ++size;

        if (sort_dicts)
        {
            top().sort(sortbuf);
        }
    }

    void pop()
    {
        TR_ASSERT(size > 0);
        if (size > 0)
        {
            --size;
        }
    }

    [[nodiscard]] bool empty() const
    {
        return size == 0;
    }

    WalkNode& top()
    {
        TR_ASSERT(size > 0);
        return stack[size - 1];
    }

private:
    size_t size = 0;
    std::vector<WalkNode> stack;
    std::vector<WalkNode::ByKey> sortbuf;
};

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted data. (#667)
 */
void tr_variantWalk(tr_variant const* top, struct VariantWalkFuncs const* walk_funcs, void* user_data, bool sort_dicts)
{
    auto stack = VariantWalker{};
    stack.emplace(top, sort_dicts);

    while (!stack.empty())
    {
        auto& node = stack.top();
        tr_variant const* v = nullptr;

        if (!node.is_visited)
        {
            v = &node.v;

            node.is_visited = true;
        }
        else
        {
            v = node.nextChild();

            if (v != nullptr)
            {
                if (tr_variantIsDict(&node.v))
                {
                    auto tmp = tr_variant{};
                    tr_variantInitQuark(&tmp, v->key);
                    walk_funcs->stringFunc(&tmp, user_data);
                }
            }
            else // finished with this node
            {
                if (tr_variantIsContainer(&node.v))
                {
                    walk_funcs->containerEndFunc(&node.v, user_data);
                }

                stack.pop();
                continue;
            }
        }

        if (v != nullptr)
        {
            switch (v->type)
            {
            case TR_VARIANT_TYPE_INT:
                walk_funcs->intFunc(v, user_data);
                break;

            case TR_VARIANT_TYPE_BOOL:
                walk_funcs->boolFunc(v, user_data);
                break;

            case TR_VARIANT_TYPE_REAL:
                walk_funcs->realFunc(v, user_data);
                break;

            case TR_VARIANT_TYPE_STR:
                walk_funcs->stringFunc(v, user_data);
                break;

            case TR_VARIANT_TYPE_LIST:
                if (v == &node.v)
                {
                    walk_funcs->listBeginFunc(v, user_data);
                }
                else
                {
                    stack.emplace(v, sort_dicts);
                }
                break;

            case TR_VARIANT_TYPE_DICT:
                if (v == &node.v)
                {
                    walk_funcs->dictBeginFunc(v, user_data);
                }
                else
                {
                    stack.emplace(v, sort_dicts);
                }
                break;

            default:
                /* did caller give us an uninitialized val? */
                tr_logAddError(_("Invalid metadata"));
                break;
            }
        }
    }
}

// ---

namespace
{
namespace clear_helpers
{
void freeDummyFunc(tr_variant const* /*v*/, void* /*buf*/)
{
}

void freeStringFunc(tr_variant const* v, void* /*user_data*/)
{
    tr_variant_string_clear(&const_cast<tr_variant*>(v)->val.s);
}

void freeContainerEndFunc(tr_variant const* v, void* /*user_data*/)
{
    delete[] v->val.l.vals;
}

VariantWalkFuncs constexpr FreeWalkFuncs = {
    freeDummyFunc, //
    freeDummyFunc, //
    freeDummyFunc, //
    freeStringFunc, //
    freeDummyFunc, //
    freeDummyFunc, //
    freeContainerEndFunc, //
};
} // namespace clear_helpers
} // namespace

void tr_variantClear(tr_variant* clearme)
{
    using namespace clear_helpers;

    if (!tr_variantIsEmpty(clearme))
    {
        tr_variantWalk(clearme, &FreeWalkFuncs, nullptr, false);
    }

    *clearme = {};
}

// ---

bool tr_variantDictChild(tr_variant* dict, size_t pos, tr_quark* key, tr_variant** setme_value)
{
    TR_ASSERT(tr_variantIsDict(dict));

    bool success = false;

    if (tr_variantIsDict(dict) && pos < dict->val.l.count)
    {
        *key = dict->val.l.vals[pos].key;
        *setme_value = dict->val.l.vals + pos;
        success = true;
    }

    return success;
}

namespace
{
namespace merge_helpers
{
void tr_variantListCopy(tr_variant* target, tr_variant const* src)
{
    for (size_t i = 0;; ++i)
    {
        auto const* const child = tr_variantListChild(const_cast<tr_variant*>(src), i);
        if (child == nullptr)
        {
            break;
        }

        if (tr_variantIsBool(child))
        {
            auto val = bool{};
            tr_variantGetBool(child, &val);
            tr_variantListAddBool(target, val);
        }
        else if (tr_variantIsReal(child))
        {
            auto val = double{};
            tr_variantGetReal(child, &val);
            tr_variantListAddReal(target, val);
        }
        else if (tr_variantIsInt(child))
        {
            auto val = int64_t{};
            tr_variantGetInt(child, &val);
            tr_variantListAddInt(target, val);
        }
        else if (tr_variantIsString(child))
        {
            auto val = std::string_view{};
            (void)tr_variantGetStrView(child, &val);
            tr_variantListAddRaw(target, std::data(val), std::size(val));
        }
        else if (tr_variantIsDict(child))
        {
            tr_variantMergeDicts(tr_variantListAddDict(target, 0), child);
        }
        else if (tr_variantIsList(child))
        {
            tr_variantListCopy(tr_variantListAddList(target, 0), child);
        }
        else
        {
            tr_logAddWarn("tr_variantListCopy skipping item");
        }
    }
}

constexpr size_t tr_variantDictSize(tr_variant const* dict)
{
    return tr_variantIsDict(dict) ? dict->val.l.count : 0;
}
} // namespace merge_helpers
} // namespace

void tr_variantMergeDicts(tr_variant* target, tr_variant const* source)
{
    using namespace merge_helpers;

    TR_ASSERT(tr_variantIsDict(target));
    TR_ASSERT(tr_variantIsDict(source));

    size_t const source_count = tr_variantDictSize(source);

    tr_variantDictReserve(target, source_count + tr_variantDictSize(target));

    for (size_t i = 0; i < source_count; ++i)
    {
        auto key = tr_quark{};
        tr_variant* child = nullptr;
        if (tr_variantDictChild(const_cast<tr_variant*>(source), i, &key, &child))
        {
            tr_variant* t = nullptr;

            // if types differ, ensure that target will overwrite source
            auto const* const target_child = tr_variantDictFind(target, key);
            if ((target_child != nullptr) && !tr_variantIsType(target_child, child->type))
            {
                tr_variantDictRemove(target, key);
            }

            if (tr_variantIsBool(child))
            {
                auto val = bool{};
                tr_variantGetBool(child, &val);
                tr_variantDictAddBool(target, key, val);
            }
            else if (tr_variantIsReal(child))
            {
                auto val = double{};
                tr_variantGetReal(child, &val);
                tr_variantDictAddReal(target, key, val);
            }
            else if (tr_variantIsInt(child))
            {
                auto val = int64_t{};
                tr_variantGetInt(child, &val);
                tr_variantDictAddInt(target, key, val);
            }
            else if (tr_variantIsString(child))
            {
                auto val = std::string_view{};
                (void)tr_variantGetStrView(child, &val);
                tr_variantDictAddRaw(target, key, std::data(val), std::size(val));
            }
            else if (tr_variantIsDict(child) && tr_variantDictFindDict(target, key, &t))
            {
                tr_variantMergeDicts(t, child);
            }
            else if (tr_variantIsList(child))
            {
                if (tr_variantDictFind(target, key) == nullptr)
                {
                    tr_variantListCopy(tr_variantDictAddList(target, key, tr_variantListSize(child)), child);
                }
            }
            else if (tr_variantIsDict(child))
            {
                tr_variant* target_dict = tr_variantDictFind(target, key);

                if (target_dict == nullptr)
                {
                    target_dict = tr_variantDictAddDict(target, key, tr_variantDictSize(child));
                }

                if (tr_variantIsDict(target_dict))
                {
                    tr_variantMergeDicts(target_dict, child);
                }
            }
            else
            {
                tr_logAddDebug(fmt::format("tr_variantMergeDicts skipping '{}'", tr_quark_get_string_view(key)));
            }
        }
    }
}

// ---

std::string tr_variantToStr(tr_variant const* v, tr_variant_fmt fmt)
{
    switch (fmt)
    {
    case TR_VARIANT_FMT_JSON:
        return tr_variantToStrJson(v, false);

    case TR_VARIANT_FMT_JSON_LEAN:
        return tr_variantToStrJson(v, true);

    default: // TR_VARIANT_FMT_BENC:
        return tr_variantToStrBenc(v);
    }
}

int tr_variantToFile(tr_variant const* v, tr_variant_fmt fmt, std::string_view filename)
{
    auto error_code = int{ 0 };
    auto const contents = tr_variantToStr(v, fmt);

    tr_error* error = nullptr;
    tr_saveFile(filename, contents, &error);
    if (error != nullptr)
    {
        tr_logAddError(fmt::format(
            _("Couldn't save '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        error_code = error->code;
        tr_error_clear(&error);
    }

    return error_code;
}

// ---

bool tr_variantFromBuf(tr_variant* setme, int opts, std::string_view buf, char const** setme_end, tr_error** error)
{
    // supported formats: benc, json
    TR_ASSERT((opts & (TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_JSON)) != 0);

    *setme = {};

    auto const success = ((opts & TR_VARIANT_PARSE_BENC) != 0) ? tr_variantParseBenc(*setme, opts, buf, setme_end, error) :
                                                                 tr_variantParseJson(*setme, opts, buf, setme_end, error);

    if (!success)
    {
        tr_variantClear(setme);
    }

    return success;
}

bool tr_variantFromFile(tr_variant* setme, tr_variant_parse_opts opts, std::string_view filename, tr_error** error)
{
    // can't do inplace when this function is allocating & freeing the memory...
    TR_ASSERT((opts & TR_VARIANT_PARSE_INPLACE) == 0);

    if (auto buf = std::vector<char>{}; tr_loadFile(filename, buf, error))
    {
        return tr_variantFromBuf(setme, opts, buf, nullptr, error);
    }

    return false;
}
