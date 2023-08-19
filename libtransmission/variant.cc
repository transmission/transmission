// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::sort
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <share.h>
#endif

#include <fmt/core.h>

#include <small/vector.hpp>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

using namespace std::literals;

namespace
{
constexpr bool tr_variantIsContainer(tr_variant const* const var)
{
    return var != nullptr && (var->holds_alternative<tr_variant::Vector>() || var->holds_alternative<tr_variant::Map>());
}

// ---

constexpr int dictIndexOf(tr_variant const* const var, tr_quark key)
{
    if (var != nullptr && var->holds_alternative<tr_variant::Map>())
    {
        for (size_t i = 0; i < var->val.l.count; ++i)
        {
            if (var->val.l.vals[i].key == key)
            {
                return (int)i;
            }
        }
    }

    return -1;
}

bool dictFindType(tr_variant* const var, tr_quark key, tr_variant::Type type, tr_variant** setme)
{
    *setme = tr_variantDictFind(var, key);
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

tr_variant* dictFindOrAdd(tr_variant* const var, tr_quark key, tr_variant::Type type)
{
    /* see if it already exists, and if so, try to reuse it */
    tr_variant* child = tr_variantDictFind(var, key);
    if (child != nullptr)
    {
        if (!tr_variantIsType(child, type))
        {
            tr_variantDictRemove(var, key);
            child = nullptr;
        }
        else if (child->holds_alternative<std::string_view>())
        {
            child->val.s.clear();
        }
    }

    /* if it doesn't exist, create it */
    if (child == nullptr)
    {
        child = tr_variantDictAdd(var, key);
    }

    return child;
}

} // namespace

tr_variant* tr_variantDictFind(tr_variant* const var, tr_quark key)
{
    auto const i = dictIndexOf(var, key);

    return i < 0 ? nullptr : var->val.l.vals + i;
}

tr_variant* tr_variantListChild(tr_variant* const var, size_t pos)
{
    if (var != nullptr && var->holds_alternative<tr_variant::Vector>() && pos < var->val.l.count)
    {
        return var->val.l.vals + pos;
    }

    return {};
}

bool tr_variantListRemove(tr_variant* const var, size_t pos)
{
    if (var == nullptr || !var->holds_alternative<tr_variant::Vector>() || pos >= var->val.l.count)
    {
        return false;
    }

    auto& vals = var->val.l.vals;
    auto& count = var->val.l.count;

    tr_variantClear(&vals[pos]);
    std::move(vals + pos + 1, vals + count, vals + pos);
    --count;
    vals[count] = {};

    return true;
}

bool tr_variantGetInt(tr_variant const* const var, int64_t* setme)
{
    if (var == nullptr)
    {
        return false;
    }

    if (var->holds_alternative<int64_t>())
    {
        if (setme != nullptr)
        {
            *setme = var->val.i;
        }

        return true;
    }

    if (var->holds_alternative<bool>())
    {
        if (setme != nullptr)
        {
            *setme = var->val.b ? 1 : 0;
        }

        return true;
    }

    return false;
}

bool tr_variantGetStrView(tr_variant const* const var, std::string_view* setme)
{
    if (var != nullptr && var->holds_alternative<std::string_view>())
    {
        *setme = var->val.s.get();
        return true;
    }

    return false;
}

bool tr_variantGetRaw(tr_variant const* v, std::byte const** setme_raw, size_t* setme_len)
{
    if (auto sv = std::string_view{}; tr_variantGetStrView(v, &sv))
    {
        *setme_raw = reinterpret_cast<std::byte const*>(std::data(sv));
        *setme_len = std::size(sv);
        return true;
    }

    return false;
}

bool tr_variantGetRaw(tr_variant const* v, uint8_t const** setme_raw, size_t* setme_len)
{
    if (auto sv = std::string_view{}; tr_variantGetStrView(v, &sv))
    {
        *setme_raw = reinterpret_cast<uint8_t const*>(std::data(sv));
        *setme_len = std::size(sv);
        return true;
    }

    return false;
}

bool tr_variantGetBool(tr_variant const* const var, bool* setme)
{
    if (var == nullptr)
    {
        return false;
    }

    if (var->holds_alternative<bool>())
    {
        *setme = var->val.b;
        return true;
    }

    if (var->holds_alternative<int64_t>() && (var->val.i == 0 || var->val.i == 1))
    {
        *setme = var->val.i != 0;
        return true;
    }

    if (auto sv = std::string_view{}; tr_variantGetStrView(var, &sv))
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

bool tr_variantGetReal(tr_variant const* const var, double* setme)
{
    if (var == nullptr)
    {
        return false;
    }

    if (var->holds_alternative<double>())
    {
        *setme = var->val.d;
        return true;
    }

    if (var->holds_alternative<int64_t>())
    {
        *setme = static_cast<double>(var->val.i);
        return true;
    }

    if (var->holds_alternative<std::string_view>())
    {
        if (auto val = tr_num_parse<double>(var->val.s.get()); val)
        {
            *setme = *val;
            return true;
        }
    }

    return false;
}

bool tr_variantDictFindInt(tr_variant* const var, tr_quark key, int64_t* setme)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetInt(child, setme);
}

bool tr_variantDictFindBool(tr_variant* const var, tr_quark key, bool* setme)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetBool(child, setme);
}

