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
#include <functional>
#include <iterator> // for std::next()
#include <optional>
#include <utility> // for std::pair<>
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

    void move_top(Item const* items, size_t n_items)
    {
        return move_top([&items, &n_items](auto const& test) { return contains(items, n_items, test); });
    }

    void move_bottom(Item const* items, size_t n_items)
    {
        return move_top([&items, &n_items](auto const& test) { return !contains(items, n_items, test); });
    }

    void move_up(Item const* items, size_t n_items)
    {
        for (size_t i = 0, end = std::size(items_); i != end; ++i)
        {
            if (auto const& [pos, item] = items_[i]; contains(items, n_items, item) && i > 0U)
            {
                std::swap(items_[i - 1U].second, items_[i].second);
            }
        }
    }

    void move_down(Item const* items, size_t n_items)
    {
        for (size_t begin = std::size(items_), i = begin, end = 0U; i != end; --i)
        {
            if (auto const& [pos, item] = items_[i - 1U]; contains(items, n_items, item) && i + 1 < begin)
            {
                std::swap(items_[i - 1U].second, items_[i].second);
            }
        }
    }

    void erase(Item const& item)
    {
        if (auto old_iter = find(item); old_iter != std::cend(items_))
        {
            items_.erase(old_iter);
        }
    }

    void set(Item item, size_t pos)
    {
        erase(item);

        auto iter = items_.emplace(
            std::lower_bound(std::cbegin(items_), std::cend(items_), pos, CompareByPos{}),
            pos,
            std::move(item));

        for (auto const end = std::cend(items_);;) // fix any pos collisions
        {
            auto const next = std::next(iter);

            if (next == end || iter->first != next->first)
            {
                break;
            }

            ++next->first;
            iter = next;
        }
    }

    [[nodiscard]] std::optional<size_t> get_position(Item const& item) const
    {
        if (auto iter = find(item); iter != std::cend(items_))
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

    void move_top(std::function<bool(Item const&)>&& test)
    {
        auto const n_items = std::size(items_);
        auto moved = decltype(items_){};
        moved.resize(n_items);
        for (size_t i = 0, end = n_items; i != end; ++i)
        {
            moved[i].first = items_[i].first;
        }

        auto const hits = std::count_if(
            std::cbegin(items_),
            std::cend(items_),
            [&test](auto const& pair) { return test(pair.second); });

        auto hit_count = size_t{};
        auto miss_count = size_t{};
        for (auto& [pos, item] : items_)
        {
            auto const idx = test(item) ? hit_count++ : hits + miss_count++;
            moved[idx].second = std::move(item);
        }

        std::swap(items_, moved);
    }

    [[nodiscard]] auto find(Item const& item) const
    {
        auto const pred = [&item = item](PairType const& pair)
        {
            return item == pair.second;
        };

        return std::find_if(std::cbegin(items_), std::cend(items_), pred);
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
