// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t
#include <cstdint>
#include <functional>
#include <utility>

// A fixed-size cache that erases least-recently-used items to make room for new ones.
template<typename Key, typename Val, std::size_t N>
class tr_lru_cache
{
public:
    [[nodiscard]] constexpr Val* get(Key const& key) noexcept
    {
        if (auto const found = find(key); found != nullptr)
        {
            found->sequence_ = next_sequence_++;
            return &found->val_;
        }

        return nullptr;
    }

    [[nodiscard]] constexpr bool contains(Key const& key) const noexcept
    {
        return find(key) != nullptr;
    }

    Val& add(Key&& key)
    {
        auto& entry = get_free_slot();
        entry.key_ = std::move(key);
        entry.sequence_ = next_sequence_++;

        key = {};
        return entry.val_;
    }

    void erase(Key const& key)
    {
        if (auto* const found = find(key); found != nullptr)
        {
            this->erase(*found);
        }
    }

    void erase_if(std::function<bool(Key const&, Val const&)> const& test)
    {
        for (auto& entry : entries_)
        {
            if (entry.sequence_ != InvalidSeq && test(entry.key_, entry.val_))
            {
                erase(entry);
            }
        }
    }

    void clear()
    {
        for (auto& entry : entries_)
        {
            erase(entry);
        }
    }

private:
    struct Entry
    {
        Key key_ = {};
        Val val_ = {};
        uint64_t sequence_ = InvalidSeq;
    };

    void erase(Entry& entry) const
    {
        entry.key_ = {};
        entry.val_ = {};
        entry.sequence_ = InvalidSeq;
    }

    [[nodiscard]] constexpr Entry* find(Key const& key) noexcept
    {
        for (auto& entry : entries_)
        {
            if (entry.sequence_ != InvalidSeq && entry.key_ == key)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    [[nodiscard]] constexpr Entry const* find(Key const& key) const noexcept
    {
        for (auto const& entry : entries_)
        {
            if (entry.sequence_ != InvalidSeq && entry.key_ == key)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    Entry& get_free_slot()
    {
        auto const iter = std::min_element(
            std::begin(entries_),
            std::end(entries_),
            [](auto const& a, auto const& b) { return a.sequence_ < b.sequence_; });
        erase(*iter);
        return *iter;
    }

    std::array<Entry, N> entries_;
    uint64_t next_sequence_ = 1U;
    static uint64_t constexpr InvalidSeq = 0U;
};