bool tr_variantDictFindReal(tr_variant* const var, tr_quark key, double* setme)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetReal(child, setme);
}

bool tr_variantDictFindStrView(tr_variant* const var, tr_quark key, std::string_view* setme)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetStrView(child, setme);
}

bool tr_variantDictFindList(tr_variant* const var, tr_quark key, tr_variant** setme)
{
    return dictFindType(var, key, tr_variant::Type::Vector, setme);
}

bool tr_variantDictFindDict(tr_variant* const var, tr_quark key, tr_variant** setme)
{
    return dictFindType(var, key, tr_variant::Type::Map, setme);
}

bool tr_variantDictFindRaw(tr_variant* const var, tr_quark key, uint8_t const** setme_raw, size_t* setme_len)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetRaw(child, setme_raw, setme_len);
}

bool tr_variantDictFindRaw(tr_variant* const var, tr_quark key, std::byte const** setme_raw, size_t* setme_len)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetRaw(child, setme_raw, setme_len);
}

// ---

void tr_variantInitStrView(tr_variant* initme, std::string_view val)
{
    tr_variantInit(initme, tr_variant::Type::String);
    initme->val.s.set_shallow(val);
}

void tr_variantInitRaw(tr_variant* initme, void const* value, size_t value_len)
{
    tr_variantInitStr(initme, std::string_view{ static_cast<char const*>(value), value_len });
}

void tr_variantInitQuark(tr_variant* initme, tr_quark value)
{
    tr_variantInitStrView(initme, tr_quark_get_string_view(value));
}

void tr_variantInitStr(tr_variant* initme, std::string_view value)
{
    tr_variantInit(initme, tr_variant::Type::String);
    initme->val.s.set(value);
}

void tr_variantInitList(tr_variant* initme, size_t reserve_count)
{
    tr_variantInit(initme, tr_variant::Type::Vector);
    tr_variantListReserve(initme, reserve_count);
}

void tr_variantListReserve(tr_variant* const var, size_t count)
{
    TR_ASSERT(var != nullptr && var->holds_alternative<tr_variant::Vector>());

    containerReserve(var, count);
}

void tr_variantInitDict(tr_variant* initme, size_t reserve_count)
{
    tr_variantInit(initme, tr_variant::Type::Map);
    tr_variantDictReserve(initme, reserve_count);
}

void tr_variantDictReserve(tr_variant* const var, size_t reserve_count)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Map>());

    containerReserve(var, reserve_count);
}

tr_variant* tr_variantListAdd(tr_variant* const var)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Vector>());

    auto* const child = containerReserve(var, 1);
    ++var->val.l.count;
    child->key = 0;
    tr_variantInit(child, tr_variant::Type::Int);

    return child;
}

tr_variant* tr_variantListAddInt(tr_variant* const var, int64_t value)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitInt(child, value);
    return child;
}

tr_variant* tr_variantListAddReal(tr_variant* const var, double value)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitReal(child, value);
    return child;
}

tr_variant* tr_variantListAddBool(tr_variant* const var, bool value)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitBool(child, value);
    return child;
}

