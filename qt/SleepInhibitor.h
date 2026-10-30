// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <functional>

// Blocks idle sleep only: the user can still put the computer to sleep.
class SleepInhibitor
{
public:
    // Returns a callable that releases the inhibition, or an empty one on failure.
    using Inhibit = std::function<std::function<void()>()>;

    explicit SleepInhibitor(Inhibit inhibit = platformInhibit);
    ~SleepInhibitor();
    SleepInhibitor(SleepInhibitor&&) = delete;
    SleepInhibitor(SleepInhibitor const&) = delete;
    SleepInhibitor& operator=(SleepInhibitor&&) = delete;
    SleepInhibitor& operator=(SleepInhibitor const&) = delete;

    void setInhibited(bool inhibited);

private:
    [[nodiscard]] static std::function<void()> platformInhibit();

    Inhibit const inhibit_;
    std::function<void()> release_;

    // Stops every refresh from retrying (and blocking on D-Bus) after a
    // failure; cleared once inhibiting is no longer wanted.
    bool failed_ = false;
};
