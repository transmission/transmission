/*
 * This file Copyright (C) 2020 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "libtransmission/utils.h" // tr_free(), tr_wait_msec()

#include <chrono>
#include <functional>
#include <string>

namespace transmission
{

namespace tests
{

namespace helpers
{

auto const makeString = [](char*&& s)
    {
        auto const ret = std::string(s != nullptr ? s : "");
        tr_free(s);
        return ret;
    };

bool waitFor(std::function<bool()> const& test, int msec)
{
    auto const deadline = std::chrono::milliseconds { msec };
    auto const begin = std::chrono::steady_clock::now();

    for (;;)
    {
        if (test())
        {
            return true;
        }

        if ((std::chrono::steady_clock::now() - begin) >= deadline)
        {
            return false;
        }

        tr_wait_msec(10);
    }
}

} // namespace helpers

} // namespace tests

} // namespace transmission