tr_variant* tr_variantListAddStr(tr_variant* const var, std::string_view value)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitStr(child, value);
    return child;
}

tr_variant* tr_variantListAddStrView(tr_variant* const var, std::string_view value)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitStrView(child, value);
    return child;
}

tr_variant* tr_variantListAddQuark(tr_variant* const var, tr_quark value)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitQuark(child, value);
    return child;
}

tr_variant* tr_variantListAddRaw(tr_variant* const var, void const* value, size_t value_len)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitRaw(child, value, value_len);
    return child;
}

tr_variant* tr_variantListAddList(tr_variant* const var, size_t reserve_count)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitList(child, reserve_count);
    return child;
}

tr_variant* tr_variantListAddDict(tr_variant* const var, size_t reserve_count)
{
    auto* const child = tr_variantListAdd(var);
    tr_variantInitDict(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictAdd(tr_variant* const var, tr_quark key)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Map>());

    auto* const val = containerReserve(var, 1);
    ++var->val.l.count;
    val->key = key;
    tr_variantInit(val, tr_variant::Type::Int);

    return val;
}

tr_variant* tr_variantDictAddInt(tr_variant* const var, tr_quark key, int64_t val)
{
    auto* const child = dictFindOrAdd(var, key, tr_variant::Type::Int);
    tr_variantInitInt(child, val);
    return child;
}

tr_variant* tr_variantDictAddBool(tr_variant* const var, tr_quark key, bool val)
{
    auto* const child = dictFindOrAdd(var, key, tr_variant::Type::Bool);
    tr_variantInitBool(child, val);
    return child;
}

tr_variant* tr_variantDictAddReal(tr_variant* const var, tr_quark key, double val)
{
    auto* const child = dictFindOrAdd(var, key, tr_variant::Type::Double);
    tr_variantInitReal(child, val);
    return child;
}

tr_variant* tr_variantDictAddQuark(tr_variant* const var, tr_quark key, tr_quark const val)
{
    auto* const child = dictFindOrAdd(var, key, tr_variant::Type::String);
    tr_variantInitQuark(child, val);
    return child;
}

tr_variant* tr_variantDictAddStr(tr_variant* const var, tr_quark key, std::string_view val)
{
    auto* const child = dictFindOrAdd(var, key, tr_variant::Type::String);
    tr_variantInitStr(child, val);
    return child;
}

tr_variant* tr_variantDictAddStrView(tr_variant* const var, tr_quark key, std::string_view val)
{
    auto* const child = dictFindOrAdd(var, key, tr_variant::Type::String);
    tr_variantInitStrView(child, val);
    return child;
}

tr_variant* tr_variantDictAddRaw(tr_variant* const var, tr_quark key, void const* value, size_t len)
{
    auto* const child = dictFindOrAdd(var, key, tr_variant::Type::String);
    tr_variantInitRaw(child, value, len);
    return child;
}

