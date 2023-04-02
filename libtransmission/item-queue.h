// This file Copyright © 2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <cstddef> // for size_t
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

#include "tr-macros.h" // for TR_CONSTEXPR20

namespace libtransmission
{
template<typename Item>
class ItemQueue
{
public:
    [[nodiscard]] TR_CONSTEXPR20 auto size() const noexcept
    {
        return std::size(items_);
    }

    [[nodiscard]] TR_CONSTEXPR20 auto empty() const noexcept
    {
        return std::empty(items_);
    }

    void move_top(Item const* top_items, size_t n_top_items)
    {
        auto const n_all_items = std::size(items_);
        auto moved = decltype(items_){};
        moved.resize(n_all_items);
        for (size_t i = 0, end = n_all_items; i != end; ++i)
        {
            moved[i].first = items_[i].first;
        }

        auto const hits = std::count_if(std::begin(items_), std::end(items_), [&top_items, &n_top_items](auto const& item){ return contains(top_items, n_top_items, item.second); });

        auto hit_count = size_t{};
        auto miss_count = size_t{};
        for (size_t i = 0, end = n_all_items; i != end; ++i)
        {
            if (contains(top_items, n_top_items, items_[i].second))
            {
                moved[hit_count++].second = std::move(items_[i].second);
            }
            else
            {
                moved[hits + miss_count++].second = std::move(items_[i].second);
            }
        }

        std::swap(items_, moved);
    }

    void move_up(Item const* items, size_t n_items)
    {
        for (size_t i = 0, end = std::size(items_); i != end; ++i)
        {
            if (auto const& [pos, item] = items_[i]; contains(items, n_items, item) && i > 0U)
            {
                std::swap(items_[i-1U].second, items_[i].second);
            }
        }
    }

    void move_down(Item const* items, size_t n_items)
    {
        for (size_t i = std::size(items_), end = 0U; i != end; --i)
        {
            if (auto const& [pos, item] = items_[i-1U]; contains(items, n_items, item) && i + 1 < std::size(items_))
            {
                std::swap(items_[i-1U].second, items_[i].second);
            }
        }
    }

    void move_bottom(Item const* items, size_t item_count);

    void erase(Item const& item)
    {
        if (auto old_iter = find(item); old_iter != std::end(items_))
        {
            items_.erase(old_iter);
        }
    }

    void set(Item item, size_t pos)
    {
        erase(item);
        auto const iter = std::lower_bound(std::begin(items_), std::end(items_), pos, CompareByPos{});
        items_.emplace(iter, pos, std::move(item));
    }

    [[nodiscard]] std::optional<size_t> get_position(Item const& item) const
    {
        if (auto iter = find(item); item != std::end(items_))
        {
            return iter - std::begin(items_);
        }

        return {};
    }

    [[nodiscard]] std::optional<Item> pop()
    {
        if (std::empty(items_))
        {
            return {};
        }

        auto item = std::move(items_.at(0).second);
        items_.erase(std::begin(items_));
        return item;
    }

    [[nodiscard]] std::vector<Item> queue() const
    {
        auto ret = std::vector<Item>{};
        ret.reserve(size());
        for (auto const& [pos, item] : items_)
        {
            ret.emplace_back(item);
        }
        return ret;
    }

private:
    using PairType = std::pair<size_t /*pos*/, Item>;
    std::vector<PairType> items_;

    [[nodiscard]] auto find(Item const& item) const
    {
        auto const pred = [&item = item](PairType const& pair)
        {
            return item == pair.second;
        };

        return std::find_if(std::begin(items_), std::end(items_), pred);
    }

    struct CompareByPos
    {

        [[nodiscard]] constexpr bool operator()(PairType const& pair, size_t pos)
        {
            return pair.first < pos;
        }

        [[nodiscard]] constexpr bool operator()(PairType const& a, PairType const& b)
        {
            return a.first < b.first;
        }
    };

    [[nodiscard]] static bool contains(Item const* items, size_t n_items, Item const& needle)
    {
        return std::find(items, items + n_items, needle) != items + n_items;
    }
};

} // namespace libtransmission