// This file Copyright 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // for size_t
#include <functional>
#include <memory>
#include <string_view>

struct event_base;

class tr_watchdir
{
public:
    tr_watchdir() = default;
    virtual ~tr_watchdir() = default;
    tr_watchdir(tr_watchdir&&) = delete;
    tr_watchdir(tr_watchdir const&) = delete;
    tr_watchdir& operator=(tr_watchdir&&) = delete;
    tr_watchdir& operator=(tr_watchdir const&) = delete;

    [[nodiscard]] virtual std::string_view dirname() const noexcept = 0;

    enum class Action
    {
        Done,
        Retry
    };

    using Callback = std::function<Action(std::string_view dirname, std::string_view basename)>;

    using TimeFunc = time_t (*)();

    static std::unique_ptr<tr_watchdir> create(
        std::string_view dirname,
        Callback callback,
        event_base* event_base,
        TimeFunc current_time_func);

    static std::unique_ptr<tr_watchdir> createGeneric(
        std::string_view dirname,
        Callback callback,
        event_base* event_base,
        TimeFunc current_time_func,
        size_t rescan_interval_msec = DefaultGenericRescanIntevalMsec);

private:
    static constexpr size_t DefaultGenericRescanIntevalMsec = 10000;
};
