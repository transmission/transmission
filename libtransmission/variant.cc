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
constexpr bool variant_is_container(tr_variant const* const var)
{
    return tr_variantIsList(var) || tr_variantIsDict(var);
}

// ---

constexpr std::optional<size_t> dict_index_of(tr_variant const* const var, tr_quark const key)
{
    if (!tr_variantIsDict(var))
    {
        return {};
    }

    for (size_t idx = 0; idx < var->val.l.count; ++idx)
    {
        if (var->val.l.vals[idx].key == key)
        {
            return idx;
        }
    }

    return {};
}

bool dictFindType(tr_variant* const var, tr_quark key, tr_variant::Type type, tr_variant** setme)
{
    *setme = tr_variantDictFind(var, key);
    return tr_variantIsType(*setme, type);
}

tr_variant* containerReserve(tr_variant* var, size_t count)
{
    TR_ASSERT(variant_is_container(var));
    auto& container = var->val.l;

    if (size_t const needed = container.count + count; needed > container.alloc)
    {
        // scale the alloc size in powers-of-2
        auto n = container.alloc != 0 ? container.alloc : 8U;
        while (n < needed)
        {
            n *= 2U;
        }

        auto* vals = new tr_variant[n];
        std::move(container.vals, container.vals + container.count, vals);
        delete[] container.vals;
        container.vals = vals;
        container.alloc = n;
    }

    return container.vals + container.count;
}

bool variant_remove_child(tr_variant* const var, size_t idx)
{
    if (!variant_is_container(var))
    {
        return false;
    }

    auto& container = var->val.l;
    if (idx >= container.count)
    {
        return false;
    }

    std::move(container.vals + idx + 1, container.vals + container.count, container.vals + idx);
    --container.count;
    // container.vals[container.count--] = {};
    return true;
}

} // namespace

tr_variant::~tr_variant()
{
    if (type == Type::Vector || type == Type::Map)
    {
        delete[] val.l.vals;
        val.l.vals = nullptr;
        val.l.count = {};
    }
    else if (type == Type::String)
    {
        val.s.clear();
    }

    type = Type::None;
}

tr_variant* tr_variantDictFind(tr_variant* const var, tr_quark key)
{
    auto const idx = dict_index_of(var, key);
    return idx.has_value() ? var->val.l.vals + *idx : nullptr;
}

tr_variant* tr_variantListChild(tr_variant* const var, size_t pos)
{
    if (tr_variantIsList(var) && pos < var->val.l.count)
    {
        return var->val.l.vals + pos;
    }

    return {};
}

bool tr_variantListRemove(tr_variant* const var, size_t pos)
{
    return variant_remove_child(var, pos);
}

bool tr_variantGetInt(tr_variant const* const var, int64_t* setme)
{
    if (var == nullptr)
    {
        return false;
    }

    if (tr_variantIsInt(var))
    {
        if (setme != nullptr)
        {
            *setme = var->val.i;
        }

        return true;
    }

    if (tr_variantIsBool(var))
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
    if (tr_variantIsString(var))
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

    if (tr_variantIsBool(var))
    {
        *setme = var->val.b;
        return true;
    }

    if (tr_variantIsInt(var) && (var->val.i == 0 || var->val.i == 1))
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

    if (tr_variantIsReal(var))
    {
        *setme = var->val.d;
        return true;
    }

    if (tr_variantIsInt(var))
    {
        *setme = static_cast<double>(var->val.i);
        return true;
    }

    if (tr_variantIsString(var))
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
    TR_ASSERT(tr_variantIsList(var));

    containerReserve(var, count);
}

void tr_variantInitDict(tr_variant* initme, size_t reserve_count)
{
    tr_variantInit(initme, tr_variant::Type::Map);
    tr_variantDictReserve(initme, reserve_count);
}

