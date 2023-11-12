// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <condition_variable>
#include <cstddef> // size_t
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

#include "libtransmission/observable.h"

// A fixed-size cache that erases least-recently-used items to make room for new ones.
template<typename Key, typename Val, std::size_t N>
class tr_lru_cache
{
public:
    [[nodiscard]] TR_CONSTEXPR23 std::optional<std::pair<Val&, libtransmission::ObserverTag>> get(Key const& key) noexcept
    {
        auto const lock = std::lock_guard{ mutex_ };
        if (auto* const found = find(key); found != nullptr)
        {
            found->sequence_ = next_sequence_++;
            return lock_and_get_entry(*found);
        }

        return {};
    }

    [[nodiscard]] TR_CONSTEXPR23 bool contains(Key const& key) const noexcept
    {
        return find(key) != nullptr;
    }

    std::pair<Val&, libtransmission::ObserverTag> add(Key&& key)
    {
        auto const lock = std::lock_guard{ mutex_ };

        auto& entry = get_free_slot();
        entry.key_ = std::move(key);
        entry.sequence_ = next_sequence_++;

        key = {};
        return lock_and_get_entry(entry);
    }

    void erase(Key const& key)
    {
        auto const lock = std::lock_guard{ mutex_ };
        if (auto* const found = find(key); found != nullptr)
        {
            this->erase(*found);
        }
    }

    void erase_if(std::function<bool(Key const&, Val const&)> test)
    {
        auto const lock = std::lock_guard{ mutex_ };
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
        auto const lock = std::lock_guard{ mutex_ };
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
        size_t in_use_ = 0U;
        uint64_t sequence_ = InvalidSeq;
    };

    std::pair<Val&, libtransmission::ObserverTag> lock_and_get_entry(Entry& entry)
    {
        auto const lock = std::lock_guard{ mutex_ };
        ++entry.in_use_;
        return std::make_pair(
            std::ref(entry.val_),
            libtransmission::ObserverTag{ [this, &entry]()
                                          {
                                              auto lock2 = std::unique_lock{ mutex_ };
                                              --entry.in_use_;
                                              TR_ASSERT(entry.in_use_ >= 0U);
                                              lock2.unlock();
                                              cv_.notify_one();
                                          } });
    }

    void erase(Entry& entry)
    {
        auto const lock = std::lock_guard{ mutex_ };

        entry.key_ = {};
        entry.val_ = {};
        entry.sequence_ = InvalidSeq;
    }

    [[nodiscard]] TR_CONSTEXPR23 Entry* find(Key const& key) noexcept
    {
        auto const lock = std::lock_guard{ mutex_ };
        for (auto& entry : entries_)
        {
            if (entry.sequence_ != InvalidSeq && entry.key_ == key)
            {
                return &entry;
            }
        }

        return nullptr;
    }

    [[nodiscard]] TR_CONSTEXPR23 Entry const* find(Key const& key) const noexcept
    {
        auto const lock = std::lock_guard{ mutex_ };
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
        auto lock = std::unique_lock{ mutex_ };
        cv_.wait(
            lock,
            [this]() {
                return std::any_of(
                    std::begin(entries_),
                    std::end(entries_),
                    [](auto const& entry) { return entry.in_use_ == 0U; });
            });
        auto const iter = std::min_element(
            std::begin(entries_),
            std::end(entries_),
            [](auto const& a, auto const& b) { return b.in_use_ > 0U || a.sequence_ < b.sequence_; });
        erase(*iter);
        return *iter;
    }

    std::array<Entry, N> entries_;
    uint64_t next_sequence_ = 1U;
    static uint64_t constexpr InvalidSeq = 0U;

    mutable std::recursive_mutex mutex_;
    std::condition_variable_any cv_;
};
