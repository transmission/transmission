// This file Copyright © 2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm> // for std::move
#include <cstddef> // for size_t
#include <functional>

#include <small/map.hpp>

#include <libtransmission/tr-assert.h>

namespace libtransmission
{

// A simple observer/observable implementation.
// Intentionally avoids edge cases like thread safety and
// remove-during-emit; this is meant to be as lightweight
// as possible for very basic use cases.
template<typename... Args>
class SimpleObservable
{
    using key_t = size_t;

public:
    class Tag
    {
    public:
        [[nodiscard]] bool operator<(Tag const& that) const
        {
            return key_ < that.key_;
        }

        ~Tag()
        {
            TR_ASSERT(observable_->observers_.count(key_) == 1U);
            observable_->observers_.erase(key_);
        }

    private:
        friend class SimpleObservable;
        Tag(SimpleObservable* observable, key_t key)
            : observable_{ observable }
            , key_{ key }
        {
        }
        SimpleObservable* observable_;
        key_t key_;
    };

    using observer_t = std::function<void(Args...)>;

    ~SimpleObservable()
    {
        TR_ASSERT(std::empty(observers_));
    }

    Tag observe(observer_t observer)
    {
        auto const key = next_key_++;
        observers_.emplace(key, std::move(observer));
        return Tag{ this, key };
    }

    void emit(Args... args) const
    {
        for (auto& [tag, observer] : observers_)
        {
            observer((args)...);
        }
    }

private:
    friend class Tag;
    static auto inline next_key_ = key_t{ 1U };
    small::map<key_t, observer_t, 2> observers_;
};

} // namespace libtransmission