tr_variant* tr_variantDictAddList(tr_variant* const var, tr_quark key, size_t reserve_count)
{
    auto* const child = tr_variantDictAdd(var, key);
    tr_variantInitList(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictAddDict(tr_variant* const var, tr_quark key, size_t reserve_count)
{
    auto* const child = tr_variantDictAdd(var, key);
    tr_variantInitDict(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictSteal(tr_variant* const var, tr_quark key, tr_variant* value)
{
    tr_variant* const child = tr_variantDictAdd(var, key);
    *child = *value;
    child->key = key;
    tr_variantInit(value, value->type);
    return child;
}

bool tr_variantDictRemove(tr_variant* const var, tr_quark key)
{
    bool removed = false;

    if (int const i = dictIndexOf(var, key); i >= 0)
    {
        int const last = (int)var->val.l.count - 1;

        tr_variantClear(&var->val.l.vals[i]);

        if (i != last)
        {
            var->val.l.vals[i] = var->val.l.vals[last];
        }

        --var->val.l.count;

        removed = true;
    }

    return removed;
}

// --- BENC WALKING

class WalkNode
{
public:
    WalkNode() = default;

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

    template<typename Container>
    void sort(Container& sortbuf)
    {
        if (!v.holds_alternative<tr_variant::Map>())
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
    small::vector<size_t, 128U> sorted;
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

    static auto constexpr InitialCapacity = size_t{ 24U };
    small::vector<WalkNode, InitialCapacity> stack;
    small::vector<WalkNode::ByKey, InitialCapacity> sortbuf;
};

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted data. (#667)
 */
void tr_variant_serde::walk(tr_variant const& top, WalkFuncs const& walk_funcs, void* user_data, bool sort_dicts)
{
    auto stack = VariantWalker{};
    stack.emplace(&top, sort_dicts);

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
                if (node.v.holds_alternative<tr_variant::Map>())
                {
                    auto const keystr = tr_quark_get_string_view(v->key);
                    auto tmp = tr_variant{};
                    tr_variantInitQuark(&tmp, v->key);
                    walk_funcs.string_func(tmp, keystr, user_data);
                }
            }
            else // finished with this node
            {
                if (tr_variantIsContainer(&node.v))
                {
                    walk_funcs.container_end_func(node.v, user_data);
                }

                stack.pop();
                continue;
            }
        }

        if (v != nullptr)
        {
            switch (v->type)
            {
            case tr_variant::Type::Int:
                walk_funcs.int_func(*v, v->val.i, user_data);
                break;

            case tr_variant::Type::Bool:
                walk_funcs.bool_func(*v, v->val.b, user_data);
                break;

            case tr_variant::Type::Double:
                walk_funcs.double_func(*v, v->val.d, user_data);
                break;

            case tr_variant::Type::String:
                walk_funcs.string_func(*v, v->val.s.get(), user_data);
                break;

            case tr_variant::Type::Vector:
                if (v == &node.v)
                {
                    walk_funcs.list_begin_func(*v, user_data);
                }
                else
                {
                    stack.emplace(v, sort_dicts);
                }
                break;

            case tr_variant::Type::Map:
                if (v == &node.v)
                {
                    walk_funcs.dict_begin_func(*v, user_data);
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

void tr_variantClear(tr_variant* const clearme)
{
    // clang-format off
    constexpr tr_variant_serde::WalkFuncs CleanupFuncs = {
        [](tr_variant const&, int64_t, void*) {},
        [](tr_variant const&, bool, void*) {},
        [](tr_variant const&, double, void*) {},
        [](tr_variant const& var, std::string_view, void*){ const_cast<tr_variant&>(var).val.s.clear(); },
        [](tr_variant const&, void*) {},
        [](tr_variant const&, void*) {},
        [](tr_variant const& var, void*) { delete[] var.val.l.vals; }
    };
    // clang-format on

    if (clearme != nullptr && clearme->has_value())
    {
        tr_variant_serde::walk(*clearme, CleanupFuncs, nullptr, false);

        *clearme = {};
    }
}

// ---

bool tr_variantDictChild(tr_variant* const var, size_t pos, tr_quark* key, tr_variant** setme_value)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Map>());

    bool success = false;

    if (var != nullptr && var->holds_alternative<tr_variant::Map>() && pos < var->val.l.count)
    {
        *key = var->val.l.vals[pos].key;
        *setme_value = var->val.l.vals + pos;
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

        if (child->holds_alternative<bool>())
        {
            auto val = bool{};
            tr_variantGetBool(child, &val);
            tr_variantListAddBool(target, val);
        }
        else if (child->holds_alternative<double>())
        {
            auto val = double{};
            tr_variantGetReal(child, &val);
            tr_variantListAddReal(target, val);
        }
        else if (child->holds_alternative<int64_t>())
        {
            auto val = int64_t{};
            tr_variantGetInt(child, &val);
            tr_variantListAddInt(target, val);
        }
        else if (child->holds_alternative<std::string_view>())
        {
            auto val = std::string_view{};
            (void)tr_variantGetStrView(child, &val);
            tr_variantListAddRaw(target, std::data(val), std::size(val));
        }
        else if (child->holds_alternative<tr_variant::Map>())
        {
            tr_variantMergeDicts(tr_variantListAddDict(target, 0), child);
        }
        else if (child->holds_alternative<tr_variant::Vector>())
        {
            tr_variantListCopy(tr_variantListAddList(target, 0), child);
        }
        else
        {
            tr_logAddWarn("tr_variantListCopy skipping item");
        }
    }
}

constexpr size_t tr_variantDictSize(tr_variant const* const var)
{
    return var != nullptr && var->holds_alternative<tr_variant::Map>() ? var->val.l.count : 0;
}
} // namespace merge_helpers
} // namespace

void tr_variantMergeDicts(tr_variant* const tgt, tr_variant const* const src)
{
    using namespace merge_helpers;

    TR_ASSERT(tgt != nullptr);
    TR_ASSERT(tgt->holds_alternative<tr_variant::Map>());
    TR_ASSERT(src != nullptr);
    TR_ASSERT(src->holds_alternative<tr_variant::Map>());

    size_t const source_count = tr_variantDictSize(src);

    tr_variantDictReserve(tgt, source_count + tr_variantDictSize(tgt));

    for (size_t i = 0; i < source_count; ++i)
    {
        auto key = tr_quark{};
        tr_variant* child = nullptr;
        if (tr_variantDictChild(const_cast<tr_variant*>(src), i, &key, &child))
        {
            tr_variant* t = nullptr;

            // if types differ, ensure that target will overwrite source
            auto const* const target_child = tr_variantDictFind(tgt, key);
            if ((target_child != nullptr) && !tr_variantIsType(target_child, child->type))
            {
                tr_variantDictRemove(tgt, key);
            }

            if (child->holds_alternative<bool>())
            {
                auto val = bool{};
                tr_variantGetBool(child, &val);
                tr_variantDictAddBool(tgt, key, val);
            }
            else if (child->holds_alternative<double>())
            {
                auto val = double{};
                tr_variantGetReal(child, &val);
                tr_variantDictAddReal(tgt, key, val);
            }
            else if (child->holds_alternative<int64_t>())
            {
                auto val = int64_t{};
                tr_variantGetInt(child, &val);
                tr_variantDictAddInt(tgt, key, val);
            }
            else if (child->holds_alternative<std::string_view>())
            {
                auto val = std::string_view{};
                (void)tr_variantGetStrView(child, &val);
                tr_variantDictAddRaw(tgt, key, std::data(val), std::size(val));
            }
            else if (child->holds_alternative<tr_variant::Map>() && tr_variantDictFindDict(tgt, key, &t))
            {
                tr_variantMergeDicts(t, child);
            }
            else if (child->holds_alternative<tr_variant::Vector>())
            {
                if (tr_variantDictFind(tgt, key) == nullptr)
                {
                    tr_variantListCopy(tr_variantDictAddList(tgt, key, tr_variantListSize(child)), child);
                }
            }
            else if (child->holds_alternative<tr_variant::Map>())
            {
                tr_variant* target_dict = tr_variantDictFind(tgt, key);

                if (target_dict == nullptr)
                {
                    target_dict = tr_variantDictAddDict(tgt, key, tr_variantDictSize(child));
                }

                if (target_dict->holds_alternative<tr_variant::Map>())
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

tr_variant_serde::~tr_variant_serde()
{
    tr_error_clear(&error_);
}

std::optional<tr_variant> tr_variant_serde::parse(std::string_view input)
{
    tr_error_clear(&error_);
    return type_ == Type::Json ? parse_json(input) : parse_benc(input);
}

[[nodiscard]] std::optional<tr_variant> tr_variant_serde::parse_file(std::string_view filename)
{
    TR_ASSERT_MSG(!parse_inplace_, "not supported in from_file()");
    parse_inplace_ = false;

    if (auto buf = std::vector<char>{}; tr_file_read(filename, buf, &error_))
    {
        return parse(buf);
    }

    return {};
}

std::string tr_variant_serde::to_string(tr_variant const& var) const
{
    return type_ == Type::Json ? to_json_string(var) : to_benc_string(var);
}

bool tr_variant_serde::to_file(tr_variant const& var, std::string_view filename)
{
    tr_file_save(filename, to_string(var), &error_);

    if (error_ != nullptr)
    {
        tr_logAddError(fmt::format(
            _("Couldn't save '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", error_->message),
            fmt::arg("error_code", error_->code)));
        return false;
    }

    return true;
}