void tr_variantDictReserve(tr_variant* const var, size_t reserve_count)
{
    TR_ASSERT(tr_variantIsDict(var));

    containerReserve(var, reserve_count);
}

tr_variant* tr_variantListAdd(tr_variant* const var)
{
    TR_ASSERT(tr_variantIsList(var));

    auto* const child = containerReserve(var, 1);
    ++var->val.l.count;
    *child = tr_variant{};

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
    TR_ASSERT(tr_variantIsDict(var));

    auto* const child = containerReserve(var, 1);
    ++var->val.l.count;
    *child = tr_variant{};
    child->key = key;

    return child;
}

tr_variant* tr_variantDictAddInt(tr_variant* const var, tr_quark key, int64_t val)
{
    tr_variantDictRemove(var, key);
    auto* const child = tr_variantDictAdd(var, key);
    tr_variantInitInt(child, val);
    return child;
}

tr_variant* tr_variantDictAddBool(tr_variant* const var, tr_quark key, bool val)
{
    tr_variantDictRemove(var, key);
    auto* const child = tr_variantDictAdd(var, key);
    tr_variantInitBool(child, val);
    return child;
}

tr_variant* tr_variantDictAddReal(tr_variant* const var, tr_quark key, double val)
{
    tr_variantDictRemove(var, key);
    auto* const child = tr_variantDictAdd(var, key);
    tr_variantInitReal(child, val);
    return child;
}

tr_variant* tr_variantDictAddQuark(tr_variant* const var, tr_quark key, tr_quark const val)
{
    tr_variantDictRemove(var, key);
    auto* const child = tr_variantDictAdd(var, key);
    tr_variantInitQuark(child, val);
    return child;
}

tr_variant* tr_variantDictAddStr(tr_variant* const var, tr_quark key, std::string_view val)
{
    tr_variantDictRemove(var, key);
    auto* const child = tr_variantDictAdd(var, key);
    tr_variantInitStr(child, val);
    return child;
}

tr_variant* tr_variantDictAddStrView(tr_variant* const var, tr_quark key, std::string_view val)
{
    tr_variantDictRemove(var, key);
    auto* const child = tr_variantDictAdd(var, key);
    tr_variantInitStrView(child, val);
    return child;
}

