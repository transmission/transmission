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

#include <fmt/core.h>

#include <small/map.hpp>

#include <libtransmission/tr-assert.h>

namespace libtransmission
{

// An RAII-based subscription to an Observable.
// Returned by SimpleObservable::observe().
// Let it go out-of-scope to cancel the subscription.
class ObserverTag
{
public:
    using Callback = std::function<void()>;

    ObserverTag() = default;
    ObserverTag(ObserverTag&&) = default;
    ObserverTag& operator=(ObserverTag&&) = default;
    ObserverTag(ObserverTag const&) = delete;
    ObserverTag& operator=(ObserverTag const&) = delete;

    ObserverTag(Callback on_destroy)
        : on_destroy_{ std::move(on_destroy) }
    {
    }

    ~ObserverTag()
    {
        if (on_destroy_)
            on_destroy_();
    }

private:
    Callback on_destroy_;
};

// A simple observer/observable implementation.
// Intentionally avoids edge cases like thread safety and
// remove-during-emit; this is meant to be as lightweight
// as possible for very basic use cases.
template<typename... Args>
class SimpleObservable
{
    using Key = size_t;

public:
    using Observer = std::function<void(Args...)>;

    ~SimpleObservable()
    {
        TR_ASSERT_MSG(std::empty(observers_), fmt::print("observers size {:d}\n", std::size(observers_)));
    }

    auto observe(Observer observer)
    {
        auto const key = next_key_++;
        observers_.emplace(key, std::move(observer));
        return ObserverTag{ [this, key]()
                            {
                                remove(key);
                            } };
    }

    void emit(Args... args) const
    {
        for (auto& [tag, observer] : observers_)
        {
            observer((args)...);
        }
    }

private:
    void remove(Key key)
    {
        [[maybe_unused]] auto const n_removed = observers_.erase(key);
        TR_ASSERT_MSG(n_removed == 1U, fmt::print("n_removed is {:d}\n", n_removed));
    }

    static auto inline next_key_ = Key{ 1U };
    small::map<Key, Observer, 2> observers_;
};

} // namespace libtransmission
