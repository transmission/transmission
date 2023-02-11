// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
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
        return !!find(key);
    }

    Val& add(Key&& key)
    {
        auto& entry = getFreeSlot();
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

    void erase_if(std::function<bool(Key const&, Val const&)> test)
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

    using PreEraseCallback = std::function<void(Key const&, Val&)>;

    void setPreErase(PreEraseCallback&& func)
    {
        pre_erase_cb_ = std::move(func);
    }

private:
    PreEraseCallback pre_erase_cb_ = [](Key const&, Val&) {
    };

    struct Entry
    {
        Key key_ = {};
        Val val_ = {};
        uint64_t sequence_ = InvalidSeq;
    };

    void erase(Entry& entry)
    {
        if (entry.sequence_ != InvalidSeq)
        {
            pre_erase_cb_(entry.key_, entry.val_);
        }

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

    Entry& getFreeSlot()
    {
        auto const iter = std::min_element(
            std::begin(entries_),
            std::end(entries_),
            [](auto const& a, auto const& b) { return a.sequence_ < b.sequence_; });
        this->erase(*iter);
        return *iter;
    }

    std::array<Entry, N> entries_;
    uint64_t next_sequence_ = 1;
    static uint64_t constexpr InvalidSeq = 0;
};