tr_variant* tr_variantDictAddRaw(tr_variant* const var, tr_quark key, void const* value, size_t len)
{
    tr_variantDictRemove(var, key);
    auto* const child = tr_variantDictAdd(var, key);
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

bool tr_variantDictRemove(tr_variant* const var, tr_quark key)
{
    auto const idx = dict_index_of(var, key);
    return idx.has_value() && variant_remove_child(var, *idx);
}

// --- BENC WALKING

class WalkNode
{
public:
    WalkNode() = default;

    explicit WalkNode(tr_variant const* const var)
        : var_{ var }
    {
    }

    tr_variant const* next_child()
    {
        if (!variant_is_container(var_) || (child_index_ >= var_->val.l.count))
        {
            return nullptr;
        }

        auto idx = child_index_++;
        if (!sorted.empty())
        {
            idx = sorted[idx];
        }

        return var_->val.l.vals + idx;
    }

    [[nodiscard]] constexpr auto is_visited() const noexcept
    {
        return is_visited_;
    }

    constexpr void set_visited() noexcept
    {
        is_visited_ = true;
    }

    [[nodiscard]] tr_variant const* current() const noexcept
    {
        return var_;
    }

protected:
    friend class VariantWalker;

    tr_variant const* var_ = nullptr;

    bool is_visited_ = false;

    void assign(tr_variant const* v_in)
    {
        var_ = v_in;
        is_visited_ = false;
        child_index_ = 0;
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
        if (!tr_variantIsDict(var_))
        {
            return;
        }

        auto const n = var_->val.l.count;
        auto const* children = var_->val.l.vals;

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
    size_t child_index_ = 0;

    // When `v` is a dict, this is its children's indices sorted by key.
    // Bencoded dicts must be sorted, so this is useful when writing benc.
    small::vector<size_t, 128U> sorted;
};

class VariantWalker
{
public:
    void emplace(tr_variant const* v_in, bool sort_dicts)
    {
        stack_.emplace_back(v_in);

        if (sort_dicts)
        {
            top().sort(sortbuf_);
        }
    }

    void pop()
    {
        TR_ASSERT(!std::empty(stack_));
        stack_.resize(std::size(stack_) - 1U);
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return std::empty(stack_);
    }

    WalkNode& top()
    {
        TR_ASSERT(!std::empty(stack_));
        return stack_.back();
    }

private:
    static auto constexpr InitialCapacity = size_t{ 24U };
    small::vector<WalkNode, InitialCapacity> stack_;
    small::vector<WalkNode::ByKey, InitialCapacity> sortbuf_;
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

        if (!node.is_visited())
        {
            v = node.current();
            node.set_visited();
        }
        else
        {
            v = node.next_child();

            if (v != nullptr)
            {
                if (tr_variantIsDict(node.current()))
                {
                    auto const keystr = tr_quark_get_string_view(v->key);
                    auto tmp = tr_variant{};
                    tr_variantInitQuark(&tmp, v->key);
                    walk_funcs.string_func(tmp, keystr, user_data);
                }
            }
            else // finished with this node
            {
                if (variant_is_container(node.current()))
                {
                    walk_funcs.container_end_func(*node.current(), user_data);
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
                if (v == node.current())
                {
                    walk_funcs.list_begin_func(*v, user_data);
                }
                else
                {
                    stack.emplace(v, sort_dicts);
                }
                break;

            case tr_variant::Type::Map:
                if (v == node.current())
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

bool tr_variantDictChild(tr_variant* const var, size_t pos, tr_quark* key, tr_variant** setme_value)
{
    TR_ASSERT(tr_variantIsDict(var));

    bool success = false;

    if (tr_variantIsDict(var) && pos < var->val.l.count)
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

constexpr size_t tr_variantDictSize(tr_variant const* const var)
{
    return tr_variantIsDict(var) ? var->val.l.count : 0U;
}
} // namespace merge_helpers
} // namespace

void tr_variantMergeDicts(tr_variant* const tgt, tr_variant const* const src)
{
    using namespace merge_helpers;

    TR_ASSERT(tr_variantIsDict(tgt));
    TR_ASSERT(tr_variantIsDict(src));

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

            if (tr_variantIsBool(child))
            {
                auto val = bool{};
                tr_variantGetBool(child, &val);
                tr_variantDictAddBool(tgt, key, val);
            }
            else if (tr_variantIsReal(child))
            {
                auto val = double{};
                tr_variantGetReal(child, &val);
                tr_variantDictAddReal(tgt, key, val);
            }
            else if (tr_variantIsInt(child))
            {
                auto val = int64_t{};
                tr_variantGetInt(child, &val);
                tr_variantDictAddInt(tgt, key, val);
            }
            else if (tr_variantIsString(child))
            {
                auto val = std::string_view{};
                (void)tr_variantGetStrView(child, &val);
                tr_variantDictAddRaw(tgt, key, std::data(val), std::size(val));
            }
            else if (tr_variantIsDict(child) && tr_variantDictFindDict(tgt, key, &t))
            {
                tr_variantMergeDicts(t, child);
            }
            else if (tr_variantIsList(child))
            {
                if (tr_variantDictFind(tgt, key) == nullptr)
                {
                    tr_variantListCopy(tr_variantDictAddList(tgt, key, tr_variantListSize(child)), child);
                }
            }
            else if (tr_variantIsDict(child))
            {
                tr_variant* target_dict = tr_variantDictFind(tgt, key);

                if (target_dict == nullptr)
                {
                    target_dict = tr_variantDictAddDict(tgt, key, tr_variantDictSize(child));
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
