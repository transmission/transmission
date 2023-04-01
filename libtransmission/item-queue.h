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

    void move_top(Item const* items, size_t item_count);

    void move_up(Item const* items, size_t item_count)
    {

        auto const do_move = [items, item_count](auto const& item)
        {
            return std::find(items, items + item_count, item) != items + item_count;
        };

        for (auto const& [lpos, litem] : items_) std::cerr << "[pos " << lpos << " item " << litem << ']'; std::cerr << std::endl;
        for (size_t i = 0, end = std::size(items_); i != end; ++i)
        {
            if (auto const& [pos, item] = items_[i]; do_move(item) && pos > 0U)
            {
                std::cerr << "due to " << item << " swapping " << items_[i-1U].second << " and " << items_[i].second << std::endl;
                std::swap(items_[i-1U].second, items_[i].second);
                for (auto const& [lpos, litem] : items_) std::cerr << "[pos " << lpos << " item " << litem << ']'; std::cerr << std::endl;
            }
        }
        for (auto const& [lpos, litem] : items_) std::cerr << "[pos " << lpos << " item " << litem << ']'; std::cerr << std::endl;
    }

    void move_down(Item const* items, size_t item_count);

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
};

} // namespace libtransmission
