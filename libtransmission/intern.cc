// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "libtransmission/intern.h"

namespace transmission::intern
{
struct Interner::Impl
{
public:
    [[nodiscard]] std::optional<Interned> lookup(std::string_view const str) const noexcept
    {
        if (auto const found = lookup_known(str))
        {
            return found;
        }

        if (auto const iter = runtime_.find(str); iter != std::end(runtime_))
        {
            return Interned{ iter->first, iter->second };
        }

        return {};
    }

    [[nodiscard]] Interned add(std::string_view const str)
    {
        if (auto const found = lookup(str))
        {
            return *found;
        }

        if (next_runtime_ > detail::PayloadMask)
        {
            throw std::overflow_error("intern::Interner exhausted runtime id space");
        }

        auto const view = store(str);
        auto const id = runtime_id(next_runtime_++);
        runtime_.emplace(view, id);
        return Interned{ view, id };
    }

    void add_known(Interned const* const items, size_t const n_items)
    {
        auto& v = known_;
        v.insert(std::end(v), items, items + n_items);
        std::sort(std::begin(v), std::end(v), InternedLt{});

        assert(std::none_of(std::begin(v), std::end(v), IsRuntime{}));
        assert(std::adjacent_find(std::begin(v), std::end(v), InternedEqId{}) == std::end(v));
    }

private:
    struct SvHash
    {
        [[nodiscard]] std::size_t operator()(std::string_view const str) const noexcept
        {
            return static_cast<std::size_t>(detail::fnv1a_32(str));
        }
    };

    struct InternedLt
    {
        [[nodiscard]] bool operator()(Interned const& a, Interned const& b) const noexcept
        {
            return a.id() < b.id();
        }
    };

    struct InternedEqId
    {
        [[nodiscard]] bool operator()(Interned const& a, Interned const& b) const noexcept
        {
            return a.id() == b.id();
        }
    };

    struct IsRuntime
    {
        [[nodiscard]] bool operator()(Interned const& str) const noexcept
        {
            return (str.id() & detail::RuntimeTag) != 0;
        }
    };

    [[nodiscard]] static constexpr uint32_t runtime_id(uint32_t const counter) noexcept
    {
        return detail::RuntimeTag | (counter & detail::PayloadMask); // top bit 1
    }

    [[nodiscard]] std::string_view store(std::string_view const str)
    {
        strings_.emplace_back(str); // may allocate/throw
        return std::string_view{ strings_.back() };
    }

    [[nodiscard]] std::optional<Interned> lookup_known(std::string_view const name) const noexcept
    {
        auto const& v = known_;
        auto const tmp = Interned{ name, detail::known_key(name) };
        auto const iter = std::lower_bound(std::begin(v), std::end(v), tmp, InternedLt{});
        if (iter != std::end(v) && iter->id() == tmp.id() && iter->view() == tmp.view())
        {
            return *iter;
        }

        return {};
    }

    // Set at startup with `Interner::addTable()`
    using KnownVec = std::vector<Interned>;
    KnownVec known_;

    // Runtime
    std::deque<std::string> strings_;
    std::unordered_map<std::string_view, std::uint32_t, SvHash> runtime_;
    uint32_t next_runtime_ = 1;
};

Interner& Interner::instance()
{
    static Interner singleton;
    return singleton;
}

Interner::Interner()
    : pimpl_{ std::make_unique<Impl>() }
{
}

void Interner::add_known(Interned const* const entries, std::size_t const n_entries)
{
    pimpl_->add_known(entries, n_entries);
}

std::optional<Interned> Interner::lookup(std::string_view const str) const noexcept
{
    return pimpl_->lookup(str);
}

Interned Interner::add(std::string_view const str)
{
    return pimpl_->add(str);
}
} // namespace transmission::intern
